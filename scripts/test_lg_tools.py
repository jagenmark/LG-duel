from __future__ import annotations

import io
import json
import socket
import subprocess
import sys
import tempfile
import threading
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

    def test_control_timeout_is_one_total_deadline(self) -> None:
        class FakeConnection:
            def __init__(self) -> None:
                self.timeouts = []
                self.recv_calls = 0

            def __enter__(self):
                return self

            def __exit__(self, *unused):
                return False

            def settimeout(self, timeout):
                self.timeouts.append(timeout)

            def sendall(self, unused):
                return None

            def recv(self, unused):
                self.recv_calls += 1
                return b'{"ok":'

        connection = FakeConnection()
        with mock.patch.object(
            lg_control.socket, "create_connection", return_value=connection
        ), mock.patch.object(
            lg_control.time, "monotonic",
            side_effect=[0.0, 0.0, 0.4, 0.9, 1.1],
        ):
            with self.assertRaisesRegex(
                lg_control.ControlError, "timed out after 1 second"
            ):
                lg_control.send_request("status", timeout=1.0)
        self.assertEqual(connection.recv_calls, 1)
        self.assertAlmostEqual(connection.timeouts[-1], 0.1)

    def test_large_mcp_image_is_omitted_before_read_and_next_call_works(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "large.png"
            image_size = 9_217_968
            path.write_bytes(
                b"\x89PNG\r\n\x1a\n" + b"x" * (image_size - 8)
            )
            capture = {"path": str(path), "map": "eyetoeye"}
            status = {"connected": True}
            with mock.patch.object(
                lg_mcp_server,
                "run_tool_supervised",
                side_effect=[(capture, None), (status, None)],
            ):
                first = lg_mcp_server.handle({
                    "jsonrpc": "2.0", "id": 3, "method": "tools/call",
                    "params": {
                        "name": "lg_capture_screenshot", "arguments": {},
                    },
                })
                second = lg_mcp_server.handle({
                    "jsonrpc": "2.0", "id": 4, "method": "tools/call",
                    "params": {"name": "lg_status", "arguments": {}},
                })
        first_result = first["result"]
        self.assertFalse(first_result["isError"])
        self.assertEqual(len(first_result["content"]), 1)
        structured = first_result["structuredContent"]
        self.assertEqual(structured["path"], str(path))
        self.assertEqual(structured["inline_image_omitted"], "size_limit")
        self.assertEqual(
            structured["inline_image_size_bytes"], image_size
        )
        self.assertTrue(second["result"]["structuredContent"]["connected"])

    def test_mcp_image_budget_is_shared_across_all_views(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            paths = [
                Path(temporary) / "first.png",
                Path(temporary) / "second.png",
            ]
            for path in paths:
                path.write_bytes(b"\x89PNG\r\n\x1a\n" + b"x" * 600_000)
            payload = lg_mcp_server.tool_result({
                "views": [{"path": str(path)} for path in paths],
            })
        images = [
            item for item in payload["content"] if item["type"] == "image"
        ]
        views = payload["structuredContent"]["views"]
        self.assertEqual(len(images), 1)
        self.assertNotIn("inline_image_omitted", views[0])
        self.assertEqual(views[1]["inline_image_omitted"], "size_limit")
        self.assertLessEqual(len(images[0]["data"]), 1024 * 1024)

    def test_oversized_structured_output_keeps_summary_and_paths(self) -> None:
        result = {
            "status": "complete",
            "artifact_path": r"C:\results\summary.json",
            "aggregate": {"mean_fps": 120.5, "p99_ms": 12.0},
            "runs": [{
                "result_directory": rf"C:\results\run-{index}",
                "samples": ["x" * 4096] * 20,
            } for index in range(20)],
        }
        payload = lg_mcp_server.tool_result(result)
        structured = payload["structuredContent"]
        self.assertEqual(
            structured["structured_output_omitted"], "size_limit"
        )
        self.assertEqual(
            structured["artifact_path"], r"C:\results\summary.json"
        )
        self.assertEqual(structured["aggregate"]["mean_fps"], 120.5)
        preserved = structured["structured_output_preserved_paths"]
        self.assertTrue(any(
            item["value"] == r"C:\results\run-0"
            for item in preserved
        ))
        self.assertLessEqual(
            len(json.dumps(payload).encode("utf-8")),
            lg_mcp_server.MCP_RESULT_BUDGET,
        )

    def test_non_ascii_result_uses_the_wire_encoding_for_its_cap(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "inline.png"
            path.write_bytes(
                b"\x89PNG\r\n\x1a\n" + b"x" * (750_000 - 8)
            )
            payload = lg_mcp_server.tool_result({
                "path": str(path),
                "note": "\u00e9" * 120_000,
            })
        wire = json.dumps(
            payload,
            separators=(",", ":"),
            ensure_ascii=False,
        ).encode("utf-8")
        self.assertLessEqual(
            len(wire),
            lg_mcp_server.MCP_RESULT_BUDGET,
        )
        self.assertEqual(lg_mcp_server._json_size(payload), len(wire))

    def test_worker_stdout_limit_returns_bounded_unknown_error(self) -> None:
        class NoisyWorker:
            returncode = 0

            def communicate(self, unused=None, timeout=None):
                return "x" * (lg_mcp_server.WORKER_STDOUT_BUDGET + 1), ""

            def kill(self):
                return None

        worker = NoisyWorker()
        with mock.patch.object(
            lg_mcp_server.subprocess, "Popen", return_value=worker
        ), mock.patch.object(
            lg_mcp_server,
            "_create_worker_tree",
            return_value=lg_mcp_server.WorkerTree(worker),
        ):
            result, error = lg_mcp_server.run_tool_supervised(
                "lg_set_camera", {}
            )
        self.assertIsNone(result)
        self.assertEqual(
            error["structuredContent"]["error"]["code"],
            "worker_output_limit",
        )
        self.assertEqual(error["structuredContent"]["outcome"], "unknown")

    def test_worker_compacts_valid_oversized_result_before_serializing(self) -> None:
        request = b'{"name":"lg_run_benchmark","arguments":{}}\n'
        worker_input = mock.Mock()
        worker_input.buffer = io.BytesIO(request)
        worker_output = io.StringIO()
        result = {
            "artifact_path": r"C:\results\aggregate.json",
            "summary": {"mean_fps": 120.5},
            "runs": ["x" * 4096] * 200,
        }
        with mock.patch.object(
            lg_mcp_server, "invoke_tool", return_value=result
        ), mock.patch.object(
            lg_mcp_server.sys, "stdin", worker_input
        ), mock.patch.object(
            lg_mcp_server.sys, "stdout", worker_output
        ):
            self.assertEqual(lg_mcp_server._worker_main(), 0)
        response = json.loads(worker_output.getvalue())
        structured = response["result"]
        self.assertEqual(
            structured["structured_output_omitted"], "size_limit"
        )
        self.assertEqual(
            structured["artifact_path"], r"C:\results\aggregate.json"
        )
        self.assertEqual(structured["summary"]["mean_fps"], 120.5)
        self.assertLess(
            len(worker_output.getvalue().encode("utf-8")),
            lg_mcp_server.WORKER_STDOUT_BUDGET,
        )

    def test_worker_timeout_is_structured_and_does_not_block_stop(self) -> None:
        class TimedOutWorker:
            returncode = None

            def __init__(self) -> None:
                self.killed = False

            def communicate(self, unused=None, timeout=None):
                if not self.killed:
                    raise subprocess.TimeoutExpired("worker", timeout)
                self.returncode = -9
                return "", ""

            def kill(self):
                self.killed = True

        class StopWorker:
            returncode = 0

            def communicate(self, unused=None, timeout=None):
                return json.dumps({
                    "ok": True, "result": {"stopped": ["client"]},
                }), ""

        def contained(worker):
            return lg_mcp_server.WorkerTree(worker)

        with mock.patch.object(
            lg_mcp_server.subprocess,
            "Popen",
            side_effect=[TimedOutWorker(), StopWorker()],
        ), mock.patch.object(
            lg_mcp_server, "_create_worker_tree", side_effect=contained
        ):
            timed_out = lg_mcp_server.handle({
                "jsonrpc": "2.0", "id": 5, "method": "tools/call",
                "params": {
                    "name": "lg_set_camera",
                    "arguments": {
                        "position": [0, 0, 0], "yaw": 0, "pitch": 0,
                    },
                },
            })
            stopped = lg_mcp_server.handle({
                "jsonrpc": "2.0", "id": 6, "method": "tools/call",
                "params": {"name": "lg_stop", "arguments": {}},
            })
        timeout_result = timed_out["result"]
        self.assertTrue(timeout_result["isError"])
        self.assertEqual(
            timeout_result["structuredContent"]["error"]["code"],
            "worker_timeout",
        )
        self.assertEqual(
            timeout_result["structuredContent"]["outcome"], "unknown"
        )
        self.assertEqual(
            timeout_result["structuredContent"]["timeout_seconds"], 90.0
        )
        self.assertEqual(
            stopped["result"]["structuredContent"]["stopped"], ["client"]
        )

    def test_stdio_cancellation_kills_worker_and_returns_cancelled_error(self) -> None:
        started = threading.Event()
        killed = threading.Event()
        response_ready = threading.Event()
        responses = []

        class BlockingWorker:
            returncode = None

            def communicate(self, unused=None, timeout=None):
                started.set()
                if not killed.wait(2):
                    raise subprocess.TimeoutExpired("worker", timeout)
                self.returncode = -9
                return "", ""

            def kill(self):
                killed.set()

        def write(response):
            responses.append(response)
            response_ready.set()

        dispatcher = lg_mcp_server.McpStdioDispatcher(write)
        with mock.patch.object(
            lg_mcp_server.subprocess, "Popen", return_value=BlockingWorker()
        ):
            dispatcher.dispatch({
                "jsonrpc": "2.0", "id": 10, "method": "tools/call",
                "params": {
                    "name": "lg_map_add_cuboid",
                    "arguments": {},
                },
            })
            self.assertTrue(started.wait(1))
            dispatcher.dispatch({
                "jsonrpc": "2.0",
                "method": "notifications/cancelled",
                "params": {"requestId": 10, "reason": "test cancel"},
            })
            self.assertTrue(response_ready.wait(2))
            dispatcher.finish()
        self.assertTrue(killed.is_set())
        self.assertEqual(len(responses), 1)
        self.assertEqual(responses[0]["error"]["code"], -32800)
        self.assertEqual(responses[0]["error"]["data"]["outcome"], "unknown")
        self.assertEqual(
            responses[0]["error"]["data"]["reason"], "test cancel"
        )

    def test_stdio_stop_overtakes_normal_call_and_other_calls_get_busy(self) -> None:
        normal_started = threading.Event()
        normal_cancelled = threading.Event()
        stop_response = threading.Event()
        responses = []

        def run(name, arguments, cancellation=None):
            if name == "lg_stop":
                return {"stopped": ["client"]}, None
            normal_started.set()
            while not cancellation.cancelled:
                normal_cancelled.wait(0.01)
            normal_cancelled.set()
            return {"operation": name}, None

        def write(response):
            responses.append(response)
            if response.get("id") == 13:
                stop_response.set()

        dispatcher = lg_mcp_server.McpStdioDispatcher(write)
        with mock.patch.object(lg_mcp_server, "run_tool_supervised", side_effect=run):
            dispatcher.dispatch({
                "jsonrpc": "2.0", "id": 11, "method": "tools/call",
                "params": {
                    "name": "lg_set_camera",
                    "arguments": {
                        "position": [0, 0, 0], "yaw": 0, "pitch": 0,
                    },
                },
            })
            self.assertTrue(normal_started.wait(1))
            dispatcher.dispatch({
                "jsonrpc": "2.0", "id": 12, "method": "tools/call",
                "params": {"name": "lg_status", "arguments": {}},
            })
            dispatcher.dispatch({
                "jsonrpc": "2.0", "id": 13, "method": "tools/call",
                "params": {"name": "lg_stop", "arguments": {}},
            })
            self.assertTrue(stop_response.wait(1))
            dispatcher.finish()

        by_id = {response["id"]: response for response in responses}
        self.assertTrue(normal_cancelled.is_set())
        self.assertEqual(
            by_id[12]["result"]["structuredContent"]["error"]["code"],
            "server_busy",
        )
        self.assertEqual(
            by_id[13]["result"]["structuredContent"]["stopped"], ["client"]
        )
        self.assertEqual(by_id[11]["error"]["code"], -32800)
        self.assertEqual(
            by_id[11]["error"]["data"]["reason"], "superseded by lg_stop"
        )

    def test_start_cancellation_stops_contained_worker_tree_promptly(self) -> None:
        started = threading.Event()
        response_ready = threading.Event()
        killed = threading.Event()
        responses = []

        class BlockingStartWorker:
            returncode = None

            def communicate(self, unused=None, timeout=None):
                started.set()
                if not killed.wait(2):
                    raise subprocess.TimeoutExpired("worker", timeout)
                self.returncode = -9
                return "", ""

            def kill(self):
                killed.set()

        def write(response):
            responses.append(response)
            response_ready.set()

        dispatcher = lg_mcp_server.McpStdioDispatcher(write)
        with mock.patch.object(
            lg_mcp_server.subprocess,
            "Popen",
            return_value=BlockingStartWorker(),
        ), mock.patch.object(
            lg_mcp_server,
            "_create_worker_tree",
            side_effect=lambda worker: lg_mcp_server.WorkerTree(worker),
        ):
            dispatcher.dispatch({
                "jsonrpc": "2.0", "id": 14, "method": "tools/call",
                "params": {"name": "lg_start", "arguments": {}},
            })
            self.assertTrue(started.wait(1))
            dispatcher.dispatch({
                "jsonrpc": "2.0",
                "method": "notifications/cancelled",
                "params": {"requestId": 14},
            })
            self.assertTrue(killed.wait(1))
            self.assertTrue(response_ready.wait(1))
            dispatcher.finish()
        self.assertEqual(responses[0]["error"]["code"], -32800)
        self.assertEqual(responses[0]["error"]["data"]["outcome"], "unknown")

    def test_worker_tree_terminates_only_its_posix_process_group(self) -> None:
        worker = mock.Mock(pid=4321)
        tree = lg_mcp_server.WorkerTree(worker, process_group=True)
        with mock.patch.object(
            lg_mcp_server.os, "killpg", create=True
        ) as kill_group:
            tree.terminate()
        kill_group.assert_called_once_with(
            4321,
            getattr(
                lg_mcp_server.signal,
                "SIGKILL",
                lg_mcp_server.signal.SIGTERM,
            ),
        )
        worker.kill.assert_not_called()

    def test_supervisor_deadline_scales_with_valid_benchmark_request(self) -> None:
        self.assertEqual(
            lg_mcp_server.tool_timeout(
                "lg_run_benchmark",
                {"repetitions": 10, "timeout": 300},
            ),
            3415.0,
        )
        self.assertEqual(
            lg_mcp_server.tool_timeout(
                "lg_run_live_scenario", {"timeout": 600}
            ),
            660.0,
        )

    def test_state_changing_worker_errors_report_unknown_outcome(self) -> None:
        changing = lg_mcp_server._error_payload(
            "lg_start", code="tool_error", message="failed after spawn"
        )
        read_only = lg_mcp_server._error_payload(
            "lg_status", code="worker_exit", message="worker failed"
        )
        never_started = lg_mcp_server._error_payload(
            "lg_start", code="worker_tree_unavailable", message="no tree"
        )
        self.assertEqual(
            changing["structuredContent"]["outcome"], "unknown"
        )
        self.assertEqual(
            read_only["structuredContent"]["outcome"], "not_completed"
        )
        self.assertEqual(
            never_started["structuredContent"]["outcome"], "not_completed"
        )

    def test_spawn_capable_tool_fails_closed_without_worker_tree(self) -> None:
        class Worker:
            returncode = None

            def __init__(self) -> None:
                self.killed = False

            def kill(self):
                self.killed = True

        worker = Worker()
        with mock.patch.object(
            lg_mcp_server.subprocess, "Popen", return_value=worker
        ), mock.patch.object(
            lg_mcp_server,
            "_create_worker_tree",
            return_value=lg_mcp_server.WorkerTree(
                worker, contained=False
            ),
        ):
            result, error = lg_mcp_server.run_tool_supervised(
                "lg_start", {}
            )
        self.assertIsNone(result)
        self.assertTrue(worker.killed)
        self.assertEqual(
            error["structuredContent"]["error"]["code"],
            "worker_tree_unavailable",
        )
        self.assertEqual(error["structuredContent"]["outcome"], "not_completed")

    def test_large_error_text_is_bounded_with_an_omission_marker(self) -> None:
        payload = lg_mcp_server._error_payload(
            "lg_start",
            code="tool_error",
            message="x" * (lg_mcp_server.MCP_RESULT_BUDGET + 1),
        )
        self.assertEqual(
            payload["structuredContent"]["error"]["message_omitted"],
            "size_limit",
        )
        self.assertLess(
            len(json.dumps(payload).encode("utf-8")),
            lg_mcp_server.MCP_RESULT_BUDGET,
        )

    def test_raw_stdio_mcp_survives_worker_error(self) -> None:
        server_path = Path(lg_mcp_server.__file__).resolve()
        requests = [
            {"jsonrpc": "2.0", "id": 20, "method": "initialize", "params": {}},
            {
                "jsonrpc": "2.0", "id": 21, "method": "tools/call",
                "params": {"name": "lg_not_a_tool", "arguments": {}},
            },
            {"jsonrpc": "2.0", "id": 22, "method": "ping"},
        ]
        wire = "".join(
            json.dumps(request, separators=(",", ":")) + "\n"
            for request in requests
        )
        process = subprocess.Popen(
            [sys.executable, str(server_path)],
            cwd=server_path.parents[1],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
        )
        stdout, stderr = process.communicate(wire, timeout=15)
        responses = [json.loads(line) for line in stdout.splitlines()]
        by_id = {response["id"]: response for response in responses}
        self.assertEqual(process.returncode, 0, stderr)
        self.assertEqual(set(by_id), {20, 21, 22})
        self.assertTrue(by_id[21]["result"]["isError"])
        self.assertEqual(
            by_id[21]["result"]["structuredContent"]["error"]["code"],
            "tool_error",
        )
        self.assertEqual(by_id[22]["result"], {})

    def test_mcp_tools_have_closed_typed_schemas(self) -> None:
        names = {tool["name"] for tool in lg_mcp_server.TOOLS}
        self.assertEqual(
            names,
            {
                "lg_start", "lg_status", "lg_load_map", "lg_reload_map", "lg_get_camera",
                "lg_set_camera", "lg_set_collision_debug", "lg_capture_screenshot", "lg_capture_map_views",
                "lg_stop", "lg_restart", "lg_exec_console", "lg_get_cvar", "lg_set_cvar",
                "lg_send_input", "lg_wait_frames", "lg_set_player_view", "lg_set_player_weapon",
                "lg_run_live_scenario", "lg_list_benchmarks", "lg_run_benchmark", "lg_compare_benchmarks",
                "lg_get_benchmark_result", "lg_create_benchmark_baseline",
                "lg_map_list", "lg_map_get", "lg_map_create", "lg_map_add_cuboid",
                "lg_map_copy_cuboid", "lg_map_translate_cuboid", "lg_map_resize_cuboid",
                "lg_map_delete_cuboid", "lg_map_set_material",
                "lg_map_set_entity_properties", "lg_map_validate", "lg_map_rollback",
                "lg_map_validate_sync_reload",
            },
        )
        self.assertTrue(all(tool["inputSchema"].get("additionalProperties") is False for tool in lg_mcp_server.TOOLS))

    def test_mcp_initialize_and_list(self) -> None:
        initialized = lg_mcp_server.handle({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}})
        self.assertEqual(initialized["result"]["serverInfo"]["name"], "lg-duel-dev-control")
        listed = lg_mcp_server.handle({"jsonrpc": "2.0", "id": 2, "method": "tools/list"})
        self.assertEqual(len(listed["result"]["tools"]), 37)

    def test_map_edit_tools_require_revisions_and_route_typed_values(self) -> None:
        tools = {tool["name"]: tool["inputSchema"] for tool in lg_mcp_server.TOOLS}
        mutation_names = {
            "lg_map_add_cuboid", "lg_map_copy_cuboid", "lg_map_translate_cuboid",
            "lg_map_resize_cuboid", "lg_map_delete_cuboid", "lg_map_set_material",
            "lg_map_set_entity_properties",
        }
        self.assertTrue(
            all("expected_revision" in tools[name]["required"] for name in mutation_names)
        )
        payload = {
            "map": "agent_test", "id": "floor", "min": [-64, -64, -16],
            "max": [64, 64, 0], "material": "common/clip",
            "expected_revision": "a" * 64, "dry_run": True,
        }
        with mock.patch.object(
            lg_mcp_server.MAP_EDITOR, "add_cuboid", return_value={"applied": False}
        ) as add:
            self.assertEqual(
                lg_mcp_server.invoke_tool("lg_map_add_cuboid", payload),
                {"applied": False},
            )
        add.assert_called_once_with(
            "agent_test", "floor", [-64, -64, -16], [64, 64, 0],
            "common/clip", "a" * 64, dry_run=True,
        )

    def test_player_control_schemas_are_bounded(self) -> None:
        tools = {tool["name"]: tool["inputSchema"] for tool in lg_mcp_server.TOOLS}
        input_schema = tools["lg_send_input"]
        self.assertEqual(input_schema["required"], ["ticks"])
        self.assertEqual(input_schema["properties"]["ticks"]["maximum"], 1250)
        self.assertEqual(input_schema["properties"]["forward"]["minimum"], -1)
        self.assertEqual(input_schema["properties"]["pitch"]["maximum"], 89.9)
        self.assertEqual(input_schema["dependentRequired"]["yaw"], ["pitch"])
        self.assertEqual(tools["lg_wait_frames"]["properties"]["frames"]["maximum"], 600)
        self.assertEqual(tools["lg_exec_console"]["properties"]["command"]["maxLength"], 1024)
        self.assertEqual(tools["lg_set_cvar"]["properties"]["value"]["maxLength"], 256)

    def test_send_input_mcp_and_cli_preserve_explicit_false(self) -> None:
        payload = {
            "ticks": 8, "forward": 1.0, "yaw": 45.0, "attack": False,
            "jump": True, "weapon": "railgun",
        }
        with mock.patch("lg_mcp_server.ensure_client"), mock.patch(
            "lg_mcp_server.send_request", return_value={"ticks": 8}
        ) as sender:
            self.assertEqual(lg_mcp_server.invoke_tool("lg_send_input", payload), {"ticks": 8})
        sender.assert_called_once_with("send_input", **payload)

        arguments = lg_control.build_parser().parse_args(
            ["send-input", "--ticks", "8", "--forward", "1", "--no-attack", "--jump"]
        )
        with mock.patch("lg_launch.ensure_client"), mock.patch(
            "lg_control.send_request", return_value={"ticks": 8}
        ) as sender:
            lg_control.execute(arguments)
        sender.assert_called_once_with(
            "send_input", ticks=8, forward=1.0, attack=False, jump=True,
            host="127.0.0.1", port=27961, timeout=60.0,
        )

    def test_player_control_cli_rejects_out_of_range_values(self) -> None:
        with self.assertRaises(SystemExit):
            lg_control.build_parser().parse_args(["send-input", "--ticks", "0"])
        with self.assertRaises(SystemExit):
            lg_control.build_parser().parse_args(
                ["set-player-view", "--yaw", "0", "--pitch", "90"]
            )
        arguments = lg_control.build_parser().parse_args(
            ["send-input", "--ticks", "1", "--yaw", "10"]
        )
        with self.assertRaisesRegex(lg_control.ControlError, "together"):
            lg_control.execute(arguments)

    def test_lifecycle_mcp_routes_without_control_start_probe(self) -> None:
        with mock.patch("lg_mcp_server.stop_owned", return_value={"stopped": ["client"]}) as stop, \
             mock.patch("lg_mcp_server.ensure_client") as ensure:
            self.assertEqual(
                lg_mcp_server.invoke_tool("lg_stop", {}), {"stopped": ["client"]}
            )
        stop.assert_called_once_with()
        ensure.assert_not_called()
        with mock.patch(
            "lg_mcp_server.restart_owned", return_value={"stopped": ["client"], "status": {}}
        ) as restart:
            lg_mcp_server.invoke_tool("lg_restart", {"renderer": "gpu"})
        restart.assert_called_once_with(renderer="gpu", allow_fallback=False)

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
            {
                "scenario", "repetitions", "server_port", "control_port",
                "port", "timeout", "build_mode",
            },
        )
        self.assertEqual(
            tools["lg_run_benchmark"]["properties"]["server_port"]["default"],
            28960,
        )
        self.assertEqual(
            tools["lg_run_benchmark"]["properties"]["control_port"]["default"],
            28961,
        )
        self.assertEqual(set(tools["lg_compare_benchmarks"]["required"]), {"baseline", "result"})
        self.assertEqual(set(tools["lg_get_benchmark_result"]["properties"]), {"result", "detailed"})
        self.assertEqual(set(tools["lg_create_benchmark_baseline"]["required"]), {"scenario", "name"})

    def test_benchmark_mcp_routes_explicit_debug_mode(self) -> None:
        with mock.patch("lg_mcp_server.run_benchmark", return_value={"runs": []}) as runner:
            lg_mcp_server.invoke_tool(
                "lg_run_benchmark",
                {
                    "scenario": "eyetoeye-static-baseline",
                    "build_mode": "debug",
                    "server_port": 30060,
                    "control_port": 30061,
                },
            )
        self.assertEqual(runner.call_args.kwargs["build_mode"], "debug")
        self.assertEqual(runner.call_args.kwargs["server_port"], 30060)
        self.assertEqual(runner.call_args.kwargs["control_port"], 30061)
        self.assertIsNone(runner.call_args.kwargs["port"])

    def test_benchmark_mcp_routes_legacy_control_port_alias(self) -> None:
        with mock.patch(
            "lg_mcp_server.run_benchmark", return_value={"runs": []}
        ) as runner:
            lg_mcp_server.invoke_tool(
                "lg_run_benchmark",
                {"scenario": "eyetoeye-static-baseline", "port": 30061},
            )
        self.assertEqual(runner.call_args.kwargs["server_port"], 28960)
        self.assertIsNone(runner.call_args.kwargs["control_port"])
        self.assertEqual(runner.call_args.kwargs["port"], 30061)

    def test_live_scenario_mcp_routes_without_shell(self) -> None:
        with mock.patch("lg_mcp_server.run_live_scenario", return_value={"status": "passed"}) as runner:
            self.assertEqual(
                lg_mcp_server.invoke_tool("lg_run_live_scenario", {"scenario": "basic_forward", "timeout": 12}),
                {"status": "passed"},
            )
        self.assertEqual(runner.call_args.args[0].name, "basic_forward.json")
        self.assertEqual(runner.call_args.kwargs["timeout"], 12.0)

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
        bootstrap = (root / "scripts" / "bootstrap-windows-client.ps1").read_text(
            encoding="utf-8"
        )
        server = (root / "Start LG Duel Server.bat").read_text(encoding="utf-8")
        self.assertIn("scripts\\bootstrap-windows-client.ps1", launcher)
        self.assertIn("-RepairIfNeeded", launcher)
        self.assertIn('"--preset", "default", "--fresh"', bootstrap)
        self.assertIn("Using the existing SDL3 build", bootstrap)
        self.assertIn("CMAKE_GENERATOR:INTERNAL=Ninja", bootstrap)
        self.assertIn('cmake --build "build\\default" --target lg_duel_server --parallel', server)
        self.assertNotIn("cmake --build --preset default --target lg_duel_server", server)
        self.assertIn("scripts\\lg-dev.ps1", launcher)
        self.assertIn("-Renderer gpu", launcher)
        self.assertIn("-ExternalServer", launcher)
        self.assertNotIn("set LG_DUEL_RENDER_BACKEND=gpu", launcher)
        self.assertNotIn("build\\default\\lg_duel_client.exe 127.0.0.1", launcher)

    def test_static_world_stays_linear_until_global_scene_curve(self) -> None:
        root = Path(__file__).resolve().parents[1]
        shader = (root / "assets" / "shaders" / "world_surface.frag").read_text(
            encoding="utf-8"
        )
        composite = (
            root / "assets" / "shaders" / "scene_composite.frag"
        ).read_text(encoding="utf-8")
        renderer = (root / "src" / "render" / "Renderer.cpp").read_text(
            encoding="utf-8"
        )
        self.assertIn("same single scene-to-display curve", shader)
        self.assertNotIn("acesToneMap(", shader)
        self.assertIn("acesToneMap(", composite)
        self.assertIn("sceneLights.colorIntensity", shader)
        self.assertIn("sceneLights.parameters.z", shader)
        self.assertIn("sunShadowVisibility", shader)
        self.assertIn("hazeCap = atmosphereQuality == 1", shader)
        vertex_shader = (
            root / "assets" / "shaders" / "world_surface.vert"
        ).read_text(encoding="utf-8")
        self.assertIn("pow(max(inColor.rgb, vec3(0.0)), vec3(2.2))", vertex_shader)
        self.assertIn("max(vertexColor.rgb, vec3(0.00169355))", shader)
        self.assertIn('"world_surface.frag.spv",\n    2', renderer)

    def test_material_quality_zero_gates_fragment_light_loops(self) -> None:
        root = Path(__file__).resolve().parents[1]
        for shader_name in (
            "world_surface.frag",
            "world3d.frag",
            "gltf_player_model.frag",
            "material_weapon.frag",
        ):
            shader = (
                root / "assets" / "shaders" / shader_name
            ).read_text(encoding="utf-8")
            quality_gate = shader.index("if (materialQuality > 0) {")
            local_light_loop = shader.index(
                "for (int index = 0; index <",
                quality_gate,
            )
            self.assertLess(quality_gate, local_light_loop, shader_name)

    def test_competitive_direct_shaders_compile_out_expensive_paths(self) -> None:
        root = Path(__file__).resolve().parents[1]
        shader_dir = root / "assets" / "shaders"
        shader_names = (
            "world_surface_direct.frag",
            "world3d_direct.frag",
            "instanced_color_direct.frag",
            "material_weapon_direct.frag",
            "gltf_player_model_direct.frag",
        )
        shaders = {
            name: (shader_dir / name).read_text(encoding="utf-8")
            for name in shader_names
        }
        for name, shader in shaders.items():
            self.assertIn("directDisplay", shader, name)
            for excluded in (
                "sampler2DShadow",
                "positionRadius",
                "shadowParameters",
                "sunShadowVisibility",
                "materialQuality",
                "atmosphereQuality",
                "haze",
                "for (",
            ):
                self.assertNotIn(excluded, shader, name)

        static_world = shaders["world_surface_direct.frag"]
        self.assertIn("uniform sampler2D worldAtlas", static_world)
        self.assertIn("uniform DirectSunData", static_world)
        self.assertIn("uniform WorldMaterialData", static_world)
        self.assertIn("vec3 bakedLight", static_world)
        self.assertIn("directAlbedo * sunRadiance * sunNDotL", static_world)
        self.assertIn("worldMaterial.traits.w", static_world)

        dynamic_world = shaders["world3d_direct.frag"]
        self.assertIn("uniform sampler2D worldAtlas", dynamic_world)
        self.assertNotIn("layout(set = 3", dynamic_world)

        instanced = shaders["instanced_color_direct.frag"]
        self.assertNotIn("uniform", instanced)
        self.assertNotIn("sampler", instanced)

        for name in (
            "material_weapon_direct.frag",
            "gltf_player_model_direct.frag",
        ):
            self.assertIn("uniform DirectLightData", shaders[name])
            self.assertNotIn("sampler", shaders[name])
        self.assertIn("teamTint", shaders["gltf_player_model_direct.frag"])
        self.assertIn("float rim = pow", shaders["gltf_player_model_direct.frag"])

        renderer = (root / "src" / "render" / "Renderer.cpp").read_text(
            encoding="utf-8"
        )
        for shader_name in shader_names:
            self.assertIn(f'"{shader_name}.spv"', renderer)
        self.assertIn('"world_surface.vert.spv"', renderer)
        self.assertIn("DirectPresentFallbackReason::QualityContract", renderer)
        self.assertIn("bool enableColorBlend = true", renderer)
        self.assertIn(
            '"world3d_direct.frag.spv",\n'
            "    1,\n"
            "    SDL_GPU_SAMPLECOUNT_1,\n"
            "    swapchainFormat,\n"
            "    0,\n"
            "    false,\n"
            '    "world3d.vert.spv",\n'
            "    false\n"
            "  );",
            renderer,
        )
        self.assertIn(
            '"world_surface_direct.frag.spv",\n'
            "    1,\n"
            "    SDL_GPU_SAMPLECOUNT_1,\n"
            "    swapchainFormat,\n"
            "    2,\n"
            "    false,\n"
            '    "world_surface.vert.spv",\n'
            "    false\n"
            "  );",
            renderer,
        )
        self.assertIn(
            "colorTarget.blend_state.enable_blend = enableColorBlend;",
            renderer,
        )

        cmake = (root / "CMakeLists.txt").read_text(encoding="utf-8")
        for shader_name in shader_names:
            self.assertEqual(4, cmake.count(f"{shader_name}.spv"), shader_name)
        self.assertEqual(4, cmake.count("world_surface.vert.spv"))

    def test_outline_mask_vertex_shaders_are_lean(self) -> None:
        root = Path(__file__).resolve().parents[1]
        shader_dir = root / "assets" / "shaders"
        expected_inputs = {
            "outline_mask_world.vert": 1,
            "outline_mask_static.vert": 4,
            "outline_mask_gltf.vert": 9,
        }
        for shader_name, input_count in expected_inputs.items():
            source = (shader_dir / shader_name).read_text(encoding="utf-8")
            self.assertEqual(input_count, source.count("layout(location = "), shader_name)
            self.assertIn("invariant gl_Position;", source, shader_name)
            self.assertNotIn(" out ", source, shader_name)
            self.assertEqual(
                b"\x03\x02#\x07",
                (shader_dir / f"{shader_name}.spv").read_bytes()[:4],
                shader_name,
            )
        for shader_name in (
            "world3d.vert",
            "static_mesh_instance.vert",
            "material_mesh_instance.vert",
            "gltf_player_model.vert",
        ):
            source = (shader_dir / shader_name).read_text(encoding="utf-8")
            self.assertIn("invariant gl_Position;", source, shader_name)

        renderer = (root / "src" / "render" / "Renderer.cpp").read_text(
            encoding="utf-8"
        )
        world_start = renderer.index("createGpuPipelineOutlineMask(")
        world_end = renderer.index("createGpuPipelineOutlineClear(", world_start)
        world_pipeline = renderer[world_start:world_end]
        self.assertIn('"outline_mask_world.vert.spv"', world_pipeline)
        self.assertIn(
            "std::array<SDL_GPUVertexAttribute, 1> vertexAttributes",
            world_pipeline,
        )

        static_start = renderer.index("createGpuStaticMeshOutlineMaskPipeline(")
        static_end = renderer.index("createGpuGltfPlayerModelPipeline(", static_start)
        static_pipeline = renderer[static_start:static_end]
        self.assertIn('"outline_mask_static.vert.spv"', static_pipeline)
        self.assertIn(
            "std::array<SDL_GPUVertexAttribute, 4> vertexAttributes",
            static_pipeline,
        )
        self.assertIn(
            "materialLayout ? sizeof(GpuMaterialVertex) : sizeof(GpuVertex)",
            static_pipeline,
        )
        self.assertIn(
            "mesh->materialLit ? materialPipeline : simplePipeline",
            renderer,
        )
        self.assertIn(
            "std::array<SDL_GPUVertexAttribute, 9> outlineVertexAttributes",
            renderer,
        )
        self.assertIn('"outline_mask_gltf.vert.spv"', renderer)

        cmake = (root / "CMakeLists.txt").read_text(encoding="utf-8")
        package = (root / "scripts" / "package-windows.ps1").read_text(
            encoding="utf-8"
        )
        for shader_name in expected_inputs:
            self.assertEqual(4, cmake.count(f"{shader_name}.spv"), shader_name)
            self.assertEqual(1, package.count(f"{shader_name}.spv"), shader_name)

    def test_empty_overlay_skips_swapchain_render_pass(self) -> None:
        root = Path(__file__).resolve().parents[1]
        renderer = (root / "src" / "render" / "Renderer.cpp").read_text(
            encoding="utf-8"
        )
        guard = (
            "if (overlayVertexCount > 0U && !overlayBatches.empty()) {"
        )
        overlay_guard = renderer.index(guard)
        overlay_pass = renderer.index(
            "SDL_BeginGPURenderPass(commandBuffer, &colorTarget, 1, nullptr)",
            overlay_guard,
        )
        self.assertLess(overlay_guard, overlay_pass)

    def test_direct_world_sun_matches_fallback_material_zero(self) -> None:
        root = Path(__file__).resolve().parents[1]
        shaders = root / "assets" / "shaders"
        direct = (shaders / "world_surface_direct.frag").read_text(
            encoding="utf-8"
        )
        fallback = (shaders / "world_surface.frag").read_text(
            encoding="utf-8"
        )
        parity_lines = (
            "float bakedPeak = max(",
            "vec3 authoredTint = clamp(vertexColor.rgb / bakedPeak, 0.0, 1.0);",
            "vec3 directAlbedo = albedo * authoredTint;",
            "sceneColor += directAlbedo * sunRadiance * sunNDotL",
        )
        for line in parity_lines:
            self.assertIn(line, direct)
            self.assertIn(line, fallback)
        albedo = 0.5
        baked_light = 0.25
        authored_tint = baked_light / baked_light
        expected = albedo * baked_light + albedo * authored_tint
        self.assertAlmostEqual(0.625, expected)

    def test_shadow_draw_skips_material_environment_binding(self) -> None:
        root = Path(__file__).resolve().parents[1]
        renderer = (root / "src" / "render" / "Renderer.cpp").read_text(
            encoding="utf-8"
        )
        shadow_call = (
            "drawStaticMeshBatches(\n"
            "        shadowPass,\n"
            "        sunShadowStaticPipeline,\n"
            "        sunShadowMaterialPipeline,\n"
            "        simpleResources,\n"
            "        perspectiveScene,\n"
            "        RenderPass::OpaqueWorld,\n"
            "        false\n"
            "      );"
        )
        self.assertIn(shadow_call, renderer)

    def test_f10_graphics_menu_covers_saved_visual_quality_controls(self) -> None:
        root = Path(__file__).resolve().parents[1]
        app = (root / "src" / "app" / "GameApp.cpp").read_text(encoding="utf-8")
        for label in (
            "Combat effects / temp lights",
            "Tone-map exposure",
            "Atmosphere / grade",
            "Bright-effect bloom",
            "Bloom strength",
            "Cartridge casings",
            "Impact-particle density",
            "Bullet decal budget",
        ):
            self.assertIn(label, app)
        for cvar in (
            "r_combat_effects",
            "r_tonemap_exposure",
            "r_atmosphere_grade",
            "r_bloom",
            "r_bloom_intensity",
            "r_casings",
            "r_impact_particles",
            "r_decals_max",
        ):
            self.assertIn(f'"set {cvar} "', app)

    def test_power_shell_launcher_forwards_a_bounded_timeout(self) -> None:
        launcher = (Path(__file__).resolve().parent / "lg-dev.ps1").read_text(encoding="utf-8")
        self.assertIn("[ValidateRange(0.25, 120.0)]", launcher)
        self.assertIn("'--timeout', $Timeout", launcher)

    def test_power_shell_control_routes_status_to_the_lifecycle_wrapper(self) -> None:
        wrapper = (Path(__file__).resolve().parent / "lg-control.ps1").read_text(encoding="utf-8")
        self.assertIn("'restart', 'status'", wrapper)


if __name__ == "__main__":
    unittest.main()
