from __future__ import annotations

import socket
import unittest

import lg_control
import lg_mcp_server


class LgToolTests(unittest.TestCase):
    def test_position_validation(self) -> None:
        self.assertEqual(lg_control.parse_position("12,8,4"), [12.0, 8.0, 4.0])
        self.assertEqual(lg_control.parse_position(["12", "8", "4"]), [12.0, 8.0, 4.0])
        with self.assertRaises(Exception):
            lg_control.parse_position("12,8")

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
                "lg_status", "lg_load_map", "lg_reload_map", "lg_get_camera",
                "lg_set_camera", "lg_capture_screenshot", "lg_capture_map_views",
                "lg_list_benchmarks", "lg_run_benchmark", "lg_compare_benchmarks",
                "lg_get_benchmark_result", "lg_create_benchmark_baseline",
            },
        )
        self.assertTrue(all(tool["inputSchema"].get("additionalProperties") is False for tool in lg_mcp_server.TOOLS))

    def test_mcp_initialize_and_list(self) -> None:
        initialized = lg_mcp_server.handle({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}})
        self.assertEqual(initialized["result"]["serverInfo"]["name"], "lg-duel-dev-control")
        listed = lg_mcp_server.handle({"jsonrpc": "2.0", "id": 2, "method": "tools/list"})
        self.assertEqual(len(listed["result"]["tools"]), 12)

    def test_benchmark_mcp_schemas_are_exact_and_closed(self) -> None:
        tools = {tool["name"]: tool["inputSchema"] for tool in lg_mcp_server.TOOLS}
        self.assertEqual(tools["lg_list_benchmarks"]["properties"], {})
        self.assertEqual(set(tools["lg_run_benchmark"]["properties"]), {"scenario", "repetitions", "port", "timeout"})
        self.assertEqual(set(tools["lg_compare_benchmarks"]["required"]), {"baseline", "result"})
        self.assertEqual(set(tools["lg_get_benchmark_result"]["properties"]), {"result", "detailed"})
        self.assertEqual(set(tools["lg_create_benchmark_baseline"]["required"]), {"scenario", "name"})


if __name__ == "__main__":
    unittest.main()
