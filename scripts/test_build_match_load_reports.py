from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import build_match_load_reports as reports


class MatchLoadReportTests(unittest.TestCase):
    def test_percentiles_use_version_one_nearest_rank(self) -> None:
        self.assertEqual(1.0, reports.nearest_rank([1.0, 2.0], 0.5))
        self.assertEqual(2.0, reports.nearest_rank([1.0, 2.0], 0.95))
        self.assertEqual(99.0, reports.nearest_rank(list(range(1, 101)), 0.99))
        summary = reports.summarize([1.0, 2.0])
        self.assertEqual(1.0, summary["median"])
        self.assertEqual(2.0, summary["p95"])
        self.assertEqual(2.0, summary["p99"])

    def test_package_metadata_names_files_that_the_writer_creates(self) -> None:
        repo = Path.cwd().resolve()
        output = repo / "build" / "benchmark-reports" / "example"
        value = reports.package_info(repo, output, "cpu-benchmark-artifact.json")
        self.assertEqual(
            "build/benchmark-reports/example",
            value["root"],
        )
        self.assertEqual(
            "cpu-benchmark-artifact.json",
            value["manifestPath"],
        )
        self.assertEqual("report-snapshot.json", value["snapshotPath"])

    def test_findings_follow_current_stability_results(self) -> None:
        cpu_pacing = []
        gpu_pacing = []
        for index, profile in enumerate(reports.PROFILE_ORDER):
            cpu_pacing.append(
                {
                    "profile": profile,
                    "median_ms": 1.0 + index,
                    "max_ms": 4.0 + index,
                    "over_16_67_ms_percent": 0.0,
                    "run_median_cv_percent": 5.0 + index,
                    "stable": False,
                }
            )
            gpu_pacing.append({"profile": profile, "median_ms": 0.5 + index})
        datasets = {
            "cpu_pacing": cpu_pacing,
            "gpu_pacing": gpu_pacing,
            "cpu_subsystems": [
                {"profile": "High", "subsystem": "swapchain_acquisition", "median_ms": 1.5},
                {"profile": "High", "subsystem": "renderer_total", "median_ms": 2.0},
            ],
            "gpu_stages": [
                {"profile": "High", "stage": "main_scene", "median_ms": 1.0},
                {"profile": "High", "stage": "sun_shadow", "median_ms": 0.2},
                {"profile": "High", "stage": "bloom", "median_ms": 0.1},
            ],
        }

        complete, cpu, gpu = reports.findings_text(datasets)

        self.assertIn("No preset stayed under the **3%**", complete)
        self.assertIn("No preset stayed under the **3%**", cpu)
        self.assertNotIn("Only Default", complete)
        self.assertIn("Low is the fastest measured preset", complete)
        self.assertNotIn("Competitive is the fastest measured preset", complete)
        self.assertIn("above Low", complete)
        self.assertIn("versus Low", gpu)


if __name__ == "__main__":
    unittest.main()
