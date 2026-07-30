from __future__ import annotations

import copy
import json
import shutil
import subprocess
import tempfile
import unittest
from argparse import Namespace
from pathlib import Path
from unittest import mock

import lg_benchmark
import lg_compare_benchmarks as compare
import lg_launch
import lg_performance_policy


class FakeGit:
    def __init__(self, *, dirty: bool = False, fail_build: bool = False) -> None:
        self.dirty = dirty
        self.fail_build = fail_build
        self.registered: set[Path] = set()
        self.commands: list[list[str]] = []

    def __call__(self, command: list[str], **_: object) -> subprocess.CompletedProcess[str]:
        self.commands.append(list(command))
        if command[:3] == ["git", "rev-parse", "--verify"]:
            ref = command[3]
            commit = "a" * 40 if ref.startswith("base") else "b" * 40
            return subprocess.CompletedProcess(command, 0, commit + "\n", "")
        if command[:3] == ["git", "status", "--porcelain"]:
            return subprocess.CompletedProcess(
                command, 0, " M tracked.cpp\n" if self.dirty else "", ""
            )
        if command[:4] == ["git", "worktree", "add", "--detach"]:
            path = Path(command[4]).resolve()
            path.mkdir(parents=True)
            header = path / "src" / "net"
            header.mkdir(parents=True)
            (header / "NetCodec.hpp").write_text(
                "inline constexpr auto kMaxUdpApplicationDatagramBytes = 1200;\n",
                encoding="utf-8",
            )
            maps = path / "maps"
            maps.mkdir()
            (maps / "overkill_import.map").write_text(
                "// shared benchmark map\n", encoding="utf-8"
            )
            self.registered.add(path)
            return subprocess.CompletedProcess(command, 0, "", "")
        if command[:3] == ["git", "worktree", "list"]:
            foreign = Path(tempfile.gettempdir()) / "foreign-lg-worktree"
            lines = [f"worktree {foreign.resolve()}\nHEAD {'f' * 40}\n"]
            lines += [
                f"worktree {path}\nHEAD {'a' * 40}\n"
                for path in sorted(self.registered, key=str)
            ]
            return subprocess.CompletedProcess(command, 0, "\n".join(lines), "")
        if command[:4] == ["git", "worktree", "remove", "--force"]:
            path = Path(command[4]).resolve()
            self.registered.discard(path)
            if path.exists():
                shutil.rmtree(path)
            return subprocess.CompletedProcess(command, 0, "", "")
        if command[:2] == ["cmake", "-S"]:
            build = Path(command[command.index("-B") + 1])
            build.mkdir(parents=True)
            return subprocess.CompletedProcess(command, 0, "configured", "")
        if command[:3] == ["cmake", "--build", command[2]]:
            return subprocess.CompletedProcess(
                command,
                1 if self.fail_build else 0,
                "",
                "build failed" if self.fail_build else "",
            )
        return subprocess.CompletedProcess(command, 0, "", "")


class CompareBenchmarkTests(unittest.TestCase):
    def setUp(self) -> None:
        self.policy = compare.DEFAULT_POLICY

    def args(self, output: Path, **changes: object) -> Namespace:
        values: dict[str, object] = {
            "baseline": None,
            "candidate": None,
            "baseline_results": None,
            "candidate_results": None,
            "suite": None,
            "repetitions": None,
            "profile": "pr_headless",
            "policy": str(self.policy),
            "output": str(output),
            "not_comparable_exit_zero": False,
        }
        values.update(changes)
        return Namespace(**values)

    def test_parser_accepts_competitive_gpu_profile_and_suite(self) -> None:
        parser = compare.build_parser()
        args = parser.parse_args([
            "--baseline", "base", "--candidate", "HEAD",
            "--suite", "trusted_gpu_competitive", "--repetitions", "5",
            "--profile", "trusted_gpu_competitive", "--output", "out",
        ])
        self.assertEqual(args.suite, "trusted_gpu_competitive")
        self.assertEqual(args.profile, "trusted_gpu_competitive")

    def test_benchmark_scope_uses_result_owned_pair_state_and_restores(self) -> None:
        original_state = lg_launch.STATE_DIR
        original_root = lg_benchmark.BENCHMARK_STATE_ROOT
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "source"
            build = root / "build"
            results = root / "results"
            with compare._benchmark_scope(source, build, results):
                self.assertEqual(
                    lg_benchmark.BENCHMARK_STATE_ROOT,
                    results / "_launcher",
                )
                with lg_benchmark.benchmark_launcher_scope(
                    *compare.GPU_PORTS["baseline"]
                ) as state:
                    self.assertEqual(
                        state, results / "_launcher" / "29060-29061"
                    )
                    self.assertEqual(lg_launch.STATE_DIR, state)
                self.assertEqual(lg_launch.STATE_DIR, results / "_launcher")
        self.assertEqual(lg_launch.STATE_DIR, original_state)
        self.assertEqual(lg_benchmark.BENCHMARK_STATE_ROOT, original_root)

    def aggregate(
        self,
        scenario: str,
        value: float = 1.0,
        *,
        commit: str = "a" * 40,
        scenario_hash: str | None = None,
    ) -> dict:
        metric = (
            "movement_collision_us_per_operation"
            if scenario == "sim-movement-collision"
            else "projectile_segment_us_per_trace"
        )
        statistic = "run_values" if scenario == "sim-movement-collision" else "run_p95_values"
        return {
            "schema_version": 1,
            "scenario": {
                "name": scenario,
                "map": "overkill_import",
                "schema_version": 1,
                "benchmark_version": 1,
            },
            "scenario_hash": scenario_hash or f"hash-{scenario}",
            "map_content_hash": "map-hash",
            "git": {"commit": commit},
            "environment": {
                "benchmark_version": 1,
                "cpu": "test-cpu",
                "os": "test-os",
                "architecture": "x64",
                "compiler": "clang",
                "compiler_version": "clang version 20.1.0",
                "build_type": "Release",
                "cmake_generator": "Ninja",
                "compile_time_options": {
                    "BUILD_TESTING": "ON",
                },
                "sdl_configuration": {
                    "LG_DUEL_FETCH_SDL3": "ON",
                    "LG_DUEL_REQUIRE_SDL3": "OFF",
                    "LG_DUEL_SDL3_GIT_TAG": "release-3.4.10",
                    "LG_DUEL_SDL3_SOURCE_DIR": "",
                    "LG_DUEL_USE_PATCHED_SDL3": "OFF",
                },
                "logical_cores": 8,
                "build_mode": "release",
                "protocol_version": 1,
                "server_tick_rate": 125,
            },
            "settings": {
                "backend": "headless-shared-simulation",
                "map": "overkill_import",
                "repetitions": 5,
                "warmup_batches": 5,
                "measured_batches": 40,
                "operations_per_batch": 256,
                "collision_query_mode": "indexed-when-available",
            },
            "aggregate": {
                "valid": True,
                "run_count": 5,
                "metrics": {metric: {statistic: [value] * 5}},
            },
            "max_app_datagram_bytes": 1200,
            "configured_datagram_ceiling_bytes": 1200,
            "snapshot_encode_failures": 0,
        }

    def result_dir(
        self,
        parent: Path,
        name: str,
        *,
        value: float = 1.0,
        commit: str = "a" * 40,
        changed_hash: bool = False,
    ) -> Path:
        root = parent / name
        for scenario in ("sim-movement-collision", "sim-trace-projectile"):
            target = root / scenario
            target.mkdir(parents=True)
            document = self.aggregate(
                scenario,
                value,
                commit=commit,
                scenario_hash=(
                    f"changed-{scenario}" if changed_hash else None
                ),
            )
            (target / "aggregate.json").write_text(
                json.dumps(document), encoding="utf-8"
            )
            (target / "stdout.log").write_text("raw", encoding="utf-8")
        return root

    def change_environment(
        self, root: Path, field: str, value: object
    ) -> None:
        for path in root.rglob("aggregate.json"):
            document = json.loads(path.read_text(encoding="utf-8"))
            document["environment"][field] = value
            path.write_text(json.dumps(document), encoding="utf-8")

    def test_stored_mode_pass_copies_raw_and_writes_matching_reports(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            baseline = self.result_dir(root, "base")
            candidate = self.result_dir(root, "candidate", commit="b" * 40)
            output = root / "output"
            code = compare.execute(
                self.args(
                    output,
                    baseline_results=str(baseline),
                    candidate_results=str(candidate),
                )
            )
            self.assertEqual(code, 0)
            document = json.loads(
                (output / "comparison.json").read_text(encoding="utf-8")
            )
            report = (output / "report.md").read_text(encoding="utf-8")
            self.assertEqual(document["status"], "PASS")
            self.assertIn("**Status: PASS**", report)
            self.assertEqual(
                document["comparison_metadata"]["baseline_commit"], "a" * 40
            )
            self.assertTrue(
                (output / "raw" / "baseline" / "sim-movement-collision"
                 / "stdout.log").is_file()
            )
            manifest = json.loads(
                (output / "manifest.json").read_text(encoding="utf-8")
            )
            paths = [item["path"] for item in manifest["artifacts"]]
            self.assertEqual(paths, sorted(paths))
            self.assertTrue(all(not Path(path).is_absolute() for path in paths))
            self.assertTrue(
                all("sha256" in item for item in manifest["artifacts"])
            )

    def test_pr_headless_ignores_sdl_only_build_differences(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            baseline = self.result_dir(root, "base")
            candidate = self.result_dir(root, "candidate", commit="b" * 40)
            self.change_environment(
                candidate,
                "sdl_configuration",
                {
                    "LG_DUEL_FETCH_SDL3": "ON",
                    "LG_DUEL_REQUIRE_SDL3": "OFF",
                    "LG_DUEL_SDL3_GIT_TAG": "8e37db5e",
                    "LG_DUEL_SDL3_SOURCE_DIR": "",
                    "LG_DUEL_USE_PATCHED_SDL3": "OFF",
                },
            )
            output = root / "output"
            code = compare.execute(
                self.args(
                    output,
                    baseline_results=str(baseline),
                    candidate_results=str(candidate),
                )
            )
            document = json.loads(
                (output / "comparison.json").read_text(encoding="utf-8")
            )
            self.assertEqual(code, 0)
            self.assertEqual(document["status"], "PASS")
            self.assertTrue(document["comparable"])

    def test_pr_headless_rejects_simulation_build_differences(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            baseline = self.result_dir(root, "base")
            candidate = self.result_dir(root, "candidate", commit="b" * 40)
            self.change_environment(
                candidate,
                "compile_time_options",
                {
                    "BUILD_TESTING": "ON",
                    "LG_DUEL_SIMULATION_FAST_MATH": "ON",
                },
            )
            output = root / "output"
            code = compare.execute(
                self.args(
                    output,
                    baseline_results=str(baseline),
                    candidate_results=str(candidate),
                )
            )
            document = json.loads(
                (output / "comparison.json").read_text(encoding="utf-8")
            )
            self.assertEqual(code, 1)
            self.assertEqual(document["status"], "NOT_COMPARABLE")
            self.assertFalse(document["comparable"])

    def test_stored_output_is_deterministic_and_report_status_agrees(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            baseline = self.result_dir(root, "base")
            candidate = self.result_dir(root, "candidate", commit="b" * 40)
            outputs = [root / "one", root / "two"]
            for output in outputs:
                compare.execute(
                    self.args(
                        output,
                        baseline_results=str(baseline),
                        candidate_results=str(candidate),
                    )
                )
            first = (outputs[0] / "comparison.json").read_text(encoding="utf-8")
            second = (outputs[1] / "comparison.json").read_text(encoding="utf-8")
            self.assertEqual(first, second)
            status = json.loads(first)["status"]
            self.assertIn(
                f"**Status: {status}**",
                (outputs[0] / "report.md").read_text(encoding="utf-8"),
            )

    def test_stored_mode_rejects_mixed_commits(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            baseline = self.result_dir(root, "base")
            candidate = self.result_dir(root, "candidate", commit="b" * 40)
            aggregate = candidate / "sim-trace-projectile" / "aggregate.json"
            document = json.loads(aggregate.read_text(encoding="utf-8"))
            document["git"]["commit"] = "c" * 40
            aggregate.write_text(json.dumps(document), encoding="utf-8")
            output = root / "output"
            with self.assertRaisesRegex(
                lg_performance_policy.PerformancePolicyError,
                "different commits",
            ):
                compare.execute(
                    self.args(
                        output,
                        baseline_results=str(baseline),
                        candidate_results=str(candidate),
                    )
                )
            self.assertFalse(output.exists())

    def test_directory_errors_and_preexisting_output_leave_no_new_data(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            baseline = self.result_dir(root, "base")
            output = root / "output"
            with self.assertRaisesRegex(
                lg_performance_policy.PerformancePolicyError,
                "does not exist",
            ):
                compare.execute(
                    self.args(
                        output,
                        baseline_results=str(baseline),
                        candidate_results=str(root / "missing"),
                    )
                )
            self.assertFalse(output.exists())
            candidate = self.result_dir(root, "candidate")
            output.mkdir()
            marker = output / "owned-by-user"
            marker.write_text("keep", encoding="utf-8")
            with self.assertRaisesRegex(compare.CompareError, "already exists"):
                compare.execute(
                    self.args(
                        output,
                        baseline_results=str(baseline),
                        candidate_results=str(candidate),
                    )
                )
            self.assertEqual(marker.read_text(encoding="utf-8"), "keep")

    def test_ref_validation_and_dirty_rejection_happen_before_output(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            output = root / "output"
            runner = FakeGit()
            with self.assertRaisesRegex(compare.CompareError, "safe git revision"):
                compare.execute(
                    self.args(
                        output,
                        baseline="-bad",
                        candidate="HEAD",
                        suite="pr_headless",
                        repetitions=5,
                    ),
                    runner=runner,
                )
            self.assertEqual(runner.commands, [])
            dirty = FakeGit(dirty=True)
            with self.assertRaisesRegex(compare.CompareError, "clean active"):
                compare.execute(
                    self.args(
                        output,
                        baseline="base",
                        candidate="HEAD",
                        suite="pr_headless",
                        repetitions=5,
                    ),
                    runner=dirty,
                )
            self.assertFalse(output.exists())
            self.assertTrue(
                any(command[:3] == ["git", "rev-parse", "--verify"]
                    for command in dirty.commands)
            )

    def test_revision_build_failure_keeps_partial_evidence_and_cleans_owned_only(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            output = root / "output"
            runner = FakeGit(fail_build=True)
            with self.assertRaisesRegex(compare.CompareError, "baseline-build"):
                compare.execute(
                    self.args(
                        output,
                        baseline="base",
                        candidate="HEAD",
                        suite="pr_headless",
                        repetitions=5,
                    ),
                    runner=runner,
                )
            manifest = json.loads(
                (output / "manifest.json").read_text(encoding="utf-8")
            )
            self.assertEqual(manifest["status"], "ERROR")
            self.assertIn("baseline-build", manifest["error"])
            self.assertTrue((output / "logs" / "baseline-build.stderr.log").is_file())
            removed = [
                command[-1]
                for command in runner.commands
                if command[:4] == ["git", "worktree", "remove", "--force"]
            ]
            self.assertEqual(len(removed), 2)
            owned_names = {
                Path(item["path"]).name
                for item in manifest["cleanup"]
                if item["path"].startswith("temp/worktrees/")
            }
            self.assertEqual({Path(item).name for item in removed}, owned_names)
            self.assertFalse(any("foreign-lg-worktree" in item for item in removed))

    def test_revision_run_failure_also_cleans_worktrees(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            output = root / "output"
            runner = FakeGit()

            def protocol(*_: object, **__: object) -> dict:
                return {
                    "max_app_datagram_bytes": 1200,
                    "configured_datagram_ceiling_bytes": 1200,
                    "snapshot_encode_failures": 0,
                }

            with mock.patch.object(compare, "_configure_and_build"), \
                    mock.patch.object(compare, "_run_protocol_test", side_effect=protocol), \
                    mock.patch.object(
                        lg_benchmark,
                        "run_simulation_benchmark",
                        side_effect=lg_benchmark.BenchmarkError("run failed"),
                    ):
                with self.assertRaisesRegex(lg_benchmark.BenchmarkError, "run failed"):
                    compare.execute(
                        self.args(
                            output,
                            baseline="base",
                            candidate="HEAD",
                            suite="pr_headless",
                            repetitions=5,
                        ),
                        runner=runner,
                    )
            manifest = json.loads(
                (output / "manifest.json").read_text(encoding="utf-8")
            )
            self.assertEqual(manifest["status"], "ERROR")
            self.assertEqual(
                [item["status"] for item in manifest["cleanup"]],
                ["removed", "removed", "absent", "absent"],
            )

    def test_revision_success_cleanup_and_protocol_evidence(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            output = root / "output"
            runner = FakeGit()

            def fake_build(
                _side: str,
                _source: Path,
                build: Path,
                _suite: str,
                *_: object,
            ) -> None:
                build.mkdir(parents=True)

            def protocol(
                side: str, *_: object, **__: object
            ) -> dict:
                return {
                    "max_app_datagram_bytes": 1200,
                    "max_app_datagram_bytes_source": "source ceiling",
                    "configured_datagram_ceiling_bytes": 1200,
                    "snapshot_encode_failures": 0,
                    "protocol_test_return_code": 0,
                    "protocol_test_log": f"logs/{side}-protocol-test.stdout.log",
                }

            def benchmark(workload: str, **_: object) -> dict:
                map_directories.append(Path(_["map_directory"]).resolve())
                scenario = f"sim-{workload}"
                result = self.aggregate(
                    scenario,
                    commit=(
                        "a" * 40
                        if "baseline" in str(lg_benchmark.RESULT_ROOT)
                        else "b" * 40
                    ),
                )
                destination = lg_benchmark.RESULT_ROOT / scenario / "run"
                destination.mkdir(parents=True)
                result["result_directory"] = str(destination)
                (destination / "aggregate.json").write_text(
                    json.dumps(result), encoding="utf-8"
                )
                return result

            map_directories: list[Path] = []
            with mock.patch.object(compare, "_configure_and_build", side_effect=fake_build), \
                    mock.patch.object(compare, "_run_protocol_test", side_effect=protocol), \
                    mock.patch.object(
                        lg_benchmark,
                        "run_simulation_benchmark",
                        side_effect=benchmark,
                    ):
                code = compare.execute(
                    self.args(
                        output,
                        baseline="base",
                        candidate="HEAD",
                        suite="pr_headless",
                        repetitions=5,
                    ),
                    runner=runner,
                )
            self.assertEqual(code, 0)
            self.assertEqual(len(map_directories), 4)
            self.assertEqual(len(set(map_directories)), 1)
            self.assertIn("candidate-", map_directories[0].parent.name)
            aggregate = json.loads(
                next((output / "raw" / "candidate").rglob("aggregate.json")).read_text(
                    encoding="utf-8"
                )
            )
            self.assertEqual(aggregate["snapshot_encode_failures"], 0)
            manifest = json.loads(
                (output / "manifest.json").read_text(encoding="utf-8")
            )
            self.assertEqual(
                [item["status"] for item in manifest["cleanup"]],
                ["removed", "removed", "removed", "removed"],
            )
            self.assertEqual(
                manifest["shared_inputs"]["headless_maps"],
                {
                    "source": "candidate",
                    "commit": "b" * 40,
                    "path": "maps",
                },
            )
            self.assertFalse((output / "temp" / "builds").exists())
            self.assertFalse((output / "temp").exists())
            self.assertTrue(
                all(
                    not item["path"].startswith("temp/")
                    for item in manifest["artifacts"]
                )
            )

    def test_failed_worktree_inspection_is_not_treated_as_empty(self) -> None:
        def runner(
            command: list[str], **_: object
        ) -> subprocess.CompletedProcess[str]:
            return subprocess.CompletedProcess(command, 1, "", "git failed")

        with self.assertRaisesRegex(compare.CompareError, "cannot inspect"):
            compare._registered_worktrees(runner)

    def test_exit_codes_for_fail_not_comparable_and_tool_error(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            baseline = self.result_dir(root, "base")
            failed = self.result_dir(root, "failed", value=2.0, commit="b" * 40)
            code = compare.main(
                [
                    "--baseline-results", str(baseline),
                    "--candidate-results", str(failed),
                    "--profile", "pr_headless",
                    "--output", str(root / "failed-output"),
                ]
            )
            self.assertEqual(code, 1)
            code = compare.main(
                [
                    "--baseline-results", str(baseline),
                    "--candidate-results", str(failed),
                    "--profile", "pr_headless",
                    "--output", str(root / "failed-output-allowed"),
                    "--not-comparable-exit-zero",
                ]
            )
            self.assertEqual(code, 1)
            changed = self.result_dir(
                root, "changed", commit="b" * 40, changed_hash=True
            )
            code = compare.main(
                [
                    "--baseline-results", str(baseline),
                    "--candidate-results", str(changed),
                    "--profile", "pr_headless",
                    "--output", str(root / "changed-output"),
                ]
            )
            self.assertEqual(code, 1)
            code = compare.main(
                [
                    "--baseline-results", str(baseline),
                    "--candidate-results", str(changed),
                    "--profile", "pr_headless",
                    "--output", str(root / "changed-output-allowed"),
                    "--not-comparable-exit-zero",
                ]
            )
            self.assertEqual(code, 0)
            for name in ("comparison.json", "manifest.json"):
                document = json.loads(
                    (root / "changed-output-allowed" / name).read_text(
                        encoding="utf-8"
                    )
                )
                self.assertEqual(document["status"], "NOT_COMPARABLE")
            code = compare.main(
                [
                    "--baseline-results", str(root / "missing"),
                    "--candidate-results", str(changed),
                    "--profile", "pr_headless",
                    "--output", str(root / "error-output"),
                    "--not-comparable-exit-zero",
                ]
            )
            self.assertEqual(code, 2)

    def test_command_mode_parsing_and_repetition_limits(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "out"
            with self.assertRaisesRegex(compare.CompareError, "exclusive"):
                compare._validate_mode(
                    self.args(
                        output,
                        baseline="base",
                        candidate="HEAD",
                        baseline_results="a",
                        candidate_results="b",
                        suite="pr_headless",
                        repetitions=5,
                    )
                )
            with self.assertRaisesRegex(compare.CompareError, "requires --suite"):
                compare._validate_mode(
                    self.args(output, baseline="base", candidate="HEAD")
                )
            runner = FakeGit()
            with self.assertRaisesRegex(compare.CompareError, "required_repetitions"):
                compare.execute(
                    self.args(
                        output,
                        baseline="base",
                        candidate="HEAD",
                        suite="pr_headless",
                        repetitions=4,
                    ),
                    runner=runner,
                )


if __name__ == "__main__":
    unittest.main()
