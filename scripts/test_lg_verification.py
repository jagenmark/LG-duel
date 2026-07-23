from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import lg_verification


class VerificationEvidenceTests(unittest.TestCase):
    def test_manifest_update_keeps_categories_and_has_relative_artifacts(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "evidence"
            root.mkdir()
            (root / "result.txt").write_text("result", encoding="utf-8")
            lg_verification.collect_ci(root, "linux", "unit", "success")
            manifest = lg_verification.collect_ci(root, "windows", "build", "warn")
            self.assertEqual(list(manifest["categories"]), ["build", "unit"])
            self.assertEqual(manifest["categories"]["unit"]["status"], "PASS")
            self.assertEqual(manifest["artifacts"], [{
                "path": "result.txt", "sha256": "f6a214f7a5fcda0c2cee9660b7fc29f5649e3c68aad48e20e950137c98913a68", "size_bytes": 6,
            }])
            self.assertFalse(Path(manifest["artifacts"][0]["path"]).is_absolute())

    def test_partial_failure_is_recorded(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "evidence"
            lg_verification.collect_ci(root, "linux", "live", "failure")
            summary = json.loads((root / "summary.json").read_text(encoding="utf-8"))
            self.assertEqual(summary["status"], "FAIL")
            self.assertEqual(summary["categories"]["live"]["status"], "FAIL")

    def test_artifact_order_is_stable(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "evidence"
            (root / "z").mkdir(parents=True)
            (root / "a.txt").write_text("a", encoding="utf-8")
            (root / "z" / "b.txt").write_text("b", encoding="utf-8")
            (root / "temp" / "builds").mkdir(parents=True)
            (root / "temp" / "builds" / "binary").write_text("large", encoding="utf-8")
            first = lg_verification.artifact_index(root)
            second = lg_verification.artifact_index(root)
            self.assertEqual(first, second)
            self.assertEqual([item["path"] for item in first], ["a.txt", "z/b.txt"])

    def test_status_normalization(self) -> None:
        self.assertEqual(lg_verification.normalize_status("SUCCESS"), "PASS")
        self.assertEqual(lg_verification.normalize_status("unavailable"), "UNAVAILABLE")
        self.assertEqual(lg_verification.normalize_status("not_comparable"), "NOT_COMPARABLE")
        with self.assertRaises(ValueError):
            lg_verification.normalize_status("maybe")

    def _fake_binary(self, build_dir: Path, output: str, code: int = 0) -> Path:
        binary = build_dir / ("lg_duel_protocol_tests.exe" if __import__("os").name == "nt" else "lg_duel_protocol_tests")
        binary.parent.mkdir(parents=True, exist_ok=True)
        binary.write_text("fake", encoding="utf-8")
        return binary

    def test_packet_budget_passes_from_fake_binary(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root, build = Path(temporary) / "evidence", Path(temporary) / "build"
            binary = self._fake_binary(build, "")
            completed = mock.Mock(returncode=0, stdout="snapshot bytes: duel=811 six-player=1000\n")
            with mock.patch.object(lg_verification, "_source_budget", return_value=1200), \
                 mock.patch("subprocess.run", return_value=completed) as run:
                result, code = lg_verification.protocol_budget(root, build, 1200)
            self.assertEqual(code, 0)
            self.assertEqual(result["status"], "PASS")
            self.assertEqual(result["max_observed_datagram_bytes"], 1000)
            self.assertIn(mock.call([str(binary)], cwd=binary.parent, text=True,
                                    stdout=mock.ANY, stderr=mock.ANY, timeout=120,
                                    check=False), run.call_args_list)

    def test_packet_budget_fails_over_limit(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root, build = Path(temporary) / "evidence", Path(temporary) / "build"
            self._fake_binary(build, "")
            completed = mock.Mock(returncode=0, stdout="application datagram bytes=1201\n")
            with mock.patch.object(lg_verification, "_source_budget", return_value=1200), \
                 mock.patch("subprocess.run", return_value=completed):
                result, code = lg_verification.protocol_budget(root, build, 1200)
            self.assertEqual(code, 1)
            self.assertEqual(result["hard_limit_status"], "FAIL")
            self.assertEqual(result["status"], "FAIL")

    def test_packet_parser_reads_command_bundle_and_missing_facts_fail(self) -> None:
        facts = lg_verification.parse_packet_facts(
            "command control bundle bytes=1140\n"
        )
        self.assertEqual(
            facts,
            [{"bytes": 1140, "label": "command control bundle"}],
        )
        with tempfile.TemporaryDirectory() as temporary:
            root, build = Path(temporary) / "evidence", Path(temporary) / "build"
            self._fake_binary(build, "")
            completed = mock.Mock(returncode=0, stdout="all protocol tests passed\n")
            with mock.patch.object(lg_verification, "_source_budget", return_value=1200), \
                 mock.patch("subprocess.run", return_value=completed):
                result, code = lg_verification.protocol_budget(root, build, 1200)
            self.assertEqual(code, 1)
            self.assertEqual(result["hard_limit_status"], "FAIL")
            self.assertEqual(result["status"], "FAIL")

    def test_missing_binary_is_unavailable_and_fails_command(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root, build = Path(temporary) / "evidence", Path(temporary) / "build"
            with mock.patch.object(lg_verification, "_source_budget", return_value=1200):
                result, code = lg_verification.protocol_budget(root, build, 1200)
            self.assertEqual(code, 1)
            self.assertEqual(result["status"], "UNAVAILABLE")
            self.assertIsNone(result["max_observed_datagram_bytes"])

    def test_source_constant_mismatch_fails(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root, build = Path(temporary) / "evidence", Path(temporary) / "build"
            self._fake_binary(build, "")
            completed = mock.Mock(returncode=0, stdout="packet bytes=700\n")
            with mock.patch.object(lg_verification, "_source_budget", return_value=1199), \
                 mock.patch("subprocess.run", return_value=completed):
                result, code = lg_verification.protocol_budget(root, build, 1200)
            self.assertEqual(code, 1)
            self.assertEqual(result["source_constant_status"], "FAIL")


if __name__ == "__main__":
    unittest.main()
