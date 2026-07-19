from __future__ import annotations

import socket
import unittest
from pathlib import Path
from unittest import mock

import lg_control
import lg_mcp_server


class LgToolTests(unittest.TestCase):
    def test_position_validation(self) -> None:
        self.assertEqual(lg_control.parse_position("12,8,4"), [12.0, 8.0, 4.0])
        self.assertEqual(lg_control.parse_position(["12", "8", "4"]), [12.0, 8.0, 4.0])
        with self.assertRaises(Exception):
            lg_control.parse_position("12,8")

    def test_collision_debug_cli_routes_typed_mode(self) -> None:
        arguments = lg_control.build_parser().parse_args(["set-collision-debug", "4"])
        with mock.patch("lg_launch.ensure_client"), mock.patch(
            "lg_control.send_request", return_value={"mode": 4}
        ) as sender:
            self.assertEqual(lg_control.execute(arguments), {"mode": 4})
        sender.assert_called_once_with(
            "set_collision_debug", mode=4, host="127.0.0.1", port=27961, timeout=60.0
        )
        with self.assertRaises(SystemExit):
            lg_control.build_parser().parse_args(["set-collision-debug", "6"])

    def test_standard_preset_has_three_named_views(self) -> None:
        views = lg_control.load_preset("eyetoeye", "standard")
        self.assertGreaterEqual(len(views), 3)
        self.assertTrue(all(isinstance(view.get("name"), str) for view in views))

    def test_connection_refusal_is_actionable(self) -> None:
        probe = socket.socket()
        probe.bind(("127.0.0.1", 0))
        port = probe.getsockname()[1]
        probe.close()
        with self.assertRaisesRegex(lg_control.ControlError, "--dev-control"):
            lg_control.send_request("status", port=port, timeout=0.25)

    def test_mcp_tools_have_closed_typed_schemas(self) -> None:
        names = {tool["name"] for tool in lg_mcp_server.TOOLS}
        self.assertEqual(
            names,
            {
                "lg_start", "lg_status", "lg_load_map", "lg_reload_map", "lg_get_camera",
                "lg_set_camera", "lg_set_collision_debug", "lg_capture_screenshot", "lg_capture_map_views",
                "lg_list_benchmarks", "lg_run_benchmark", "lg_compare_benchmarks",
                "lg_get_benchmark_result", "lg_create_benchmark_baseline",
            },
        )
        self.assertTrue(all(tool["inputSchema"].get("additionalProperties") is False for tool in lg_mcp_server.TOOLS))

    def test_mcp_initialize_and_list(self) -> None:
        initialized = lg_mcp_server.handle({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}})
        self.assertEqual(initialized["result"]["serverInfo"]["name"], "lg-duel-dev-control")
        listed = lg_mcp_server.handle({"jsonrpc": "2.0", "id": 2, "method": "tools/list"})
        self.assertEqual(len(listed["result"]["tools"]), 14)

    def test_collision_debug_mcp_schema_and_routing(self) -> None:
        tools = {tool["name"]: tool["inputSchema"] for tool in lg_mcp_server.TOOLS}
        schema = tools["lg_set_collision_debug"]
        self.assertEqual(schema["required"], ["mode"])
        self.assertEqual(
            schema["properties"]["mode"],
            {"type": "integer", "minimum": 0, "maximum": 5},
        )
        with mock.patch("lg_mcp_server.ensure_client"), mock.patch(
            "lg_mcp_server.send_request", return_value={"mode": 5}
        ) as sender:
            self.assertEqual(lg_mcp_server.invoke_tool("lg_set_collision_debug", {"mode": 5}), {"mode": 5})
        sender.assert_called_once_with("set_collision_debug", mode=5)

    def test_benchmark_mcp_schemas_are_exact_and_closed(self) -> None:
        tools = {tool["name"]: tool["inputSchema"] for tool in lg_mcp_server.TOOLS}
        self.assertEqual(tools["lg_list_benchmarks"]["properties"], {})
        self.assertEqual(
            set(tools["lg_run_benchmark"]["properties"]),
            {"scenario", "repetitions", "port", "timeout", "build_mode"},
        )
        self.assertEqual(set(tools["lg_compare_benchmarks"]["required"]), {"baseline", "result"})
        self.assertEqual(set(tools["lg_get_benchmark_result"]["properties"]), {"result", "detailed"})
        self.assertEqual(set(tools["lg_create_benchmark_baseline"]["required"]), {"scenario", "name"})

    def test_benchmark_mcp_routes_explicit_debug_mode(self) -> None:
        with mock.patch("lg_mcp_server.run_benchmark", return_value={"runs": []}) as runner:
            lg_mcp_server.invoke_tool(
                "lg_run_benchmark", {"scenario": "eyetoeye-static-baseline", "build_mode": "debug"}
            )
        self.assertEqual(runner.call_args.kwargs["build_mode"], "debug")

    def test_mcp_setup_uses_absolute_python_and_verifies_registration(self) -> None:
        setup = (Path(__file__).resolve().parent / "setup-lg-mcp.ps1").read_text(encoding="utf-8")
        self.assertIn("Get-Command python.exe", setup)
        self.assertIn("[IO.Path]::GetFullPath($python.Source)", setup)
        self.assertIn("mcp list", setup)
        self.assertIn("Codex config:", setup)
        self.assertIn("Registration host:", setup)

    def test_root_gpu_batch_uses_verified_launcher(self) -> None:
        root = Path(__file__).resolve().parents[1]
        launcher = (root / "Start LG Duel Client GPU.bat").read_text(encoding="utf-8")
        self.assertIn("scripts\\lg-dev.ps1", launcher)
        self.assertIn("-Renderer gpu", launcher)
        self.assertIn("-ExternalServer", launcher)
        self.assertNotIn("set LG_DUEL_RENDER_BACKEND=gpu", launcher)
        self.assertNotIn("build\\default\\lg_duel_client.exe 127.0.0.1", launcher)


if __name__ == "__main__":
    unittest.main()
