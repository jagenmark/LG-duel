import json
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
HELPER = ROOT / "scripts" / "integrate-tasks.ps1"
POWERSHELL = shutil.which("pwsh") or shutil.which("powershell")


def run(command, cwd, check=True):
    result = subprocess.run(
        command,
        cwd=cwd,
        check=False,
        text=True,
        capture_output=True,
    )
    if check and result.returncode:
        raise AssertionError(
            f"Command failed ({result.returncode}): {command}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    return result


@unittest.skipUnless(POWERSHELL, "PowerShell is required")
class IntegrationHelperTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="lg-integration-test-")
        self.root = Path(self.temp.name) / "repo"
        self.root.mkdir()
        run(["git", "init", "-b", "main"], self.root)
        run(["git", "config", "user.name", "Integration Test"], self.root)
        run(["git", "config", "user.email", "integration@example.invalid"], self.root)
        (self.root / "scripts").mkdir()
        self.helper = self.root / "scripts" / "integrate-tasks.ps1"
        shutil.copy2(HELPER, self.helper)
        (self.root / ".gitignore").write_text(
            "/reports/integration/*.generated.md\n", encoding="utf-8"
        )
        (self.root / "base.txt").write_text("base\n", encoding="utf-8")
        run(["git", "add", ".gitignore", "base.txt"], self.root)
        run(["git", "commit", "-m", "base"], self.root)

        run(["git", "switch", "-c", "task/bootstrap"], self.root)
        (self.root / "order.txt").write_text("bootstrap\n", encoding="utf-8")
        run(["git", "add", "order.txt"], self.root)
        run(["git", "commit", "-m", "bootstrap"], self.root)
        self.bootstrap = run(["git", "rev-parse", "HEAD"], self.root).stdout.strip()

        run(["git", "switch", "main"], self.root)
        run(["git", "switch", "-c", "task/ui"], self.root)
        (self.root / "ui.txt").write_text("ui\n", encoding="utf-8")
        run(["git", "add", "ui.txt"], self.root)
        run(["git", "commit", "-m", "ui"], self.root)
        self.ui = run(["git", "rev-parse", "HEAD"], self.root).stdout.strip()
        run(["git", "switch", "main"], self.root)

    def tearDown(self):
        self.temp.cleanup()

    def write_manifest(self):
        manifest = {
            "version": 1,
            "base": "main",
            "integrationBranch": "integration/test",
            "worktreePath": "../integration-worktree",
            "reportPath": "reports/integration/test.generated.md",
            "groups": [
                {
                    "name": "bootstrap",
                    "items": [{"type": "commit", "ref": self.bootstrap}],
                    "validation": [
                        {
                            "command": "git",
                            "arguments": ["diff", "--check", "HEAD^", "HEAD"],
                        }
                    ],
                },
                {
                    "name": "UI/settings",
                    "items": [
                        {
                            "type": "branch",
                            "ref": "task/ui",
                            "from": "main",
                        }
                    ],
                    "validation": [],
                },
            ],
        }
        path = self.root / "manifest.json"
        path.write_text(json.dumps(manifest), encoding="utf-8")
        return path

    def invoke(self, manifest, *extra, check=True):
        return run(
            [
                POWERSHELL,
                "-NoProfile",
                "-ExecutionPolicy",
                "Bypass",
                "-File",
                str(self.helper),
                "-ManifestPath",
                str(manifest),
                *extra,
            ],
            self.root,
            check=check,
        )

    def test_dry_run_and_repeatable_ordered_integration(self):
        manifest = self.write_manifest()
        dry_run = self.invoke(manifest, "-DryRun")
        self.assertIn(self.bootstrap, dry_run.stdout)
        self.assertFalse((self.root.parent / "integration-worktree").exists())

        self.invoke(manifest)
        worktree = self.root.parent / "integration-worktree"
        subjects = run(
            ["git", "log", "--format=%s", "--reverse", "main..HEAD"], worktree
        ).stdout.splitlines()
        self.assertEqual(subjects, ["bootstrap", "ui"])
        report = worktree / "reports" / "integration" / "test.generated.md"
        self.assertIn("Result: success", report.read_text(encoding="utf-8-sig"))

        rerun = self.invoke(manifest)
        self.assertIn("Skipping recorded commit", rerun.stdout)
        subjects_after = run(
            ["git", "log", "--format=%s", "--reverse", "main..HEAD"], worktree
        ).stdout.splitlines()
        self.assertEqual(subjects_after, subjects)

    def test_conflict_stops_without_resolving(self):
        run(["git", "switch", "-c", "task/change-a"], self.root)
        (self.root / "base.txt").write_text("change a\n", encoding="utf-8")
        run(["git", "add", "base.txt"], self.root)
        run(["git", "commit", "-m", "change a"], self.root)
        change_a = run(["git", "rev-parse", "HEAD"], self.root).stdout.strip()

        run(["git", "switch", "main"], self.root)
        run(["git", "switch", "-c", "task/change-b"], self.root)
        (self.root / "base.txt").write_text("change b\n", encoding="utf-8")
        run(["git", "add", "base.txt"], self.root)
        run(["git", "commit", "-m", "change b"], self.root)
        change_b = run(["git", "rev-parse", "HEAD"], self.root).stdout.strip()
        run(["git", "switch", "main"], self.root)

        manifest = {
            "version": 1,
            "base": "main",
            "integrationBranch": "integration/conflict-test",
            "worktreePath": "../conflict-worktree",
            "reportPath": "reports/integration/conflict.generated.md",
            "groups": [
                {
                    "name": "conflict",
                    "items": [
                        {"type": "commit", "ref": change_a},
                        {"type": "commit", "ref": change_b},
                    ],
                    "validation": [],
                }
            ],
        }
        path = self.root / "conflict-manifest.json"
        path.write_text(json.dumps(manifest), encoding="utf-8")
        result = self.invoke(path, check=False)
        self.assertEqual(result.returncode, 2)
        self.assertIn("The helper did not resolve files", result.stderr)

        worktree = self.root.parent / "conflict-worktree"
        unmerged = run(
            ["git", "diff", "--name-only", "--diff-filter=U"], worktree
        ).stdout.splitlines()
        self.assertEqual(unmerged, ["base.txt"])
        report = worktree / "reports" / "integration" / "conflict.generated.md"
        self.assertIn("Result: conflict", report.read_text(encoding="utf-8-sig"))

    def test_rejects_active_worktree_and_report_escape(self):
        manifest = {
            "version": 1,
            "base": "main",
            "integrationBranch": "integration/unsafe-test",
            "worktreePath": ".",
            "reportPath": "reports/integration/test.generated.md",
            "groups": [{"name": "empty", "items": [], "validation": []}],
        }
        path = self.root / "unsafe-manifest.json"
        path.write_text(json.dumps(manifest), encoding="utf-8")
        active_result = self.invoke(path, "-DryRun", check=False)
        self.assertNotEqual(active_result.returncode, 0)
        self.assertIn("must be outside the source worktree", active_result.stderr)

        manifest["worktreePath"] = "../safe-worktree"
        manifest["reportPath"] = "../base.txt"
        path.write_text(json.dumps(manifest), encoding="utf-8")
        report_result = self.invoke(path, "-DryRun", check=False)
        self.assertNotEqual(report_result.returncode, 0)
        self.assertIn("reports/integration", report_result.stderr)
        self.assertEqual((self.root / "base.txt").read_text(encoding="utf-8"), "base\n")

        run(["git", "switch", "-c", "task/tracked-report"], self.root)
        tracked_report = self.root / "reports" / "integration" / "test.generated.md"
        tracked_report.parent.mkdir(parents=True)
        tracked_report.write_text("task data\n", encoding="utf-8")
        run(["git", "add", "-f", str(tracked_report)], self.root)
        run(["git", "commit", "-m", "add reserved report"], self.root)
        report_commit = run(["git", "rev-parse", "HEAD"], self.root).stdout.strip()
        run(["git", "switch", "main"], self.root)

        manifest["reportPath"] = "reports/integration/test.generated.md"
        manifest["groups"][0]["items"] = [{"type": "commit", "ref": report_commit}]
        path.write_text(json.dumps(manifest), encoding="utf-8")
        tracked_result = self.invoke(path, "-DryRun", check=False)
        self.assertNotEqual(tracked_result.returncode, 0)
        self.assertIn("tracks reserved report path", tracked_result.stderr)
        self.assertFalse((self.root.parent / "safe-worktree").exists())


if __name__ == "__main__":
    unittest.main()
