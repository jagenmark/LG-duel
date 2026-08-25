from __future__ import annotations

import json
import os
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import lg_mcp_server
from lg_tool_feedback import FeedbackStore


class FeedbackStoreTests(unittest.TestCase):
    def test_feedback_receipt_report_and_digest(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            store = FeedbackStore(Path(temporary))
            store.record_call(
                call_id="LGC-test-1",
                tool="lg_capture_screenshot",
                duration_ms=1840,
                outcome="not_completed",
                error_code="renderer_unavailable",
                server_version="1.7.0",
            )
            result = store.submit(
                tool="lg_capture_screenshot",
                kind="poor_diagnostic",
                impact="workaround",
                note="The error did not explain <which> recovery call to use.",
                call_id="LGC-test-1",
                server_version="1.7.0",
            )

            self.assertTrue(result["call_linked"])
            self.assertTrue(result["final_response_required"])
            self.assertIn("[WORKAROUND] lg_capture_screenshot", result["receipt"])
            self.assertTrue(store.report_path.is_file())
            rendered = store.report_path.read_text(encoding="utf-8")
            self.assertIn("LG Devtools Feedback", rendered)
            self.assertIn("&lt;which&gt;", rendered)
            self.assertNotIn("<which>", rendered)

            digest = store.digest()
            self.assertEqual(digest["new_count"], 1)
            self.assertEqual(digest["impact_counts"], {"workaround": 1})
            marked = store.digest(mark_seen=True)
            self.assertEqual(marked["marked_seen"], 1)
            self.assertEqual(store.digest()["new_count"], 0)

    def test_feedback_rejects_unknown_enums_and_long_notes(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            store = FeedbackStore(Path(temporary))
            with self.assertRaisesRegex(ValueError, "kind must be one of"):
                store.submit(
                    tool="general",
                    kind="game_bug",
                    impact="minor",
                    note="Wrong category.",
                )
            with self.assertRaisesRegex(ValueError, "at most 2000"):
                store.submit(
                    tool="general",
                    kind="docs",
                    impact="minor",
                    note="x" * 2001,
                )

    def test_call_record_contains_no_arguments_or_prompt(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            store = FeedbackStore(Path(temporary))
            store.record_call(
                call_id="LGC-test-2",
                tool="lg_exec_console",
                duration_ms=12,
                outcome="completed",
            )
            path = next((Path(temporary) / "calls").glob("*.json"))
            record = json.loads(path.read_text(encoding="utf-8"))
            self.assertEqual(record["tool"], "lg_exec_console")
            self.assertNotIn("arguments", record)
            self.assertNotIn("prompt", record)


class FeedbackMcpTests(unittest.TestCase):
    def test_feedback_tool_is_closed_and_returns_user_receipt(self) -> None:
        tool = next(
            tool
            for tool in lg_mcp_server.TOOLS
            if tool["name"] == "lg_report_tool_feedback"
        )
        self.assertFalse(tool["inputSchema"]["additionalProperties"])
        self.assertIn("receipt", tool["description"])
        with tempfile.TemporaryDirectory() as temporary, mock.patch.dict(
            os.environ, {"LG_MCP_FEEDBACK_DIR": temporary}
        ):
            result = lg_mcp_server.invoke_tool(
                "lg_report_tool_feedback",
                {
                    "tool": "lg_status",
                    "kind": "confusing_interface",
                    "impact": "blocked",
                    "note": "The result did not identify the owned worktree.",
                },
            )
        self.assertIn("[BLOCKED] lg_status", result["receipt"])
        self.assertTrue(result["final_response_required"])

    def test_dispatch_adds_call_context_and_records_call(self) -> None:
        responses: list[dict] = []
        with tempfile.TemporaryDirectory() as temporary, mock.patch.dict(
            os.environ, {"LG_MCP_FEEDBACK_DIR": temporary}
        ), mock.patch.object(
            lg_mcp_server,
            "run_tool_supervised",
            return_value=({"connected": True}, None),
        ):
            dispatcher = lg_mcp_server.McpStdioDispatcher(responses.append)
            dispatcher.dispatch({
                "jsonrpc": "2.0",
                "id": 7,
                "method": "tools/call",
                "params": {"name": "lg_status", "arguments": {}},
            })
            dispatcher.finish()
            call_files = list((Path(temporary) / "calls").glob("*.json"))

        self.assertEqual(len(responses), 1)
        structured = responses[0]["result"]["structuredContent"]
        self.assertEqual(structured["tool_call"]["tool"], "lg_status")
        self.assertRegex(structured["tool_call"]["call_id"], r"^LGC-")
        text_result = json.loads(responses[0]["result"]["content"][0]["text"])
        self.assertEqual(text_result["tool_call"], structured["tool_call"])
        self.assertEqual(len(call_files), 1)


if __name__ == "__main__":
    unittest.main()
