from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
WORKFLOW_PATH = ROOT / ".github" / "workflows" / "pr-verification.yml"
REPAIR_WORKFLOW_PATH = ROOT / ".github" / "workflows" / "pr267-review-fixes.yml"

EXPECTED_JOBS = {
    "linux-build-and-tests",
    "windows-build-and-tests",
    "deterministic-scenarios",
    "protocol-and-packet-budgets",
    "live-client-server-smoke",
    "linux-sanitizers",
    "performance-smoke",
}
PR_HEAD_REF = "${{ github.event.pull_request.head.sha || github.sha }}"


def workflow_jobs(workflow: str) -> dict[str, str]:
    _, separator, jobs_text = workflow.partition("\njobs:\n")
    if not separator:
        return {}

    matches = list(re.finditer(r"(?m)^  ([a-z][a-z0-9-]*):\s*$", jobs_text))
    return {
        match.group(1): jobs_text[
            match.end() : matches[index + 1].start()
            if index + 1 < len(matches)
            else len(jobs_text)
        ]
        for index, match in enumerate(matches)
    }


class PrVerificationWorkflowTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.workflow = WORKFLOW_PATH.read_text(encoding="utf-8")
        cls.jobs = workflow_jobs(cls.workflow)

    def test_workflow_is_read_only_and_has_no_repair_code(self) -> None:
        header = self.workflow.partition("\njobs:\n")[0]
        self.assertRegex(header, r"(?m)^permissions:\n  contents: read$")
        self.assertNotIn("contents: write", self.workflow)
        self.assertNotRegex(self.workflow, r"(?m)\bgit (?:add|commit|push)\b")
        self.assertNotIn("apply-and-verify", self.workflow)
        self.assertNotIn("agent/test-ci-audit-slice-2", self.workflow)
        self.assertFalse(REPAIR_WORKFLOW_PATH.exists())

    def test_workflow_keeps_each_check_identity(self) -> None:
        self.assertEqual(set(self.jobs), EXPECTED_JOBS)
        for job_name, job in self.jobs.items():
            with self.subTest(job=job_name):
                self.assertRegex(job, rf"(?m)^    name: {re.escape(job_name)}$")

    def test_source_jobs_check_out_the_pull_request_head(self) -> None:
        source_jobs = {
            "linux-build-and-tests",
            "windows-build-and-tests",
            "live-client-server-smoke",
            "linux-sanitizers",
            "performance-smoke",
        }
        for job_name in source_jobs:
            with self.subTest(job=job_name):
                job = self.jobs[job_name]
                self.assertIn("uses: actions/checkout@", job)
                self.assertIn(f"ref: {PR_HEAD_REF}", job)

    def test_evidence_identity_matches_the_checked_out_head(self) -> None:
        header = self.workflow.partition("\njobs:\n")[0]
        self.assertIn(f"LG_CANDIDATE_COMMIT: {PR_HEAD_REF}", header)

    def test_linux_result_gates_reuse_the_linux_build(self) -> None:
        linux = self.jobs["linux-build-and-tests"]
        self.assertIn("determinism: ${{ steps.determinism.outcome }}", linux)
        self.assertIn("protocol: ${{ steps.protocol.outcome }}", linux)

        for job_name, output_name in (
            ("deterministic-scenarios", "determinism"),
            ("protocol-and-packet-budgets", "protocol"),
        ):
            with self.subTest(job=job_name):
                job = self.jobs[job_name]
                self.assertRegex(job, r"(?m)^    needs: linux-build-and-tests$")
                self.assertIn(
                    f"needs.linux-build-and-tests.outputs.{output_name}", job
                )
                self.assertNotIn("actions/checkout", job)
                self.assertNotIn("cmake ", job)

    def test_each_check_uploads_evidence_for_fourteen_days(self) -> None:
        for job_name, job in self.jobs.items():
            with self.subTest(job=job_name):
                self.assertIn("uses: actions/upload-artifact@v4", job)
                self.assertRegex(job, r"(?m)^          retention-days: 14$")

    def test_performance_check_keeps_its_opt_in_gate(self) -> None:
        performance = self.jobs["performance-smoke"]
        self.assertIn("github.event_name == 'workflow_dispatch'", performance)
        self.assertIn(
            "contains(github.event.pull_request.labels.*.name, 'performance-smoke')",
            performance,
        )

    def test_label_event_can_start_performance_check(self) -> None:
        header = self.workflow.partition("\njobs:\n")[0]
        self.assertRegex(
            header,
            r"(?ms)^  pull_request:\n"
            r"(?=.*?^    types:\n(?:^      - [a-z]+\n)*^      - labeled$)",
        )


if __name__ == "__main__":
    unittest.main()
