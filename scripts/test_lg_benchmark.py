from __future__ import annotations

import copy
import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import lg_benchmark
from lg_benchmark import BenchmarkError
from lg_launch import LaunchError


class BenchmarkTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.catalog = json.loads(
            (lg_benchmark.SCENARIO_ROOT / "eyetoeye-static-baseline.json").read_text(encoding="utf-8")
        )

    def scenario(self) -> dict:
        value = copy.deepcopy(self.catalog)
        value["name"] = "test-scenario"
        return value

    def test_catalog_scenarios_validate(self) -> None:
        listed = lg_benchmark.list_scenarios()
        self.assertGreaterEqual(listed["count"], 5)
        self.assertTrue(all(entry["valid"] for entry in listed["scenarios"]))

    def test_invalid_duration_and_resolution(self) -> None:
        value = self.scenario()
        value["warmup_frames"] = 10
        with self.assertRaisesRegex(BenchmarkError, "exactly one"):
            lg_benchmark.validate_scenario(value)
        value = self.scenario()
        value["resolution"] = [0, 720]
        with self.assertRaisesRegex(BenchmarkError, "positive"):
            lg_benchmark.validate_scenario(value)
        value = self.scenario()
        value["measured_seconds"] = 0
        with self.assertRaisesRegex(BenchmarkError, "at least"):
            lg_benchmark.validate_scenario(value)

    def test_strict_unknown_field(self) -> None:
        value = self.scenario()
        value["surprise"] = True
        with self.assertRaisesRegex(BenchmarkError, "unknown"):
            lg_benchmark.validate_scenario(value)

    def test_canonical_hash_is_order_independent_and_sensitive(self) -> None:
        validated = lg_benchmark.validate_scenario(self.scenario())
        reversed_order = dict(reversed(list(validated.items())))
        self.assertEqual(lg_benchmark.scenario_hash(validated), lg_benchmark.scenario_hash(reversed_order))
        changed = copy.deepcopy(validated)
        changed["fov"] += 1
        self.assertNotEqual(lg_benchmark.scenario_hash(validated), lg_benchmark.scenario_hash(changed))

    def test_percentile_aggregate_outliers_and_stability(self) -> None:
        self.assertEqual(lg_benchmark.percentile([1, 2, 3, 4, 5], 95), 5)
        runs = [{"valid": True, "summary": {"frame_ms": {"median": value}}} for value in (10, 10.1, 9.9, 40)]
        aggregate = lg_benchmark.aggregate_runs(runs)
        self.assertEqual(aggregate["outlier_runs"], [4])
        self.assertFalse(aggregate["stable"])
        stable = lg_benchmark.aggregate_runs(runs[:3])
        self.assertTrue(stable["stable"])

    def test_vulkan_summary_and_icd_metadata(self) -> None:
        summary = """GPU0:\n apiVersion = 1.4.348\n driverVersion = 101.8861\n deviceType = PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU\n deviceName = Intel(R) Arc(TM) 140V GPU (16GB)\n driverName = Intel Corporation\n driverInfo = 101.8861\n"""
        parsed = lg_benchmark._parse_vulkan_summary(summary)
        self.assertEqual(parsed["gpu_name"], "Intel(R) Arc(TM) 140V GPU (16GB)")
        self.assertEqual(parsed["graphics_driver_version"], "101.8861")
        with tempfile.TemporaryDirectory() as temporary:
            manifest = Path(temporary) / "icd.json"
            manifest.write_text('{"ICD":{"library_path":"driver.dll"}}', encoding="utf-8")
            completed = mock.Mock(returncode=0, stdout=summary, stderr="")
            with mock.patch.dict("os.environ", {"VK_DRIVER_FILES": str(manifest)}, clear=False), \
                 mock.patch("subprocess.run", return_value=completed):
                metadata = lg_benchmark._vulkan_metadata("SDL_GPU/vulkan")
        self.assertEqual(metadata["vulkan_metadata_status"], "available")
        self.assertEqual(metadata["vulkan_icd_manifests"], [str(manifest)])
        self.assertRegex(metadata["vulkan_icd_manifest_records"][0]["sha256"], r"^[0-9a-f]{64}$")

    def test_aggregate_preserves_native_tail_statistics(self) -> None:
        runs = [
            {"valid": True, "summary": {"frame_ms": {"median": 10, "p95": 15, "p99": 20, "max": 30}}},
            {"valid": True, "summary": {"frame_ms": {"median": 9, "p95": 18, "p99": 24, "max": 40}}},
            {"valid": True, "summary": {"frame_ms": {"median": 8, "p95": 17, "p99": 22, "max": 35}}},
        ]
        metric = lg_benchmark.aggregate_runs(runs)["metrics"]["frame_ms"]
        self.assertEqual(metric["median"], 9)
        self.assertEqual(metric["p95"], 17)
        self.assertEqual(metric["p99"], 22)
        self.assertEqual(metric["max"], 35)

    def test_tail_regression_cannot_be_masked_by_lower_median(self) -> None:
        classification, details = lg_benchmark.classify_metric(
            {"median": 10, "p95": 15, "p99": 20, "max": 25, "cv_percent": 1},
            {"median": 9, "p95": 14, "p99": 22, "max": 25, "cv_percent": 1},
        )
        self.assertEqual(classification, "regression")
        self.assertGreater(details["statistics"]["p99"]["percent"], 3)

    def _artifact(self, root: Path, name: str, median: float, *, scenario_hash: str = "abc") -> dict:
        metrics = {"frame_ms": {"median": median, "mean": median, "p95": median, "p99": median,
                                "max": median, "cv_percent": 1.0}}
        return {
            "schema_version": 1, "scenario_hash": scenario_hash,
            "scenario": {"name": "test", "schema_version": 1, "expected_benchmark_version": 1},
            "settings": {"backend": "gpu", "resolution": [1280, 720], "window_mode": "windowed",
                         "vsync": False, "frame_cap": 0, "fov": 100, "presentation_cvars": {}},
            "environment": {"renderer": "gpu", "protocol_version": 1},
            "aggregate": {"valid": True, "metrics": metrics}, "result_directory": str(root / name),
        }

    def test_compare_invalid_and_classification(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            results, baselines = root / "results", root / "results" / "baselines"
            (baselines / "base").mkdir(parents=True)
            (results / "candidate").mkdir()
            (baselines / "base" / "aggregate.json").write_text(json.dumps(self._artifact(root, "base", 10)), encoding="utf-8")
            (results / "candidate" / "aggregate.json").write_text(json.dumps(self._artifact(root, "candidate", 11)), encoding="utf-8")
            with mock.patch.object(lg_benchmark, "RESULT_ROOT", results), mock.patch.object(lg_benchmark, "BASELINE_ROOT", baselines):
                compared = lg_benchmark.compare_results("base", results / "candidate")
                self.assertEqual(compared["classification"], "regression")
                self.assertTrue((results / "candidate" / "comparison.json").is_file())
                changed = self._artifact(root, "candidate", 11, scenario_hash="different")
                (results / "candidate" / "aggregate.json").write_text(json.dumps(changed), encoding="utf-8")
                invalid = lg_benchmark.compare_results("base", results / "candidate")
                self.assertEqual(invalid["classification"], "invalid")
                self.assertIn("scenario hash", invalid["mismatches"])

    def test_gpu_driver_change_invalidates_comparison(self) -> None:
        baseline = self._artifact(Path("root"), "base", 10)
        result = self._artifact(Path("root"), "result", 10)
        baseline["environment"].update({"gpu_name": "GPU A", "graphics_driver_version": "1", "vulkan_api_version": "1.3"})
        result["environment"].update({"gpu_name": "GPU A", "graphics_driver_version": "2", "vulkan_api_version": "1.3"})
        self.assertIn("graphics_driver_version", lg_benchmark._comparison_mismatch(baseline, result))

    def test_result_schema_generation_and_request_contract(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            scenarios, results = root / "scenarios", root / "results"
            scenarios.mkdir()
            value = self.scenario()
            (scenarios / "test-scenario.json").write_text(json.dumps(value), encoding="utf-8")
            calls = []

            def sender(operation: str, **kwargs):
                calls.append((operation, kwargs))
                return {"summary": {"count": 100, "mean_ms": 10, "median_ms": 10,
                                    "p95_ms": 12, "p99_ms": 14, "max_ms": 15, "stddev_ms": 0.2},
                        "validity": {"map": True, "completed": True, "frame_count": True}}

            with mock.patch.object(lg_benchmark, "SCENARIO_ROOT", scenarios), mock.patch.object(lg_benchmark, "RESULT_ROOT", results):
                result = lg_benchmark.run_benchmark("test-scenario", repetitions=2, request_sender=sender, start_client=False)
            self.assertEqual(result["schema_version"], 1)
            self.assertTrue((Path(result["result_directory"]) / "aggregate.json").is_file())
            self.assertEqual(len(calls), 2)
            self.assertEqual(calls[0][0], "run_benchmark")
            self.assertEqual(calls[0][1]["scenario"], value)
            self.assertRegex(calls[0][1]["scenario_hash"], r"^[0-9a-f]{64}$")
            self.assertIn("run_group", calls[0][1])

    def test_unsafe_names_and_paths(self) -> None:
        for value in ("../escape", "..", "slash/name", ""):
            with self.assertRaises(BenchmarkError):
                lg_benchmark.validate_safe_name(value)
        with self.assertRaisesRegex(BenchmarkError, "not a path"):
            lg_benchmark.load_scenario("../escape")

    def test_timeout_is_actionable(self) -> None:
        with mock.patch.object(
            lg_benchmark, "ensure_client", side_effect=LaunchError("startup timed out while waiting for Vulkan")
        ):
            with self.assertRaisesRegex(BenchmarkError, "startup timed out"):
                lg_benchmark._start_client(27961, 1)


if __name__ == "__main__":
    unittest.main()
