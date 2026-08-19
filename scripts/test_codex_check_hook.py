#!/usr/bin/env python3

from __future__ import annotations

import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))

import codex_check_hook as hook


class CodexCheckHookTests(unittest.TestCase):
    def test_only_exact_request_script_selects_scope(self) -> None:
        payload = {"tool_input": {"command": "python3 scripts/request_codex_check.py changed-files"}}
        self.assertEqual(hook.requested_scope(payload), "changed-files")
        self.assertIsNone(hook.requested_scope({"tool_input": {"command": "echo changed-files"}}))

    def test_docs_only_change_has_no_commands(self) -> None:
        self.assertEqual(hook.command_plan("changed-files", [Path("docs/README.md")]), [])

    def test_cpp_change_selects_build_and_ctest(self) -> None:
        commands = hook.command_plan("changed-files", [Path("src/sim/Combat.cpp")])
        self.assertIn(["cmake", "--build", "--preset", "default", "--parallel", "2"], commands)
        self.assertEqual(commands[-1][0:2], ["ctest", "--preset"])

    def test_shader_change_selects_only_shader_checks(self) -> None:
        commands = hook.command_plan("changed-files", [Path("assets/shaders/world.vert")])
        self.assertEqual(len(commands), 2)
        self.assertEqual(commands[-1][-1], "--check")


if __name__ == "__main__":
    unittest.main()
