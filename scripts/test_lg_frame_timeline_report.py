from __future__ import annotations

import csv
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

import lg_frame_timeline_report as report


class FrameTimelineReportTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def write_json(self, name: str, values: list[float], *, subsystem: str = "simulation") -> Path:
        path = self.root / name
        frames = []
        elapsed = 0.0
        for index, value in enumerate(values):
            frames.append({
                "frame": index,
                "elapsed_time_seconds": elapsed,
                "total_cpu_ms": value,
                "total_gpu_ms": None,
                "gpu_timing_available": False,
                "cpu_subsystems_ms": {subsystem: value * 0.6, "ui": value * 0.1},
                "gpu_subsystems_ms": {},
                "workload": {"world_draws": 10 + index},
                "event_markers": [{"type": "marker"}] if value > 20 else [],
            })
            elapsed += value / 1000.0
        path.write_text(json.dumps({
            "format": "lg-duel-frame-timeline",
            "schema_version": 1,
            "gpu_execution_timing_available": False,
            "metadata": {
                "scenario_hash": "synthetic-scenario",
                "renderer": "synthetic-renderer",
                "actual_resolution": [1280, 720],
            },
            "frames": frames,
        }), encoding="utf-8")
        return path

    def test_detects_isolated_burst_periodic_and_subsystem_correlation(self) -> None:
        isolated_path = self.write_json("isolated.json", [10.0] * 20 + [30.0] + [10.0] * 20)
        frames, _, _ = report.load_input(isolated_path)
        analysis = report.analyze(frames)
        self.assertEqual([20], [item["frame"] for item in analysis["patterns"]["isolated_spikes"]])
        self.assertEqual("simulation", analysis["spikes"][0]["likely_cpu_subsystem"]["name"])
        self.assertEqual("correlation", analysis["spikes"][0]["likely_cpu_subsystem"]["kind"])
        self.assertEqual("marker", analysis["spikes"][0]["events"][0]["name"])
        self.assertIsNotNone(analysis["spikes"][0]["elapsed_seconds"])

        burst_path = self.write_json("burst.json", [10.0] * 10 + [30.0, 31.0, 10.0, 32.0] + [10.0] * 10)
        burst_frames, _, _ = report.load_input(burst_path)
        self.assertEqual(1, len(report.analyze(burst_frames)["patterns"]["bursts"]))

        values = [10.0] * 50
        for index in (5, 15, 25, 35, 45):
            values[index] = 30.0
        periodic_path = self.write_json("periodic.json", values)
        periodic_frames, _, _ = report.load_input(periodic_path)
        periodic = report.analyze(periodic_frames)["patterns"]["periodic_spikes"]
        self.assertEqual(10.0, periodic[0]["mean_interval_frames"])
        self.assertGreater(periodic[0]["confidence"], 0)

    def test_detects_sustained_and_sawtooth(self) -> None:
        sustained = self.write_json("sustained.json", [10.0] * 20 + [16.0] * 10 + [10.0] * 20)
        frames, _, _ = report.load_input(sustained)
        self.assertEqual(10, report.analyze(frames)["patterns"]["sustained_regressions"][0]["frame_count"])

        saw = self.write_json("saw.json", [8.0, 12.0] * 20)
        saw_frames, _, _ = report.load_input(saw)
        item = report.analyze(saw_frames)["patterns"]["sawtooth_alternating"][0]
        self.assertLessEqual(item["lag1_correlation"], -0.5)

    def test_rejects_unknown_schema_and_marks_short_patterns_unavailable(self) -> None:
        path = self.write_json("short.json", [10.0, 11.0, 10.0])
        frames, _, _ = report.load_input(path)
        status = report.analyze(frames)["pattern_status"]
        self.assertEqual("unavailable", status["periodic_spikes"]["status"])
        self.assertEqual("unavailable", status["sustained_regressions"]["status"])
        self.assertEqual("unavailable", status["sawtooth_alternating"]["status"])

        malformed = json.loads(path.read_text(encoding="utf-8"))
        malformed["schema_version"] = 2
        path.write_text(json.dumps(malformed), encoding="utf-8")
        with self.assertRaisesRegex(report.ReportError, "unsupported timeline schema"):
            report.load_input(path)
        malformed["schema_version"] = 1
        malformed["format"] = "other"
        path.write_text(json.dumps(malformed), encoding="utf-8")
        with self.assertRaisesRegex(report.ReportError, "unsupported timeline format"):
            report.load_input(path)

    def test_legacy_csv_fallback_and_artifacts_are_deterministic(self) -> None:
        run = self.root / "run"
        run.mkdir()
        with (run / "telemetry.csv").open("w", newline="", encoding="utf-8") as stream:
            writer = csv.DictWriter(stream, fieldnames=[
                "frame", "elapsed_seconds", "frame_ms", "simulation_ms",
                "ui_ms", "world_draws",
            ])
            writer.writeheader()
            for index, value in enumerate([10.0] * 12 + [30.0] + [10.0] * 12):
                writer.writerow({
                    "frame": index, "elapsed_seconds": index / 60,
                    "frame_ms": value, "simulation_ms": value * 0.7,
                    "ui_ms": 0.5, "world_draws": 4,
                })
        output = self.root / "output"
        first = report.write_report(run, output)
        first_bytes = (output / "timeline-analysis.json").read_bytes()
        second = report.write_report(run, output)
        self.assertEqual(first, second)
        self.assertEqual(first_bytes, (output / "timeline-analysis.json").read_bytes())
        self.assertEqual("legacy-telemetry-csv", first["source"]["format"])
        self.assertTrue((output / "frame-timeline.html").read_text(encoding="utf-8").startswith("<!doctype html>"))
        self.assertIn("<svg", (output / "frame-timeline.svg").read_text(encoding="utf-8"))
        self.assertFalse((output / "frame-timeline.html").read_text(encoding="utf-8").count("https://"))

    def test_chart_markup_has_separate_background_bars_axes_and_legend(self) -> None:
        path = self.write_json("chart.json", [9.0, 11.0, 30.0, 9.0, 11.0] * 4)
        frames, _, _ = report.load_input(path)
        analysis = report.analyze(frames)
        histogram = report._histogram(frames, label="SYNTHETIC DEMONSTRATION ONLY")
        timeline = report._timeline_svg(
            frames, analysis, interactive=True, label="SYNTHETIC DEMONSTRATION ONLY"
        )
        self.assertIn('class="plot-bg"', histogram)
        self.assertIn('class="bar"', histogram)
        self.assertIn('class="grid"', histogram)
        self.assertIn("Frame count per time bin", histogram)
        self.assertNotIn("rect{fill", histogram)
        self.assertIn("CPU frame time", timeline)
        self.assertIn("spike &gt;", timeline)
        self.assertIn('class="grid"', timeline)
        self.assertIn("SYNTHETIC DEMONSTRATION ONLY", timeline)
        page = report.render_html(frames, analysis, None)
        self.assertIn("original=[0,0,1200,420]", page)

    def test_native_gpu_totals_and_stages_reach_analysis_and_html(self) -> None:
        path = self.write_json("gpu.json", [4.0, 5.0, 6.0])
        value = json.loads(path.read_text(encoding="utf-8"))
        value["gpu_execution_timing_available"] = True
        for index, frame in enumerate(value["frames"]):
            frame["total_gpu_ms"] = 2.0 + index
            frame["gpu_timing_available"] = True
            frame["gpu_subsystems_ms"] = {
                "main_scene": 1.0 + index * 0.1,
                "scene_composite": 0.2,
            }
            frame["gpu_subsystem_states"] = {
                "main_scene": "available",
                "scene_composite": "available",
                "bloom": "not_applicable",
            }
        path.write_text(json.dumps(value), encoding="utf-8")
        frames, meta, _ = report.load_input(path)
        analysis = report.analyze(frames)
        self.assertTrue(meta["gpu_execution_timing_available"])
        self.assertEqual(3, analysis["summary"]["gpu_sample_count"])
        self.assertEqual(3.0, analysis["summary"]["median_gpu_ms"])
        self.assertEqual(3, analysis["gpu_stages"]["main_scene"]["sample_count"])
        self.assertEqual(100.0, analysis["gpu_stages"]["main_scene"]["coverage_percent"])
        self.assertEqual(0, analysis["gpu_stages"]["bloom"]["applicable_count"])
        page = report.render_html(frames, analysis, None)
        self.assertIn("GPU command buffer", page)
        self.assertIn("GPU stage timing", page)
        self.assertIn("main_scene", page)

    def test_csv_gpu_stage_values_pair_with_their_state_columns(self) -> None:
        path = self.root / "telemetry.csv"
        with path.open("w", newline="", encoding="utf-8") as stream:
            writer = csv.DictWriter(stream, fieldnames=[
                "frame", "elapsed_seconds", "frame_ms",
                "gpu_primary_command_buffer_ms",
                "main_scene_gpu_ms", "main_scene_gpu_state",
                "bloom_gpu_ms", "bloom_gpu_state",
            ])
            writer.writeheader()
            for index in range(2):
                writer.writerow({
                    "frame": index,
                    "elapsed_seconds": index / 60,
                    "frame_ms": 2.0,
                    "gpu_primary_command_buffer_ms": 0.8,
                    "main_scene_gpu_ms": 0.5,
                    "main_scene_gpu_state": "available",
                    "bloom_gpu_ms": "",
                    "bloom_gpu_state": "not_applicable",
                })
        frames, _, _ = report.load_input(path)
        self.assertEqual({"main_scene": 0.5}, frames[0]["gpu_subsystems_ms"])
        self.assertEqual(
            {"main_scene": "available", "bloom": "not_applicable"},
            frames[0]["gpu_subsystem_states"],
        )
        analysis = report.analyze(frames)
        self.assertNotIn("main_scene_gpu", analysis["gpu_stages"])
        self.assertEqual(2, analysis["gpu_stages"]["main_scene"]["sample_count"])
        self.assertEqual(2, analysis["gpu_stages"]["main_scene"]["applicable_count"])
        self.assertEqual(0, analysis["gpu_stages"]["bloom"]["applicable_count"])

    def test_cli_with_baseline_writes_comparison(self) -> None:
        candidate = self.write_json("candidate.json", [12.0] * 20)
        baseline = self.write_json("baseline.json", [10.0] * 20)
        output = self.root / "cli-output"
        completed = subprocess.run(
            [
                sys.executable,
                str(Path(report.__file__).resolve()),
                str(candidate),
                "--baseline", str(baseline),
                "--output", str(output),
            ],
            check=False, capture_output=True, text=True,
        )
        self.assertEqual("", completed.stderr)
        self.assertEqual(0, completed.returncode)
        value = json.loads((output / "timeline-analysis.json").read_text(encoding="utf-8"))
        median = value["comparison"]["metrics"]["median_cpu_ms"]
        self.assertEqual(2.0, median["delta_ms"])
        self.assertEqual(20.0, median["delta_percent"])
        html_text = (output / "frame-timeline.html").read_text(encoding="utf-8")
        self.assertIn("Baseline comparison", html_text)
        self.assertIn("+20.00%", html_text)

        baseline_value = json.loads(baseline.read_text(encoding="utf-8"))
        baseline_value["metadata"] = {"renderer": "different-renderer"}
        baseline.write_text(json.dumps(baseline_value), encoding="utf-8")
        candidate_value = json.loads(candidate.read_text(encoding="utf-8"))
        candidate_value["metadata"] = {"renderer": "candidate-renderer"}
        candidate.write_text(json.dumps(candidate_value), encoding="utf-8")
        incompatible_output = self.root / "incompatible-output"
        incompatible = report.write_report(candidate, incompatible_output, baseline)
        self.assertFalse(incompatible["comparison"]["compatible"])
        self.assertTrue(
            any("renderer differs" in reason for reason in incompatible["comparison"]["reasons"])
        )
        self.assertIn(
            "No comparison",
            (incompatible_output / "frame-timeline.html").read_text(encoding="utf-8"),
        )


if __name__ == "__main__":
    unittest.main()
