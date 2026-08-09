from __future__ import annotations

import base64
import io
import json
import math
import random
import re
import socket
import subprocess
import sys
import tempfile
import threading
import unittest
from pathlib import Path
from unittest import mock

import import_q3_map
from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageStat
import lg_control
import lg_mcp_server


def write_test_screenshot(path: Path) -> Image.Image:
    random_bytes = random.Random(7).randbytes(480 * 270 * 3)
    image = Image.frombytes("RGB", (480, 270), random_bytes)
    image = image.resize((1920, 1080), Image.Resampling.BICUBIC)
    image = image.filter(ImageFilter.GaussianBlur(1.2))
    draw = ImageDraw.Draw(image)
    for x in range(0, image.width, 80):
        draw.line((x, 0, x, image.height), fill=(235, 180, 70), width=2)
    for y in range(0, image.height, 80):
        draw.line((0, y, image.width, y), fill=(60, 180, 240), width=2)
    image.save(path, format="PNG", optimize=True)
    return image


class LgToolTests(unittest.TestCase):
    def test_pr_workflow_requires_pillow_for_worker_material_checks(self) -> None:
        root = Path(__file__).resolve().parents[1]
        cmake = (root / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn("LG_DUEL_REQUIRE_PILLOW", cmake)
        self.assertRegex(
            cmake,
            r"elseif\(LG_DUEL_REQUIRE_PILLOW\)\s+message\(\s+FATAL_ERROR",
        )
        self.assertIn("Pillow not found; skipping Worker material atlas tests", cmake)

        workflow = (root / ".github" / "workflows" / "pr-verification.yml").read_text(
            encoding="utf-8"
        )
        for job_name, preset in (
            ("linux-build-and-tests", "default"),
            ("windows-build-and-tests", "msvc"),
        ):
            with self.subTest(job=job_name):
                job = re.search(
                    rf"(?ms)^  {re.escape(job_name)}:\n(.*?)(?=^  [\w-]+:|\Z)",
                    workflow,
                )
                self.assertIsNotNone(job)
                job_body = job.group(1)
                install = 'python -m pip install --disable-pip-version-check "Pillow==11.3.0"'
                configure = f"cmake --preset {preset}"
                self.assertIn(install, job_body)
                self.assertIn(configure, job_body)
                self.assertIn("-DLG_DUEL_REQUIRE_PILLOW=ON", job_body)
                self.assertIn("-DPython3_EXECUTABLE=", job_body)
                self.assertLess(job_body.index(install), job_body.index(configure))

    def test_map_editor_generated_state_is_ignored(self) -> None:
        root = Path(__file__).resolve().parents[1]
        ignore_lines = (root / ".gitignore").read_text(
            encoding="utf-8"
        ).splitlines()
        self.assertIn("/maps/.lg-map-api/", ignore_lines)

    def test_runtime_map_entity_classes_have_docs_and_fgd_declarations(self) -> None:
        root = Path(__file__).resolve().parents[1]
        converter = (root / "src" / "map" / "MapToArena.cpp").read_text(
            encoding="utf-8"
        )
        classes = set(re.findall(
            r'(?:\*\s*)?classname\s*(?:==|!=)\s*"([^"]+)"',
            converter,
        ))
        self.assertTrue(classes)
        docs = (root / "docs" / "architecture" / "maps-assets.md").read_text(
            encoding="utf-8"
        )
        fgd = (
            root / "tools" / "trenchbroom" / "LG Duel" / "lgduel.fgd"
        ).read_text(encoding="utf-8")
        for classname in sorted(classes):
            with self.subTest(classname=classname):
                self.assertIn(classname, docs)
                self.assertRegex(
                    fgd,
                    rf"(?m)=\s*{re.escape(classname)}\s*(?::|\[|$)",
                )

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

    def test_default_mcp_image_is_compact_clear_and_next_call_works(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "large.png"
            source = write_test_screenshot(path)
            source_size = path.stat().st_size
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
        self.assertEqual(len(first_result["content"]), 2)
        structured = first_result["structuredContent"]
        self.assertEqual(structured["path"], str(path))
        self.assertEqual(structured["inline_image_mode"], "compact")
        self.assertEqual(structured["inline_image_format"], "webp")
        self.assertEqual(structured["inline_image_quality"], 92)
        self.assertEqual(
            (
                structured["inline_image_width"],
                structured["inline_image_height"],
            ),
            (1600, 900),
        )
        self.assertLessEqual(
            structured["inline_image_pixels"],
            lg_mcp_server.DEFAULT_INLINE_IMAGE_MAX_PIXELS,
        )
        self.assertLess(
            structured["inline_image_size_bytes"], source_size * 0.25
        )
        self.assertEqual(
            structured["inline_image_source_size_bytes"], source_size
        )
        image_content = first_result["content"][1]
        self.assertEqual(image_content["mimeType"], "image/webp")
        compact = Image.open(io.BytesIO(base64.b64decode(
            image_content["data"]
        ))).convert("RGB")
        reference = source.resize(compact.size, Image.Resampling.LANCZOS)
        difference = ImageStat.Stat(ImageChops.difference(
            reference, compact
        ))
        combined_rms = math.sqrt(sum(
            channel * channel for channel in difference.rms
        ) / len(difference.rms))
        self.assertLess(combined_rms, 20)
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
            }, image_arguments={"inline_image_mode": "full"})
        images = [
            item for item in payload["content"] if item["type"] == "image"
        ]
        views = payload["structuredContent"]["views"]
        self.assertEqual(len(images), 1)
        self.assertNotIn("inline_image_omitted", views[0])
        self.assertEqual(views[1]["inline_image_omitted"], "size_limit")
        self.assertLessEqual(len(images[0]["data"]), 1024 * 1024)

    def test_multiple_compact_images_share_the_reply_budget(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            paths = [
                Path(temporary) / "first.png",
                Path(temporary) / "second.png",
            ]
            for index, path in enumerate(paths):
                image = Image.new("RGB", (1280, 720), (20, 30, 45))
                draw = ImageDraw.Draw(image)
                for offset in range(0, 1280, 32):
                    draw.line(
                        (offset, 0, 1279 - offset, 719),
                        fill=(80 + index * 40, 170, 230), width=2,
                    )
                image.save(path)
            payload = lg_mcp_server.tool_result({
                "views": [{"path": str(path)} for path in paths],
            })
        images = [
            item for item in payload["content"] if item["type"] == "image"
        ]
        self.assertEqual(len(images), 2)
        self.assertLessEqual(
            sum(len(item["data"]) for item in images),
            lg_mcp_server.INLINE_IMAGE_BUDGET,
        )

    def test_full_mcp_image_mode_returns_the_saved_png_unchanged(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "full.png"
            Image.new("RGB", (64, 32), (20, 80, 160)).save(path)
            saved = path.read_bytes()
            payload = lg_mcp_server.tool_result(
                {"path": str(path)},
                image_arguments={"inline_image_mode": "full"},
            )
        delivered = payload["content"][1]
        self.assertEqual(delivered["mimeType"], "image/png")
        self.assertEqual(base64.b64decode(delivered["data"]), saved)
        self.assertEqual(
            payload["structuredContent"]["inline_image_mode"], "full"
        )

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
        ), mock.patch.object(
            lg_mcp_server,
            "_create_worker_tree",
            side_effect=lambda worker: lg_mcp_server.WorkerTree(worker),
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
        tools = {tool["name"]: tool for tool in lg_mcp_server.TOOLS}
        names = set(tools)
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
                "lg_map_list_point_lights", "lg_map_add_point_light",
                "lg_map_update_point_light", "lg_map_remove_point_light",
                "lg_map_get_world_lighting", "lg_map_set_world_lighting",
                "lg_map_apply_batch",
                "lg_map_list_teleports", "lg_map_add_teleport",
                "lg_map_update_teleport", "lg_map_remove_teleport",
            },
        )
        self.assertTrue(all(tool["inputSchema"].get("additionalProperties") is False for tool in lg_mcp_server.TOOLS))
        for name in {"lg_capture_screenshot", "lg_capture_map_views"}:
            properties = tools[name]["inputSchema"]["properties"]
            self.assertEqual(
                properties["inline_image_mode"]["default"], "compact"
            )
            self.assertEqual(
                properties["inline_image_format"]["default"], "webp"
            )
            self.assertEqual(
                properties["inline_image_max_pixels"]["default"],
                1600 * 900,
            )
            self.assertEqual(
                properties["inline_image_quality"]["default"], 92
            )

    def test_mcp_initialize_and_list(self) -> None:
        initialized = lg_mcp_server.handle({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}})
        self.assertEqual(initialized["result"]["serverInfo"]["name"], "lg-duel-dev-control")
        listed = lg_mcp_server.handle({"jsonrpc": "2.0", "id": 2, "method": "tools/list"})
        self.assertEqual(len(listed["result"]["tools"]), 48)

    def test_map_edit_tools_require_revisions_and_route_typed_values(self) -> None:
        tools = {tool["name"]: tool["inputSchema"] for tool in lg_mcp_server.TOOLS}
        mutation_names = {
            "lg_map_add_cuboid", "lg_map_copy_cuboid", "lg_map_translate_cuboid",
            "lg_map_resize_cuboid", "lg_map_delete_cuboid", "lg_map_set_material",
            "lg_map_set_entity_properties",
            "lg_map_add_point_light", "lg_map_update_point_light",
            "lg_map_remove_point_light", "lg_map_set_world_lighting",
            "lg_map_apply_batch",
            "lg_map_add_teleport", "lg_map_update_teleport",
            "lg_map_remove_teleport",
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

    def test_map_lighting_schemas_and_routing_are_typed(self) -> None:
        tools = {tool["name"]: tool["inputSchema"] for tool in lg_mcp_server.TOOLS}
        add = tools["lg_map_add_point_light"]
        self.assertEqual(add["properties"]["priority"]["minimum"], -1000)
        self.assertEqual(add["properties"]["intensity"]["maximum"], 16)
        self.assertEqual(add["properties"]["radius"]["maximum"], 4096)
        self.assertEqual(add["properties"]["source_radius"]["maximum"], 1024)
        self.assertEqual(add["properties"]["flicker_seed"]["maximum"], 4294967295)
        self.assertEqual(add["properties"]["flicker_frequency"]["minimum"], 0.1)
        self.assertEqual(add["properties"]["flicker_frequency"]["maximum"], 30)
        batch = tools["lg_map_apply_batch"]["properties"]["operations"]
        self.assertEqual(batch["maxItems"], 128)
        self.assertEqual(len(batch["items"]["oneOf"]), 14)
        self.assertTrue(
            all(
                option["additionalProperties"] is False
                for option in batch["items"]["oneOf"]
            )
        )

        payload = {
            "map": "agent_test", "id": "torch-a", "origin": [1, 2, 3],
            "color": [255, 160, 64], "intensity": 2.5, "radius": 320,
            "casts_shadows": True, "source_radius": 12, "priority": 20,
            "flicker_enabled": True, "flicker_seed": 7,
            "flicker_frequency": 6, "flicker_min": 0.8,
            "flicker_max": 1.2, "expected_revision": "b" * 64,
            "dry_run": True,
        }
        with mock.patch.object(
            lg_mcp_server.MAP_EDITOR, "add_point_light",
            return_value={"applied": False},
        ) as add_light:
            self.assertEqual(
                lg_mcp_server.invoke_tool("lg_map_add_point_light", payload),
                {"applied": False},
            )
        add_light.assert_called_once_with(
            "agent_test", "torch-a", [1, 2, 3], [255, 160, 64], 2.5, 320,
            "b" * 64, casts_shadows=True, source_radius=12, priority=20,
            flicker_enabled=True, flicker_seed=7, flicker_frequency=6,
            flicker_min=0.8, flicker_max=1.2, dry_run=True,
        )

        operations = [{
            "op": "set_world_lighting", "ambient_intensity": 0.4
        }]
        with mock.patch.object(
            lg_mcp_server.MAP_EDITOR, "apply_batch",
            return_value={"applied": True},
        ) as apply_batch:
            result = lg_mcp_server.invoke_tool("lg_map_apply_batch", {
                "map": "agent_test", "operations": operations,
                "expected_revision": "c" * 64,
            })
        self.assertEqual(result, {"applied": True})
        apply_batch.assert_called_once_with(
            "agent_test", operations, "c" * 64, dry_run=False
        )

    def test_map_teleport_schemas_and_routing_are_typed(self) -> None:
        tools = {tool["name"]: tool["inputSchema"] for tool in lg_mcp_server.TOOLS}
        add = tools["lg_map_add_teleport"]
        self.assertEqual(
            add["required"],
            [
                "map", "id", "min", "max", "destination", "exit_yaw",
                "expected_revision",
            ],
        )
        self.assertFalse(add["additionalProperties"])
        self.assertEqual(
            add["properties"]["exit_yaw"]["minimum"], -40000
        )
        batch_options = tools["lg_map_apply_batch"]["properties"][
            "operations"
        ]["items"]["oneOf"]
        teleport_ops = {
            option["properties"]["op"]["const"]
            for option in batch_options
            if "teleport" in option["properties"]["op"]["const"]
        }
        self.assertEqual(
            teleport_ops,
            {"add_teleport", "update_teleport", "remove_teleport"},
        )

        payload = {
            "map": "agent_test", "id": "gate-a",
            "min": [-16, -16, 0], "max": [16, 16, 32],
            "destination": [128, 0, 16], "exit_yaw": 90,
            "expected_revision": "d" * 64, "dry_run": True,
        }
        with mock.patch.object(
            lg_mcp_server.MAP_EDITOR, "add_teleport",
            return_value={"applied": False},
        ) as add_teleport:
            result = lg_mcp_server.invoke_tool("lg_map_add_teleport", payload)
        self.assertEqual(result, {"applied": False})
        add_teleport.assert_called_once_with(
            "agent_test", "gate-a", [-16, -16, 0], [16, 16, 32],
            [128, 0, 16], 90, "d" * 64, dry_run=True,
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
        self.assertIn('#include "includes/atmosphere.glsl"', shader)
        atmosphere = (
            root / "assets" / "shaders" / "includes" / "atmosphere.glsl"
        ).read_text(encoding="utf-8")
        self.assertIn("float hazeCap = quality == 1", atmosphere)
        vertex_shader = (
            root / "assets" / "shaders" / "world_surface.vert"
        ).read_text(encoding="utf-8")
        self.assertIn("pow(max(inColor.rgb, vec3(0.0)), vec3(2.2))", vertex_shader)
        self.assertIn("max(vertexColor.rgb, vec3(0.00169355))", shader)
        self.assertIn('"world_surface.frag.spv",\n    3', renderer)

    def test_dynamic_live_fill_uses_one_scene_relative_correction(self) -> None:
        root = Path(__file__).resolve().parents[1]
        shader_dir = root / "assets" / "shaders"
        shared = (shader_dir / "includes" / "live_fill.glsl").read_text(
            encoding="utf-8"
        )
        self.assertIn("kSceneFillEncoding = vec3(0.30, 0.36, 0.46)", shared)
        self.assertIn("kLiveFillBaselineScale = 0.90", shared)
        self.assertIn("mapAmbientRadiance", shared)
        self.assertIn("fillColor = {", (root / "src" / "render" / "Scene3D.cpp").read_text(
            encoding="utf-8"
        ))
        for shader_name in (
            "material_weapon.frag",
            "material_weapon_direct.frag",
            "gltf_player_model.frag",
            "gltf_player_model_flat.frag",
            "gltf_player_model_direct.frag",
        ):
            shader = (shader_dir / shader_name).read_text(encoding="utf-8")
            self.assertIn('#include "includes/live_fill.glsl"', shader)
            self.assertIn("correctedLiveFill(fillRadiance)", shader)
            self.assertNotIn("vec3(0.16) + fillRadiance", shader)
            self.assertNotIn("vec3(0.18) + fillRadiance", shader)

    def test_display_gamma_is_final_and_disables_direct_present_when_changed(self) -> None:
        root = Path(__file__).resolve().parents[1]
        shader_dir = root / "assets" / "shaders"
        cvars = (root / "src" / "app" / "ClientCvars.cpp").read_text(
            encoding="utf-8"
        )
        config = (root / "config" / "default_client.cfg").read_text(
            encoding="utf-8"
        )
        app = (root / "src" / "app" / "GameApp.cpp").read_text(
            encoding="utf-8"
        )
        renderer_hpp = (root / "src" / "render" / "Renderer.hpp").read_text(
            encoding="utf-8"
        )
        renderer = (root / "src" / "render" / "Renderer.cpp").read_text(
            encoding="utf-8"
        )
        self.assertIn('"r_display_gamma"', cvars)
        self.assertIn("1.0F, archivedClient, 0.50F, 1.50F", cvars)
        self.assertIn("set r_display_gamma 1", config)
        self.assertIn('"Brightness / gamma"', app)
        self.assertIn("settings.displayGamma = console.getFloat(\"r_display_gamma\")", app)
        self.assertIn("DisplayGamma", renderer_hpp)
        self.assertIn("neutralDisplayGamma", renderer_hpp)
        self.assertIn("displayGammaIsNeutral(settings.displayGamma)", renderer)
        self.assertIn('return "display-gamma"', renderer)
        for shader_name in ("scene_composite.frag", "scene_composite_no_bloom.frag"):
            shader = (shader_dir / shader_name).read_text(encoding="utf-8")
            self.assertIn('#include "includes/display_gamma.glsl"', shader)
            self.assertIn("displayEncode(displayColor, composite.parameters.w)", shader)
            self.assertLess(shader.index("gradeColor("), shader.index("displayEncode("))

    def test_worker_flat_shaders_share_luminance_team_tint(self) -> None:
        root = Path(__file__).resolve().parents[1]
        shader_dir = root / "assets" / "shaders"
        shared = (shader_dir / "includes" / "team_tint.glsl").read_text(
            encoding="utf-8"
        )
        self.assertIn("float sourceValue = dot(albedo", shared)
        self.assertIn("vec3 tinted = tint *", shared)
        self.assertNotIn("albedo * tint", shared)
        for shader_name in (
            "gltf_player_model.frag",
            "gltf_player_model_flat.frag",
            "gltf_player_model_direct.frag",
        ):
            shader = (shader_dir / shader_name).read_text(encoding="utf-8")
            self.assertIn('#include "includes/team_tint.glsl"', shader)
            self.assertIn("applyTeamTint(", shader)
        for shader_name in (
            "gltf_player_model_flat.frag",
            "gltf_player_model_direct.frag",
        ):
            shader = (shader_dir / shader_name).read_text(encoding="utf-8")
            self.assertNotIn("base * tint", shader)

    def test_material_quality_preserves_point_light_diffuse_contract(self) -> None:
        root = Path(__file__).resolve().parents[1]
        reference = (root / "src" / "render" / "PointLightResponse.hpp").read_text(
            encoding="utf-8"
        )
        self.assertIn("pointLightResponseReference(", reference)
        self.assertIn(
            "response.diffuse = multiplyComponents(albedo, radiance) * diffuseNdotL;",
            reference,
        )
        self.assertIn("if (materialQuality == 2 && diffuseNdotL > 0.0F)", reference)

        shared_response = (
            root / "assets" / "shaders" / "includes" / "point_light_response.glsl"
        ).read_text(encoding="utf-8")
        self.assertIn("vec3 pointLightDiffuseResponse(", shared_response)
        self.assertIn(
            "return albedo * radiance * max(nDotL, 0.0);", shared_response
        )

        shader_calls = {
            "world_surface.frag": "sceneColor += pointLightDiffuseResponse(",
            "gltf_player_model.frag": "color += pointLightDiffuseResponse(",
            "material_weapon.frag": "color += pointLightDiffuseResponse(",
        }
        for shader_name, diffuse_call in shader_calls.items():
            shader = (root / "assets" / "shaders" / shader_name).read_text(
                encoding="utf-8"
            )
            light_loop = shader.index("for (int index = 0; index <")
            diffuse_response = shader.index(diffuse_call, light_loop)
            enhanced_gate = shader.index("if (materialQuality == 2)", diffuse_response)
            self.assertIn('#include "includes/point_light_response.glsl"', shader)
            self.assertLess(light_loop, diffuse_response, shader_name)
            self.assertLess(diffuse_response, enhanced_gate, shader_name)

    def test_static_world_light_loop_skips_baked_lights_before_work(self) -> None:
        root = Path(__file__).resolve().parents[1]
        shader = (root / "assets" / "shaders" / "world_surface.frag").read_text(
            encoding="utf-8"
        )
        loop = shader.index("for (int index = 0; index <")
        guard = shader.index(
            "float liveWorldScale = sceneLights.lightParameters[index].y",
            loop,
        )
        offset = shader.index("vec3 offset =", guard)
        self.assertLess(loop, guard)
        self.assertLess(guard, offset)
        self.assertIn("if (liveWorldScale <= 0.0) {", shader[guard:offset])
        self.assertIn("continue;", shader[guard:offset])

    def test_point_light_radius_guards_skip_work_and_use_explicit_shadow_lods(
        self,
    ) -> None:
        root = Path(__file__).resolve().parents[1]
        shader_dir = root / "assets" / "shaders"
        distance_names = {
            "world_surface.frag": "lightDistance",
            "world3d.frag": "distanceToLight",
            "gltf_player_model.frag": "distanceToLight",
            "material_weapon.frag": "distanceToLight",
            "instanced_color.frag": "lightDistance",
        }
        for shader_name, distance_name in distance_names.items():
            shader = (shader_dir / shader_name).read_text(encoding="utf-8")
            loop = shader.index("for (int index = 0; index <")
            distance = shader.index(
                f"float {distance_name} = length(offset);",
                loop,
            )
            guard = shader.index(
                f"if ({distance_name} >= radius) {{",
                distance,
            )
            source_radius = shader.index("float sourceRadius", guard)
            self.assertLess(loop, distance, shader_name)
            self.assertLess(distance, guard, shader_name)
            self.assertLess(guard, source_radius, shader_name)
            self.assertIn("continue;", shader[guard:source_radius], shader_name)

        point_shadow = (
            shader_dir / "includes" / "point_shadow.glsl"
        ).read_text(encoding="utf-8")
        self.assertNotIn("texture(pointShadowMap", point_shadow)
        self.assertGreaterEqual(point_shadow.count("textureLod("), 4)
        for shader_name in (
            "world_surface.frag",
            "gltf_player_model.frag",
            "material_weapon.frag",
            "instanced_color.frag",
        ):
            shader = (shader_dir / shader_name).read_text(encoding="utf-8")
            self.assertIn(
                "#extension GL_EXT_texture_shadow_lod : require",
                shader,
                shader_name,
            )
            self.assertIn('#include "includes/point_shadow.glsl"', shader)

    def test_point_light_facing_guards_skip_unused_shadow_work(self) -> None:
        root = Path(__file__).resolve().parents[1]
        shader_dir = root / "assets" / "shaders"
        facing_names = {
            "world_surface.frag": "localNDotL",
            "gltf_player_model.frag": "nDotL",
            "material_weapon.frag": "nDotL",
            "instanced_color.frag": "nDotL",
        }
        for shader_name, facing_name in facing_names.items():
            shader = (shader_dir / shader_name).read_text(encoding="utf-8")
            loop = shader.index("for (int index = 0; index <")
            facing = shader.index(f"float {facing_name} =", loop)
            guard = shader.index(f"if ({facing_name} <= 0.0) {{", facing)
            shadow = shader.find("pointShadowVisibility(", guard)
            radiance = shader.index("vec3 radiance", guard)
            self.assertLess(loop, facing, shader_name)
            self.assertLess(facing, guard, shader_name)
            self.assertGreater(shadow, guard, shader_name)
            self.assertLess(guard, radiance, shader_name)
            self.assertIn("continue;", shader[guard:radiance], shader_name)

        world3d = (shader_dir / "world3d.frag").read_text(encoding="utf-8")
        self.assertNotIn("if (nDotL <= 0.0)", world3d)
        self.assertIn("linearColor += sceneLights.colorIntensity[index].rgb", world3d)

    def test_sun_shadow_lookup_skips_back_facing_fragments(self) -> None:
        root = Path(__file__).resolve().parents[1]
        shader_dir = root / "assets" / "shaders"
        for shader_name, value_name in {
            "world_surface.frag": "sunVisibility",
            "gltf_player_model.frag": "shadow",
            "material_weapon.frag": "sunVisibility",
        }.items():
            shader = (shader_dir / shader_name).read_text(encoding="utf-8")
            facing = shader.index("float sunNDotL = max(dot(n, sunDirection), 0.0);")
            lookup = shader.index("sunShadowVisibility(worldPosition, n)", facing)
            assignment = shader.index(f"float {value_name} =", facing)
            assignment_end = shader.index(";", lookup) + 1
            self.assertLess(assignment, lookup, shader_name)
            self.assertIn("sunNDotL > 0.0", shader[assignment:assignment_end], shader_name)
            self.assertIn(": 1.0;", shader[assignment:assignment_end], shader_name)

    def test_world_shader_interfaces_drop_dead_varyings(self) -> None:
        root = Path(__file__).resolve().parents[1]
        shader_dir = root / "assets" / "shaders"
        world3d_vert = (shader_dir / "world3d.vert").read_text(encoding="utf-8")
        world3d_frag = (shader_dir / "world3d.frag").read_text(encoding="utf-8")
        surface_vert = (shader_dir / "world_surface.vert").read_text(encoding="utf-8")
        surface_frag = (shader_dir / "world_surface.frag").read_text(encoding="utf-8")
        for shader in (world3d_vert, world3d_frag):
            self.assertNotIn("worldNormal", shader)
            self.assertNotIn("materialSlot", shader)
        self.assertNotIn("flat out uint materialSlot", surface_vert)
        self.assertNotIn("flat in uint materialSlot", surface_frag)
        self.assertIn("worldNormal = inNormal", surface_vert)
        self.assertIn("layout(location = 4) in vec3 worldNormal", surface_frag)

    def test_depth_world_pipeline_uses_position_only_vertex_shader(self) -> None:
        root = Path(__file__).resolve().parents[1]
        renderer = (root / "src" / "render" / "Renderer.cpp").read_text(
            encoding="utf-8"
        )
        depth_call = renderer.index(
            "SDL_GPUGraphicsPipeline* depthWorldPipeline = createGpuPipeline3D"
        )
        call_end = renderer.index("SDL_GPUGraphicsPipeline* depthInstancedPipeline", depth_call)
        call = renderer[depth_call:call_end]
        self.assertIn('true,\n          "outline_mask_world.vert.spv"', call)
        self.assertTrue((root / "assets" / "shaders" / "outline_mask_world.vert.spv").is_file())

    def test_static_sun_shadow_cache_rejects_dynamic_casters(self) -> None:
        root = Path(__file__).resolve().parents[1]
        renderer = (root / "src" / "render" / "Renderer.cpp").read_text(
            encoding="utf-8"
        )
        self.assertIn("sunShadowCacheFingerprint(", renderer)
        static_only = renderer.index("const bool staticSunShadowOnly")
        cache = renderer.index("const bool sunShadowCacheMatches", static_only)
        self.assertIn("perspectiveScene.staticMeshBatches", renderer[static_only:cache])
        self.assertIn("perspectiveScene.gltfPlayerModelBatches.empty()", renderer[static_only:cache])
        self.assertIn("!sunShadowCacheMatches", renderer[cache:])

    def test_static_world_fingerprint_cache_uses_authoritative_map_revision(self) -> None:
        root = Path(__file__).resolve().parents[1]
        renderer = (root / "src" / "render" / "Renderer.cpp").read_text(
            encoding="utf-8"
        )
        game_app = (root / "src" / "app" / "GameApp.cpp").read_text(
            encoding="utf-8"
        )
        self.assertIn("settings.mapRevision", renderer)
        self.assertIn("mesh->arenaRevision == settings.mapRevision", renderer)
        self.assertIn("arenaStaticWorldFingerprint(arena)", renderer)
        self.assertIn("currentRenderSettings.mapRevision = currentMapRevision()", game_app)

    def test_point_shadow_selection_is_gated_when_quality_is_off(self) -> None:
        root = Path(__file__).resolve().parents[1]
        renderer = (root / "src" / "render" / "Renderer.cpp").read_text(
            encoding="utf-8"
        )
        selection = renderer.index("std::vector<LivePointLight> pointShadowLights")
        budget = renderer.index("const PointShadowPassPlan pointShadowBudget", selection)
        self.assertIn("if (settings.pointShadowQuality > 0)", renderer[selection:budget])
        self.assertIn("selectPointShadowLights(", renderer[selection:budget])

    def test_empty_point_light_candidates_skip_selection_without_dropping_combat_lights(self) -> None:
        root = Path(__file__).resolve().parents[1]
        scene = (root / "src" / "render" / "Scene3D.cpp").read_text(
            encoding="utf-8"
        )
        reserve = scene.index("lightCandidates.reserve(")
        selection = scene.index("scene.livePointLights = selectLivePointLights(")
        self.assertIn(
            "settings.pointLightQuality > 0 ? arena.staticLightCount : 0U",
            scene[reserve:selection],
        )
        self.assertIn("if (lightCandidates.empty())", scene[selection - 180:selection + 220])
        self.assertIn(
            "scene.temporaryLights.size()",
            scene[reserve:selection],
        )

    def test_point_shadow_cache_reuses_validated_world_fingerprint(self) -> None:
        root = Path(__file__).resolve().parents[1]
        renderer = (root / "src" / "render" / "Renderer.cpp").read_text(
            encoding="utf-8"
        )
        function = renderer.index("pointShadowCacheFingerprint(")
        function_end = renderer.index("SDL_GPUTexture* uploadRgbaTexture", function)
        signature = renderer[function:function_end]
        self.assertIn("std::uint64_t staticWorldFingerprint", signature)
        self.assertIn("std::uint64_t hash = staticWorldFingerprint;", signature)
        draw = renderer.index("std::vector<LivePointLight> pointShadowLights")
        resources = renderer.index(
            "const bool pointShadowResourcesReady",
            draw,
        )
        key = renderer.index("std::uint64_t desiredPointShadowCacheKey", draw)
        call = renderer.index("pointShadowCacheFingerprint(", key)
        call_end = renderer.index("const bool pointShadowCacheMatches", call)
        self.assertLess(resources, key)
        self.assertIn(
            "pointShadowBudget.lightCount > 0U && pointShadowResourcesReady",
            renderer[key:call_end],
        )
        self.assertIn("worldMesh->arenaFingerprint", renderer[key:call_end])
        self.assertNotIn("pointShadowCacheFingerprint(\n        arena", renderer[key:call_end])

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

    def test_sdl_native_outline_fallback_reaches_legacy_draw(self) -> None:
        root = Path(__file__).resolve().parents[1]
        renderer = (root / "src" / "render" / "Renderer.cpp").read_text(
            encoding="utf-8"
        )
        sdl_fallback_start = renderer.index(
            "const PlayerOutlinePathPlan sdlOutlinePath = "
            "buildPlayerOutlinePathPlan("
        )
        draw_start = renderer.index("  drawPerspectiveWorld(\n", sdl_fallback_start)
        draw_end = renderer.index(
            "\n  const PerspectiveCamera camera = playerPerspectiveCamera(",
            draw_start,
        )
        draw_call = renderer[draw_start:draw_end]
        self.assertIn("    *effectiveSdlSettings\n  );", draw_call)
        self.assertNotIn("    settings\n  );", draw_call)

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
        self.assertIn('$requiredShaderFiles', package)
        self.assertIn('Get-ChildItem (Join-Path $repoRoot "assets/shaders")', package)
        for shader_name in expected_inputs:
            self.assertEqual(4, cmake.count(f"{shader_name}.spv"), shader_name)

    def test_package_texture_filter_uses_importer_one_pass_normalization(self) -> None:
        root = Path(__file__).resolve().parents[1]
        package = (root / "scripts" / "package-windows.ps1").read_text(
            encoding="utf-8"
        )
        filter_start = package.index("function Test-MapMaterialRequiresTexture")
        filter_end = package.index("function Get-MapTextureMaterials", filter_start)
        material_filter = package[filter_start:filter_end]
        reader_start = filter_end
        reader_end = package.index("function Resolve-TextureMaterialPath", reader_start)
        material_reader = package[reader_start:reader_end]

        # The reader owns token normalization. Repeating it in the filter
        # would turn the importer's once-normalized /common/sky into common/sky.
        self.assertIn("$material = Normalize-TextureMaterial", material_reader)
        self.assertIn("(Test-MapMaterialRequiresTexture $material)", material_reader)
        self.assertNotIn("Normalize-TextureMaterial", material_filter)
        self.assertIn("return $NormalizedMaterial -notin @(", material_filter)

        excluded_materials = set(
            re.findall(r'^\s+"([^"]+)",?$', material_filter, re.MULTILINE)
        )
        self.assertSetEqual(
            {"common/sky", "common/playerclip", "common/clip", "common/weapclip"},
            excluded_materials,
        )

        def requires_texture(raw_material: str) -> bool:
            normalized = import_q3_map._normalize_material(raw_material)
            return normalized not in excluded_materials

        self.assertTrue(requires_texture("textures//common/sky"))
        for material in excluded_materials:
            with self.subTest(material=material):
                self.assertFalse(requires_texture(material))

    def test_sky_assets_stay_client_only(self) -> None:
        root = Path(__file__).resolve().parents[1]
        cmake = (root / "CMakeLists.txt").read_text(encoding="utf-8")
        package = (root / "scripts" / "package-windows.ps1").read_text(
            encoding="utf-8"
        )
        client_assets = cmake[
            cmake.index("add_custom_command(\n  TARGET lg_duel_client")
            : cmake.index("add_executable(lg_duel_server")
        ]
        shared_assets = cmake[
            cmake.index("add_custom_target(lg_duel_runtime_assets ALL")
            : cmake.index("add_dependencies(lg_duel_client")
        ]

        for shader_name in ("sky.vert", "sky.frag", "sky_direct.frag"):
            self.assertEqual(
                2,
                client_assets.count(f"{shader_name}.spv"),
                shader_name,
            )
            self.assertNotIn(shader_name, shared_assets)
        self.assertIn('$requiredShaderFiles', package)
        self.assertIn('Get-ChildItem (Join-Path $repoRoot "assets/shaders")', package)
        self.assertIn('"${CMAKE_CURRENT_SOURCE_DIR}/assets/sky"', client_assets)
        self.assertNotIn("assets/sky", shared_assets)
        for sky_name in ("aurora", "crimson-sunset"):
            for face in ("posx", "negx", "posy", "negy", "posz", "negz"):
                self.assertEqual(
                    1,
                    package.count(f"sky/{sky_name}/{face}.png"),
                )

        shader_dir = root / "assets" / "shaders"
        vertex_shader = (shader_dir / "sky.vert").read_text(encoding="utf-8")
        self.assertIn("vec4 right;", vertex_shader)
        self.assertIn("vec4 up;", vertex_shader)
        self.assertIn("vec4 forward;", vertex_shader)
        self.assertIn("vec4 projection;", vertex_shader)
        self.assertNotIn("camera.position", vertex_shader)
        for shader_name in ("sky.vert", "sky.frag", "sky_direct.frag"):
            self.assertEqual(
                b"\x03\x02#\x07",
                (shader_dir / f"{shader_name}.spv").read_bytes()[:4],
            )

        renderer = (root / "src" / "render" / "Renderer.cpp").read_text(
            encoding="utf-8"
        )
        pipeline_start = renderer.index("createGpuSkyPipeline(")
        pipeline_end = renderer.index(
            "[[nodiscard]] SDL_GPUGraphicsPipeline* createGpuPipeline(",
            pipeline_start,
        )
        pipeline = renderer[pipeline_start:pipeline_end]
        self.assertIn("colorTarget.blend_state.enable_blend = false;", pipeline)
        self.assertIn(
            "createInfo.multisample_state.sample_count = sampleCount;",
            pipeline,
        )
        self.assertIn(
            "createInfo.depth_stencil_state.enable_depth_test = false;",
            pipeline,
        )
        self.assertIn(
            "createInfo.depth_stencil_state.enable_depth_write = false;",
            pipeline,
        )
        pass_start = renderer.index(
            "SDL_GPURenderPass* worldPass = SDL_BeginGPURenderPass("
        )
        sky_draw = renderer.index(
            "SDL_DrawGPUPrimitives(worldPass, 3, 1, 0, 0);",
            pass_start,
        )
        first_world_uniform = renderer.index(
            "struct alignas(16) SceneLightUniform",
            pass_start,
        )
        self.assertLess(sky_draw, first_world_uniform)
        diagnostics_update = renderer.index(
            "diagnostics.skyDrawCalls = 1;",
            sky_draw,
        )
        self.assertLess(diagnostics_update, first_world_uniform)
        self.assertIn(
            "diagnostics.skyLoadedTextures = 1;",
            renderer[diagnostics_update:first_world_uniform],
        )
        app = (root / "src" / "app" / "GameApp.cpp").read_text(
            encoding="utf-8"
        )
        self.assertIn('"renderer_sky_draw_calls"', app)
        self.assertIn('"renderer_sky_loaded_textures"', app)
        fingerprint_start = renderer.index(
            "arenaStaticWorldFingerprint(const Arena& arena)"
        )
        fingerprint_end = renderer.index(
            "pointShadowCacheFingerprint(",
            fingerprint_start,
        )
        self.assertIn(
            "arenaSkySurfaceFingerprint(arena)",
            renderer[fingerprint_start:fingerprint_end],
        )

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
