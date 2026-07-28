from __future__ import annotations

import copy
import json
import tempfile
import unittest
from pathlib import Path

import lg_performance_policy as policy_module
from lg_performance_policy import PerformancePolicyError


class PerformancePolicyTests(unittest.TestCase):
    def setUp(self) -> None:
        self.policy = {
            "version": 1,
            "profile": "test",
            "expected_scenarios": ["bench"],
            "required_repetitions": 3,
            "minimum_valid_runs": 3,
            "stability_cv_percent": 10.0,
            "gpu_required": False,
            "optional_diagnostics_affect_outcome": True,
            "comparability": {
                "fatal": [
                    "schema_version", "scenario.name", "scenario_hash", "scenario.map",
                    "map_content_hash", "settings.backend", "environment.cpu",
                    "environment.os", "environment.architecture", "environment.compiler",
                    "environment.build_mode", "environment.protocol_version",
                ],
                "warning": ["environment.logical_cores"],
                "info": ["git.commit"],
            },
            "metrics": {
                "frame_p95": {
                    "source": "frame_ms", "statistic": "p95", "label": "Frame p95", "unit": "ms",
                    "direction": "lower", "required": True,
                    "warn_relative_percent": 5.0, "warn_absolute": 0.2,
                    "fail_relative_percent": 10.0, "fail_absolute": 0.5,
                },
                "gpu_time": {
                    "source": "gpu_ms", "statistic": "median", "label": "GPU", "unit": "ms",
                    "direction": "lower", "required": False,
                    "warn_relative_percent": 5.0, "warn_absolute": 0.1,
                    "fail_relative_percent": 10.0, "fail_absolute": 0.3,
                },
            },
            "hard_limits": [
                {
                    "field": "configured_datagram_ceiling_bytes",
                    "label": "Configured packet ceiling",
                    "required": True,
                    "equals": 1200,
                },
                {"field": "max_app_datagram_bytes", "label": "Packet", "required": True, "maximum": 1200},
                {"field": "snapshot_encode_failures", "label": "Encode", "required": True, "maximum": 0},
            ],
            "correctness": [
                {"path": "aggregate.valid", "label": "Completed", "required": True, "equals": True},
                {"path": "assertion_failures", "label": "Assertions", "required": True, "equals": 0},
                {"path": "determinism_failures", "label": "Determinism", "required": True, "equals": 0},
                {"path": "cleanup_failures", "label": "Cleanup", "required": True, "equals": 0},
            ],
        }

    def manifest(self, p95_values=(10.0, 10.0, 10.0), gpu_values=(2.0, 2.0, 2.0)) -> dict:
        runs = []
        for p95, gpu in zip(p95_values, gpu_values):
            runs.append({
                "valid": True,
                "summary": {
                    "frame_ms": {"p95": p95},
                    "gpu_ms": {"median": gpu},
                    "max_app_datagram_bytes": 1000,
                    "configured_datagram_ceiling_bytes": 1200,
                    "snapshot_encode_failures": 0,
                    "assertion_failures": 0,
                    "determinism_failures": 0,
                    "cleanup_failures": 0,
                },
            })
        return {
            "schema_version": 1,
            "scenario": {"name": "bench", "map": "arena"},
            "scenario_hash": "same",
            "map_content_hash": "map-hash",
            "settings": {"backend": "headless"},
            "environment": {
                "cpu": "cpu", "os": "os", "architecture": "x64", "compiler": "clang",
                "build_mode": "release", "protocol_version": 7, "logical_cores": 8,
            },
            "git": {"commit": "base"},
            "aggregate": {"valid": True},
            "runs": runs,
        }

    def result_set(self, manifest: dict, root: str) -> dict:
        return {"schema_version": 1, "root": root, "scenarios": {"bench": manifest}, "artifacts": {}}

    def compare(self, baseline: dict, candidate: dict) -> dict:
        return policy_module.compare_result_sets(
            self.result_set(baseline, "base"), self.result_set(candidate, "candidate"), self.policy
        )

    def compare_with_policy(
        self, baseline: dict, candidate: dict, policy: dict
    ) -> dict:
        return policy_module.compare_result_sets(
            self.result_set(baseline, "base"),
            self.result_set(candidate, "candidate"),
            policy,
        )

    @staticmethod
    def _set_path(value: dict, path: str, leaf: object) -> None:
        target = value
        parts = path.split(".")
        for part in parts[:-1]:
            target = target.setdefault(part, {})
        target[parts[-1]] = leaf

    def native_gpu_manifest(self, policy: dict) -> dict:
        """Build the smallest artifact emitted by a native GPU scenario."""
        scenario = policy["expected_scenarios"][0]
        manifest = {
            "schema_version": 1,
            "scenario": {"name": scenario},
            "scenario_hash": "stable",
            "map_content_hash": "stable",
            "settings": {"backend": "SDL_GPU/vulkan"},
            "environment": {"renderer": "SDL_GPU/vulkan", "gpu_verified": True},
            "aggregate": {"valid": True},
            "runs": [],
        }
        for path in (*policy["comparability"]["fatal"], *policy["comparability"]["warning"]):
            self._set_path(manifest, path, "stable")
        self._set_path(manifest, "scenario.name", scenario)
        manifest["scenario"]["actors"] = {}
        self._set_path(manifest, "scenario.actors.expected_count", 2)
        self._set_path(manifest, "settings.backend", "SDL_GPU/vulkan")
        self._set_path(manifest, "environment.renderer", "SDL_GPU/vulkan")
        self._set_path(manifest, "environment.gpu_verified", True)
        for _ in range(policy["required_repetitions"]):
            summary = {}
            for rule in policy["metrics"].values():
                if not rule["required"]:
                    continue
                summary.setdefault(rule["source"], {})[rule["statistic"]] = 1.0
            manifest["runs"].append({"valid": True, "summary": summary})
        return manifest

    def test_shipped_policy_has_graphics_profiles(self) -> None:
        path = Path(__file__).parents[1] / "config" / "performance-policy.json"
        headless = policy_module.load_policy(path, "pr_headless")
        gpu = policy_module.load_policy(path, "trusted_gpu")
        competitive = policy_module.load_policy(path, "trusted_gpu_competitive")
        self.assertEqual(headless["version"], 1)
        self.assertTrue(gpu["gpu_required"])
        self.assertTrue(competitive["gpu_required"])
        self.assertEqual(gpu["expected_scenarios"], ["eyetoeye-readability-pan"])
        self.assertEqual(competitive["expected_scenarios"], ["eyetoeye-readability-pan-competitive"])
        self.assertTrue(gpu["metrics"]["gpu_primary_median"]["required"])
        self.assertTrue(competitive["metrics"]["gpu_primary_median"]["required"])
        self.assertEqual(gpu["metrics"]["gpu_primary_median"]["relative_cap_percent"], 25.0)
        self.assertEqual(competitive["metrics"]["gpu_primary_median"]["relative_cap_percent"], 15.0)
        self.assertIn("environment.compiler_version", headless["comparability"]["fatal"])
        self.assertIn("environment.build_type", headless["comparability"]["fatal"])
        self.assertIn("environment.compile_time_options", headless["comparability"]["fatal"])
        self.assertNotIn("environment.sdl_configuration", headless["comparability"]["fatal"])
        self.assertIn("environment.sdl_configuration", gpu["comparability"]["fatal"])
        self.assertIn("environment.observed_resolution", gpu["comparability"]["fatal"])
        self.assertIn("environment.vulkan_icd_manifest_records", gpu["comparability"]["fatal"])
        self.assertNotIn("environment.executable_sha256", gpu["comparability"]["fatal"])
        self.assertIn("environment.executable_sha256", gpu["comparability"]["info"])
        self.assertNotIn("environment.executable_sha256", competitive["comparability"]["fatal"])
        self.assertIn("environment.executable_sha256", competitive["comparability"]["info"])
        self.assertIn("settings.graphics_contract", gpu["comparability"]["fatal"])
        self.assertTrue(all(rule["required"] for rule in headless["hard_limits"]))
        self.assertTrue(headless["optional_diagnostics_affect_outcome"])
        for profile in (gpu, competitive):
            self.assertFalse(profile["optional_diagnostics_affect_outcome"])
            self.assertEqual(profile["hard_limits"], [])
            self.assertNotIn("cleanup_failures", [rule["path"] for rule in profile["correctness"]])
            self.assertTrue(profile["metrics"]["gpu_primary_p99"]["required"])
            self.assertTrue(profile["metrics"]["long_frames_16_67"]["required"])

    def test_native_gpu_self_compare_passes_and_missing_primary_median_fails(self) -> None:
        path = Path(__file__).parents[1] / "config" / "performance-policy.json"
        for profile_name in ("trusted_gpu", "trusted_gpu_competitive"):
            policy = policy_module.load_policy(path, profile_name)
            baseline = self.native_gpu_manifest(policy)
            result_set = lambda manifest: {
                "schema_version": 1,
                "root": profile_name,
                "scenarios": {policy["expected_scenarios"][0]: manifest},
                "artifacts": {},
            }
            compared = policy_module.compare_result_sets(result_set(baseline), result_set(copy.deepcopy(baseline)), policy)
            self.assertEqual(compared["status"], "PASS")
            required = {name for name, rule in policy["metrics"].items() if rule["required"]}
            self.assertTrue(all(item["status"] == "PASS" for item in compared["metrics"] if item["name"] in required))

            missing = copy.deepcopy(baseline)
            for run in missing["runs"]:
                del run["summary"]["gpu_primary_command_buffer_ms"]["median"]
            compared = policy_module.compare_result_sets(result_set(baseline), result_set(missing), policy)
            self.assertEqual(compared["status"], "FAIL")

    def test_gpu_relative_cap_boundaries_use_measurement_floor(self) -> None:
        path = Path(__file__).parents[1] / "config" / "performance-policy.json"
        for profile_name, cap in (("trusted_gpu", 25.0), ("trusted_gpu_competitive", 15.0)):
            policy = policy_module.load_policy(path, profile_name)
            policy["expected_scenarios"] = ["bench"]
            policy["required_repetitions"] = 3
            policy["minimum_valid_runs"] = 3
            policy["gpu_required"] = False
            policy["comparability"] = {"fatal": [], "warning": [], "info": []}
            policy["metrics"] = {"gpu": policy["metrics"]["gpu_primary_median"]}
            policy["hard_limits"] = []
            policy["correctness"] = []

            def gpu_manifest(value: float) -> dict:
                manifest = self.manifest()
                for run in manifest["runs"]:
                    run["summary"]["gpu_primary_command_buffer_ms"] = {"median": value}
                return manifest

            exact_cap = 0.1 * (1.0 + cap / 100.0)
            self.assertEqual(self.compare_with_policy(gpu_manifest(0.1), gpu_manifest(exact_cap), policy)["status"], "PASS")
            just_over_cap = exact_cap + 0.0001
            self.assertEqual(self.compare_with_policy(gpu_manifest(0.1), gpu_manifest(just_over_cap), policy)["status"], "INCONCLUSIVE")
            self.assertEqual(self.compare_with_policy(gpu_manifest(0.1), gpu_manifest(0.16), policy)["status"], "FAIL")
            missing = gpu_manifest(0.1)
            for run in missing["runs"]:
                del run["summary"]["gpu_primary_command_buffer_ms"]
            self.assertEqual(self.compare_with_policy(gpu_manifest(0.1), missing, policy)["status"], "FAIL")

    def test_trusted_gpu_rejects_sdl_configuration_differences(self) -> None:
        path = Path(__file__).parents[1] / "config" / "performance-policy.json"
        policy = policy_module.load_policy(path, "trusted_gpu")
        policy["expected_scenarios"] = ["bench"]
        policy["comparability"] = {
            "fatal": ["environment.sdl_configuration"],
            "warning": [],
            "info": [],
        }
        policy["metrics"] = {}
        policy["hard_limits"] = []
        policy["correctness"] = []

        baseline = self.manifest()
        candidate = self.manifest()
        baseline["settings"]["backend"] = "SDL_GPU/vulkan"
        candidate["settings"]["backend"] = "SDL_GPU/vulkan"
        for manifest in (baseline, candidate):
            manifest["environment"]["renderer"] = "SDL_GPU/vulkan"
            manifest["environment"]["gpu_verified"] = True
        baseline["environment"]["sdl_configuration"] = {
            "LG_DUEL_SDL3_GIT_TAG": "release-3.4.10"
        }
        candidate["environment"]["sdl_configuration"] = {
            "LG_DUEL_SDL3_GIT_TAG": "8e37db5e"
        }

        compared = self.compare_with_policy(baseline, candidate, policy)
        self.assertEqual(compared["status"], "NOT_COMPARABLE")
        self.assertFalse(compared["comparable"])
        self.assertEqual(
            compared["comparability"]["fatal"][0]["field"],
            "environment.sdl_configuration",
        )

    def test_policy_rejects_unknown_version_and_field(self) -> None:
        path = Path(__file__).parents[1] / "config" / "performance-policy.json"
        raw = json.loads(path.read_text(encoding="utf-8"))
        with tempfile.TemporaryDirectory() as temporary:
            target = Path(temporary) / "policy.json"
            changed = copy.deepcopy(raw)
            changed["policy_version"] = 2
            target.write_text(json.dumps(changed), encoding="utf-8")
            with self.assertRaisesRegex(PerformancePolicyError, "unsupported policy version"):
                policy_module.load_policy(target, "pr_headless")
            changed = copy.deepcopy(raw)
            changed["profiles"]["pr_headless"]["surprise"] = True
            target.write_text(json.dumps(changed), encoding="utf-8")
            with self.assertRaisesRegex(PerformancePolicyError, "unknown field"):
                policy_module.load_policy(target, "pr_headless")

    def test_result_loader_missing_malformed_duplicate_and_inconsistent(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            with self.assertRaisesRegex(PerformancePolicyError, "no aggregate"):
                policy_module.load_result_directory(root, ["bench"])
            (root / "aggregate.json").write_text("{", encoding="utf-8")
            with self.assertRaisesRegex(PerformancePolicyError, "malformed"):
                policy_module.load_result_directory(root, ["bench"])
            (root / "aggregate.json").write_text(json.dumps(self.manifest()), encoding="utf-8")
            with self.assertRaisesRegex(PerformancePolicyError, "missing aggregate"):
                policy_module.load_result_directory(root, ["other"])
            child = root / "copy"
            child.mkdir()
            (child / "aggregate.json").write_text(json.dumps(self.manifest()), encoding="utf-8")
            with self.assertRaisesRegex(PerformancePolicyError, "duplicate"):
                policy_module.load_result_directory(root, ["bench"])
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            bad = self.manifest()
            bad["schema_version"] = 9
            (root / "aggregate.json").write_text(json.dumps(bad), encoding="utf-8")
            with self.assertRaisesRegex(PerformancePolicyError, "unsupported schema"):
                policy_module.load_result_directory(root, ["bench"])

    def test_pass_warn_fail_and_combined_thresholds(self) -> None:
        base = self.manifest()
        self.assertEqual(self.compare(base, self.manifest((10.1, 10.1, 10.1)))["status"], "PASS")
        self.assertEqual(self.compare(base, self.manifest((10.6, 10.6, 10.6)))["status"], "WARN")
        self.assertEqual(self.compare(base, self.manifest((11.1, 11.1, 11.1)))["status"], "FAIL")
        # Relative fail alone is not enough.
        tiny_base = self.manifest((1.0, 1.0, 1.0))
        self.assertEqual(self.compare(tiny_base, self.manifest((1.2, 1.2, 1.2)))["status"], "PASS")

    def test_zero_baseline_uses_absolute_threshold_only(self) -> None:
        base = self.manifest((0.0, 0.0, 0.0))
        compared = self.compare(base, self.manifest((0.6, 0.6, 0.6)))
        metric = next(item for item in compared["metrics"] if item["name"] == "frame_p95")
        self.assertEqual(metric["status"], "FAIL")
        self.assertIsNone(metric["relative_change_percent"])

    def test_inconclusive_for_noise_and_insufficient_runs(self) -> None:
        noisy = self.manifest((5.0, 10.0, 15.0))
        self.assertEqual(self.compare(self.manifest(), noisy)["status"], "INCONCLUSIVE")
        short = self.manifest((10.0, 10.0), (2.0, 2.0))
        self.assertEqual(self.compare(self.manifest(), short)["status"], "INCONCLUSIVE")

    def test_optional_missing_is_unavailable_required_missing_fails(self) -> None:
        candidate = self.manifest()
        for run in candidate["runs"]:
            del run["summary"]["gpu_ms"]
        compared = self.compare(self.manifest(), candidate)
        gpu = next(item for item in compared["metrics"] if item["name"] == "gpu_time")
        self.assertEqual(gpu["status"], "UNAVAILABLE")
        for run in candidate["runs"]:
            del run["summary"]["frame_ms"]
        compared = self.compare(self.manifest(), candidate)
        frame = next(item for item in compared["metrics"] if item["name"] == "frame_p95")
        self.assertEqual(frame["status"], "FAIL")

    def test_comparability_fatal_warning_and_gpu_refusal(self) -> None:
        warning = self.manifest()
        warning["environment"]["logical_cores"] = 16
        self.assertEqual(self.compare(self.manifest(), warning)["status"], "WARN")
        fatal = self.manifest()
        fatal["scenario_hash"] = "different"
        compared = self.compare(self.manifest(), fatal)
        self.assertEqual(compared["status"], "NOT_COMPARABLE")
        self.assertFalse(compared["comparable"])
        self.assertTrue(all(item["status"] == "SKIPPED" for item in compared["metrics"]))
        gpu_policy = copy.deepcopy(self.policy)
        gpu_policy["gpu_required"] = True
        compared = policy_module.compare_result_sets(
            self.result_set(self.manifest(), "base"), self.result_set(self.manifest(), "candidate"), gpu_policy
        )
        self.assertEqual(compared["status"], "NOT_COMPARABLE")

    def test_required_comparability_presence_is_fatal(self) -> None:
        candidate = self.manifest()
        del candidate["environment"]["compiler"]
        compared = self.compare(self.manifest(), candidate)
        self.assertEqual(compared["status"], "NOT_COMPARABLE")
        self.assertEqual(compared["comparability"]["fatal"][0]["reason"], "required field missing")

    def test_hard_packet_and_correctness_failures(self) -> None:
        packet = self.manifest()
        packet["runs"][1]["summary"]["max_app_datagram_bytes"] = 1201
        compared = self.compare(self.manifest(), packet)
        self.assertEqual(compared["status"], "FAIL")
        self.assertEqual(next(item for item in compared["correctness"] if item["field"] == "max_app_datagram_bytes")["observed"], 1201.0)
        broken = self.manifest()
        broken["runs"][0]["summary"]["determinism_failures"] = 1
        self.assertEqual(self.compare(self.manifest(), broken)["status"], "FAIL")
        changed_and_broken = self.manifest()
        changed_and_broken["scenario_hash"] = "different"
        changed_and_broken["runs"][0]["summary"]["max_app_datagram_bytes"] = 1201
        compared = self.compare(self.manifest(), changed_and_broken)
        self.assertEqual(compared["status"], "FAIL")
        self.assertFalse(compared["comparable"])

    def test_outlier_report_is_retained(self) -> None:
        candidate = self.manifest((10.0, 10.0, 10.0, 40.0), (2.0, 2.0, 2.0, 2.0))
        compared = self.compare(self.manifest((10.0, 10.0, 10.0, 10.0), (2.0, 2.0, 2.0, 2.0)), candidate)
        frame = next(item for item in compared["metrics"] if item["name"] == "frame_p95")
        self.assertEqual(frame["candidate_statistics"]["outliers"]["indices"], [4])

    def test_report_output_is_deterministic_and_correctness_first(self) -> None:
        compared = self.compare(self.manifest(), self.manifest((10.6, 10.6, 10.6)))
        first = policy_module.render_markdown(compared)
        second = policy_module.render_markdown(copy.deepcopy(compared))
        self.assertEqual(first, second)
        self.assertLess(first.index("### Correctness"), first.index("### Metrics"))
        with tempfile.TemporaryDirectory() as temporary:
            paths = policy_module.write_reports(compared, temporary)
            original = Path(paths["json"]).read_bytes()
            policy_module.write_reports(compared, temporary)
            self.assertEqual(original, Path(paths["json"]).read_bytes())

    def test_named_fixture_cases_cover_requested_states(self) -> None:
        fixture = Path(__file__).parents[1] / "tests" / "benchmark" / "fixtures" / "performance-policy" / "cases.json"
        cases = json.loads(fixture.read_text(encoding="utf-8"))["cases"]
        self.assertTrue({
            "PASS", "WARN", "FAIL", "INCONCLUSIVE", "NOT_COMPARABLE", "UNAVAILABLE",
            "packet_over_1200", "different_gpu", "missing_required_metric",
            "insufficient_runs", "tukey_outlier", "encode_failure",
        }.issubset(cases))


if __name__ == "__main__":
    unittest.main()
