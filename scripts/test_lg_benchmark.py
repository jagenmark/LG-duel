from __future__ import annotations

import copy
import json
import socket
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import lg_benchmark
import lg_launch
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

    def test_worker_material_quality_descriptors_differ_only_by_quality(self) -> None:
        for prefix in ("worker-material-q", "worker-material-review-q"):
            scenarios = [
                lg_benchmark.load_scenario(f"{prefix}{quality}")[0]
                for quality in range(3)
            ]
            reference = copy.deepcopy(scenarios[0])
            reference["name"] = "worker-material-quality"
            reference["cvars"].pop("r_material_quality")
            for quality, scenario in enumerate(scenarios):
                self.assertEqual(scenario["cvars"]["r_material_quality"], quality)
                self.assertEqual(scenario["cvars"]["r_player_model"], 2)
                comparable = copy.deepcopy(scenario)
                comparable["name"] = "worker-material-quality"
                comparable["cvars"].pop("r_material_quality")
                self.assertEqual(comparable, reference)

    def test_graphics_contract_requires_native_effective_cvars(self) -> None:
        effective = {
            "r_antialiasing": "1", "r_sun_shadows": "2",
            "r_contact_shadows": "1", "r_material_quality": "1",
            "r_player_rim": "1", "r_atmosphere_grade": "2",
            "r_bloom": "1", "r_render_scale": "1.250000",
        }
        contract = lg_benchmark.graphics_contract_from_native(
            {"effective_cvars": effective}, "Default"
        )
        self.assertEqual(contract["profile"], "Default")
        self.assertEqual(contract["anti_aliasing"], "1")
        self.assertEqual(contract["sun_shadow_quality"], "2")
        self.assertEqual(contract["render_scale"], "1.250000")
        incomplete = dict(effective)
        del incomplete["r_bloom"]
        with self.assertRaisesRegex(BenchmarkError, "r_bloom"):
            lg_benchmark.graphics_contract_from_native(
                {"effective_cvars": incomplete}, "Default"
            )

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

    def test_benchmarks_default_to_muted_audio_and_keep_explicit_overrides(self) -> None:
        scenario = self.scenario()
        scenario["cvars"].pop("s_volume", None)
        self.assertEqual(
            lg_benchmark.validate_scenario(scenario)["cvars"]["s_volume"],
            0,
        )
        scenario["cvars"]["s_volume"] = 0.5
        self.assertEqual(
            lg_benchmark.validate_scenario(scenario)["cvars"]["s_volume"],
            0.5,
        )

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

    def test_build_environment_metadata_records_comparable_options(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            build_dir = Path(temporary)
            (build_dir / "CMakeCache.txt").write_text(
                "\n".join(
                    (
                        "CMAKE_BUILD_TYPE:STRING=Release",
                        "CMAKE_GENERATOR:INTERNAL=Ninja",
                        "CMAKE_CXX_COMPILER:FILEPATH=C:/tools/clang++.exe",
                        "BUILD_TESTING:BOOL=ON",
                        "LG_DUEL_SIMD_MODE:STRING=portable",
                        "LG_DUEL_FETCH_SDL3:BOOL=ON",
                        "LG_DUEL_REQUIRE_SDL3:BOOL=OFF",
                        "LG_DUEL_SDL3_GIT_TAG:STRING=release-3.4.10",
                        "LG_DUEL_SDL3_SOURCE_DIR:PATH=C:/src/SDL",
                        "LG_DUEL_USE_PATCHED_SDL3:BOOL=OFF",
                    )
                ),
                encoding="utf-8",
            )
            completed = mock.Mock(stdout="clang version 20.1.0\n", stderr="")
            with mock.patch("subprocess.run", return_value=completed):
                metadata = lg_benchmark.build_environment_metadata(build_dir)
        self.assertEqual(metadata["build_type"], "Release")
        self.assertEqual(metadata["cmake_generator"], "Ninja")
        self.assertEqual(metadata["compiler"], "clang++.exe")
        self.assertEqual(metadata["compiler_version"], "clang version 20.1.0")
        self.assertEqual(
            metadata["compile_time_options"],
            {"BUILD_TESTING": "ON", "LG_DUEL_SIMD_MODE": "portable"},
        )
        self.assertEqual(
            metadata["sdl_configuration"],
            {
                "LG_DUEL_FETCH_SDL3": "ON",
                "LG_DUEL_REQUIRE_SDL3": "OFF",
                "LG_DUEL_SDL3_GIT_TAG": "release-3.4.10",
                "LG_DUEL_SDL3_SOURCE_DIR": "C:/src/SDL",
                "LG_DUEL_USE_PATCHED_SDL3": "OFF",
            },
        )

    def test_environment_metadata_records_architecture(self) -> None:
        self.assertEqual(lg_benchmark.environment_metadata()["architecture"], lg_benchmark.platform.machine())

    def test_source_protocol_version_reads_header(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            header = root / "src" / "net"
            header.mkdir(parents=True)
            (header / "NetCodec.hpp").write_text(
                "inline constexpr std::uint16_t kProtocolVersion = 56;\n",
                encoding="utf-8",
            )
            self.assertEqual(lg_benchmark.source_protocol_version(root), 56)

    def test_source_fixed_tick_rate_reads_header(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            header = root / "src" / "shared"
            header.mkdir(parents=True)
            (header / "Constants.hpp").write_text(
                "inline constexpr float kFixedTickRate = 125.0F;\n",
                encoding="utf-8",
            )
            self.assertEqual(lg_benchmark.source_fixed_tick_rate(root), 125.0)

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

    def test_simulation_tick_csv_metrics_keep_their_scope(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "simulation-ticks.csv"
            path.write_text(
                "tick,render_frame,elapsed_seconds,simulation_ms,traces_ms\n"
                "1,1,0.008,0.2,0.05\n"
                "2,1,0.016,0.4,0.07\n",
                encoding="utf-8",
            )
            metrics = lg_benchmark._telemetry_metrics(
                path, prefix="simulation_tick_"
            )
        self.assertEqual(metrics["simulation_tick_simulation_ms"]["median"], 0.30000000000000004)
        self.assertEqual(metrics["simulation_tick_traces_ms"]["p95"], 0.07)
        self.assertNotIn("simulation_tick_tick", metrics)

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
            "environment": {"build_mode": "release", "renderer": "gpu", "protocol_version": 1},
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

    def test_build_mode_change_invalidates_comparison(self) -> None:
        baseline = self._artifact(Path("root"), "base", 10)
        result = self._artifact(Path("root"), "result", 10)
        result["environment"]["build_mode"] = "debug"
        self.assertIn("build_mode", lg_benchmark._comparison_mismatch(baseline, result))

    def test_benchmark_cli_defaults_to_release_with_debug_opt_in(self) -> None:
        parser = lg_benchmark.build_parser()
        release = parser.parse_args(["run", "--scenario", "eyetoeye-static-baseline"])
        low = parser.parse_args([
            "run", "--scenario", "eyetoeye-static-baseline",
            "--graphics-profile", "Low",
        ])
        debug = parser.parse_args([
            "sim-run", "--workload", "movement-collision", "--build-mode", "debug",
        ])
        self.assertEqual(release.build_mode, "release")
        self.assertEqual(low.graphics_profile, "Low")
        self.assertEqual(debug.build_mode, "debug")
        self.assertEqual(release.server_port, 28960)
        self.assertIsNone(release.control_port)
        self.assertIsNone(release.port)
        self.assertEqual(lg_benchmark.benchmark_build("release")[0], lg_benchmark.REPO_ROOT / "build" / "perf")

    def test_release_client_uses_perf_build_directory(self) -> None:
        with mock.patch.object(lg_benchmark, "ensure_client", return_value={}) as ensure:
            lg_benchmark._start_client(28960, 28961, 10, "release")
        ensure.assert_called_once_with(
            renderer="gpu", benchmark=True, server_port=28960,
            control_port=28961, timeout=10,
            build_dir=lg_benchmark.REPO_ROOT / "build" / "perf",
        )

    def test_benchmark_port_defaults_alias_and_validation(self) -> None:
        self.assertEqual(
            lg_benchmark.resolve_benchmark_ports(), (28960, 28961)
        )
        self.assertEqual(
            lg_benchmark.resolve_benchmark_ports(port=30061),
            (28960, 30061),
        )
        self.assertEqual(
            lg_benchmark.resolve_benchmark_ports(
                server_port=30060, control_port=30061, port=30061
            ),
            (30060, 30061),
        )
        for kwargs in (
            {"server_port": 0},
            {"control_port": 65536},
            {"server_port": 30060, "control_port": 30060},
            {"control_port": 30061, "port": 30062},
        ):
            with self.subTest(kwargs=kwargs), self.assertRaises(BenchmarkError):
                lg_benchmark.resolve_benchmark_ports(**kwargs)

    def test_benchmark_state_directory_is_derived_from_both_ports(self) -> None:
        with tempfile.TemporaryDirectory() as temporary, mock.patch.object(
            lg_benchmark, "BENCHMARK_STATE_ROOT", Path(temporary)
        ):
            first = lg_benchmark.benchmark_state_directory(30060, 30061)
            second = lg_benchmark.benchmark_state_directory(30160, 30161)
        self.assertEqual(first.name, "30060-30061")
        self.assertEqual(second.name, "30160-30161")
        self.assertNotEqual(first, second)

    def test_benchmark_launcher_scope_restores_globals_and_nests(self) -> None:
        original = {
            "STATE_DIR": lg_launch.STATE_DIR,
            "STATE_PATH": lg_launch.STATE_PATH,
            "LOCAL_VULKAN_CONFIGS": lg_launch.LOCAL_VULKAN_CONFIGS,
            "BENCHMARK_ROOT": lg_launch.BENCHMARK_ROOT,
        }
        with tempfile.TemporaryDirectory() as temporary, mock.patch.object(
            lg_benchmark, "BENCHMARK_STATE_ROOT", Path(temporary)
        ):
            with lg_benchmark.benchmark_launcher_scope(30060, 30061) as outer:
                self.assertEqual(lg_launch.STATE_DIR, outer)
                self.assertEqual(lg_launch.STATE_PATH, outer / "processes.json")
                self.assertEqual(
                    lg_launch.LOCAL_VULKAN_CONFIGS[0], outer / "vulkan.json"
                )
                with lg_benchmark.benchmark_launcher_scope(30160, 30161) as inner:
                    self.assertEqual(lg_launch.STATE_DIR, inner)
                self.assertEqual(lg_launch.STATE_DIR, outer)
        for key, value in original.items():
            self.assertEqual(getattr(lg_launch, key), value)

    def test_owned_run_cleans_scoped_session_and_records_ports(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            result_dir = root / "result"
            result_dir.mkdir()
            fake_result = {"result_directory": str(result_dir)}
            cleanup = {
                "stopped": ["client", "server"],
                "left_owned_running": False,
                "left_unowned_running": False,
                "state_preserved": False,
            }
            with mock.patch.object(
                lg_benchmark, "BENCHMARK_STATE_ROOT", root / "state"
            ), mock.patch.object(
                lg_benchmark, "_assert_benchmark_ports_available"
            ), mock.patch.object(
                lg_benchmark, "_start_client", return_value={"renderer": "SDL_GPU/vulkan"}
            ) as start, mock.patch.object(
                lg_benchmark, "_run_benchmark_with_session", return_value=fake_result
            ) as body, mock.patch.object(
                lg_launch, "stop_owned", return_value=cleanup
            ) as stop:
                result = lg_benchmark.run_benchmark(
                    "test-scenario",
                    server_port=30060,
                    control_port=30061,
                )
        start.assert_called_once_with(30060, 30061, 180.0, "release")
        self.assertEqual(body.call_args.kwargs["server_port"], 30060)
        self.assertEqual(body.call_args.kwargs["control_port"], 30061)
        self.assertEqual(
            body.call_args.kwargs["state_dir"].name, "30060-30061"
        )
        stop.assert_called_once_with()
        self.assertEqual(result["launcher_cleanup"], cleanup)

    def test_owned_run_preserves_main_error_and_notes_cleanup_failure(self) -> None:
        original = RuntimeError("benchmark body failed")
        with tempfile.TemporaryDirectory() as temporary, mock.patch.object(
            lg_benchmark, "BENCHMARK_STATE_ROOT", Path(temporary)
        ), mock.patch.object(
            lg_benchmark, "_assert_benchmark_ports_available"
        ), mock.patch.object(
            lg_benchmark, "_start_client", return_value={}
        ), mock.patch.object(
            lg_benchmark, "_run_benchmark_with_session", side_effect=original
        ), mock.patch.object(
            lg_launch, "stop_owned", side_effect=LaunchError("stop failed")
        ):
            with self.assertRaisesRegex(RuntimeError, "benchmark body failed") as caught:
                lg_benchmark.run_benchmark("test-scenario")
        self.assertIs(caught.exception, original)
        self.assertTrue(
            any("cleanup also failed" in note for note in original.__notes__)
        )

    def test_session_claim_blocks_second_caller_and_releases(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            state_dir = Path(temporary) / "28960-28961"
            with lg_benchmark.claim_benchmark_session(state_dir):
                with self.assertRaisesRegex(BenchmarkError, "already claimed"):
                    with lg_benchmark.claim_benchmark_session(state_dir):
                        self.fail("a second caller acquired the same session")
            with lg_benchmark.claim_benchmark_session(state_dir):
                self.assertTrue((state_dir / "benchmark-session.lock").exists())
            self.assertTrue((state_dir / "benchmark-session.lock").exists())

    def test_session_claim_releases_when_owner_process_exits(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            state_dir = Path(temporary) / "28960-28961"
            code = (
                "import os,sys\n"
                "from pathlib import Path\n"
                "import lg_benchmark\n"
                "claim=lg_benchmark.claim_benchmark_session(Path(sys.argv[1]))\n"
                "claim.__enter__()\n"
                "os._exit(0)\n"
            )
            completed = subprocess.run(
                [sys.executable, "-c", code, str(state_dir)],
                cwd=Path(__file__).parent,
                check=False,
                timeout=10,
            )
            self.assertEqual(completed.returncode, 0)
            with lg_benchmark.claim_benchmark_session(state_dir):
                self.assertTrue((state_dir / "benchmark-session.lock").exists())

    def test_cleanup_failure_marks_written_artifact_invalid(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            result_dir = root / "result"
            result_dir.mkdir()
            fake_result = {
                "result_directory": str(result_dir),
                "aggregate": {"valid": True, "stable": True},
            }
            cleanup = {
                "stopped": [],
                "left_owned_running": True,
                "left_unowned_running": False,
                "state_preserved": True,
            }
            with mock.patch.object(
                lg_benchmark, "BENCHMARK_STATE_ROOT", root / "state"
            ), mock.patch.object(
                lg_benchmark, "_assert_benchmark_ports_available"
            ), mock.patch.object(
                lg_benchmark, "_start_client", return_value={}
            ), mock.patch.object(
                lg_benchmark, "_run_benchmark_with_session",
                return_value=fake_result,
            ), mock.patch.object(
                lg_launch, "stop_owned", return_value=cleanup
            ), mock.patch.object(
                lg_benchmark, "render_report", return_value="invalid\n"
            ):
                with self.assertRaisesRegex(
                    BenchmarkError, "cleanup was incomplete"
                ):
                    lg_benchmark.run_benchmark("test-scenario")
            saved = json.loads(
                (result_dir / "aggregate.json").read_text(encoding="utf-8")
            )
            self.assertFalse(saved["aggregate"]["valid"])
            self.assertFalse(saved["aggregate"]["stable"])
            self.assertEqual(saved["launcher_cleanup"], cleanup)
            self.assertIn("cleanup was incomplete", saved["launcher_cleanup_error"])

    def test_external_run_never_scopes_or_cleans_launcher(self) -> None:
        expected = {"result_directory": "external"}
        with mock.patch.object(
            lg_benchmark, "_run_benchmark_with_session", return_value=expected
        ) as body, mock.patch.object(lg_launch, "stop_owned") as stop:
            result = lg_benchmark.run_benchmark(
                "test-scenario",
                server_port=30060,
                control_port=30061,
                start_client=False,
            )
        self.assertIs(result, expected)
        self.assertEqual(body.call_args.kwargs["status"], {})
        self.assertFalse(body.call_args.kwargs["start_client"])
        stop.assert_not_called()

    def test_busy_or_corrupt_benchmark_state_fails_before_start(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            state = root / "28960-28961"
            state.mkdir()
            state_path = state / "processes.json"
            state_path.write_text('{"phase":"ready"}', encoding="utf-8")
            with mock.patch.object(
                lg_benchmark, "BENCHMARK_STATE_ROOT", root
            ), mock.patch.object(lg_benchmark, "_start_client") as start:
                with self.assertRaisesRegex(BenchmarkError, "already in use"):
                    lg_benchmark.run_benchmark("test-scenario")
            start.assert_not_called()
            state_path.write_text("{broken", encoding="utf-8")
            with mock.patch.object(
                lg_benchmark, "BENCHMARK_STATE_ROOT", root
            ), mock.patch.object(lg_benchmark, "_start_client") as start:
                with self.assertRaisesRegex(BenchmarkError, "corrupt"):
                    lg_benchmark.run_benchmark("test-scenario")
            start.assert_not_called()

    def test_busy_control_port_fails_without_connecting(self) -> None:
        busy = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        busy.bind(("127.0.0.1", 0))
        port = busy.getsockname()[1]
        busy.listen(1)
        try:
            with self.assertRaisesRegex(BenchmarkError, "busy or unavailable"):
                lg_benchmark._assert_benchmark_ports_available(28960, port)
        finally:
            busy.close()

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
                        "validity": {"map": True, "completed": True, "frame_count": True},
                        "effective_cvars": {
                            "r_antialiasing": "1", "r_sun_shadows": "2",
                            "r_contact_shadows": "1", "r_material_quality": "1",
                            "r_player_rim": "1", "r_atmosphere_grade": "2",
                            "r_bloom": "1", "r_render_scale": "1.000000",
                        }}

            with mock.patch.object(lg_benchmark, "SCENARIO_ROOT", scenarios), mock.patch.object(lg_benchmark, "RESULT_ROOT", results):
                result = lg_benchmark.run_benchmark("test-scenario", repetitions=2, request_sender=sender, start_client=False)
            self.assertEqual(result["schema_version"], 1)
            self.assertEqual(result["environment"]["build_mode"], "release")
            self.assertTrue((Path(result["result_directory"]) / "aggregate.json").is_file())
            self.assertEqual(len(calls), 2)
            self.assertEqual(calls[0][0], "run_benchmark")
            self.assertEqual(calls[0][1]["port"], 28961)
            self.assertEqual(
                calls[0][1]["scenario"],
                lg_benchmark.validate_scenario(value),
            )
            self.assertEqual(calls[0][1]["scenario"]["cvars"]["s_volume"], 0)
            self.assertRegex(calls[0][1]["scenario_hash"], r"^[0-9a-f]{64}$")
            self.assertIn("run_group", calls[0][1])
            self.assertIn("graphics_contract", result["settings"])
            self.assertEqual(result["settings"]["presentation_cvars"]["s_volume"], 0)
            self.assertEqual(result["settings"]["graphics_contract"]["profile"], "Default")
            self.assertEqual(result["settings"]["graphics_contract"]["render_scale"], "1.000000")
            self.assertEqual(result["environment"]["benchmark_server_port"], 28960)
            self.assertEqual(result["environment"]["benchmark_control_port"], 28961)
            self.assertTrue(
                result["environment"]["benchmark_state_directory"].endswith(
                    "28960-28961"
                )
            )

    def test_optional_render_pass_diagnostics_are_preserved(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            run_dir = root / "test" / "run-1"
            run_dir.mkdir(parents=True)
            native = run_dir / "result.json"
            native.write_text(json.dumps({
                "summary": {"median_ms": 2.0},
                "render_pass_diagnostics": {"world": {"draws": 4, "triangles": 120}},
            }), encoding="utf-8")
            with mock.patch.object(lg_benchmark, "RESULT_ROOT", root):
                normalized = lg_benchmark._normalize_native_result(
                    {"summary": {"median_ms": 2.0}, "result_path": str(native)}, run_dir, "run-1",
                )
        self.assertEqual(normalized["render_pass_diagnostics"]["world"]["draws"], 4)

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
                lg_benchmark._start_client(28960, 28961, 1)


if __name__ == "__main__":
    unittest.main()
