from __future__ import annotations

import os
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import lg_launch
from lg_control import ControlError
from lg_launch import LaunchError


class LaunchTests(unittest.TestCase):
    def selection(self) -> dict:
        return {
            "source": "test-verified-benchmark",
            "icd_path": r"C:\verified\igvk64.json",
            "icd_sha256": "a" * 64,
            "icd_library_path": r"C:\verified\igvk64.dll",
            "gpu_name": "Intel Test GPU",
            "gpu_type": "PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU",
            "graphics_driver_name": "Intel Corporation",
            "graphics_driver_version": "101.9999",
            "vulkan_api_version": "1.4.999",
            "verification_state": "preflight-verified",
        }

    def status(self, renderer: str = lg_launch.GPU_RENDERER) -> dict:
        return {
            "renderer": renderer,
            "gpu_name": "Intel Test GPU",
            "graphics_driver_version": "101.9999",
            "vulkan_api_version": "1.4.999",
            "vulkan_icd_path": r"C:\verified\igvk64.json",
            "vulkan_icd_sha256": "a" * 64,
            "software_renderer": False,
            "benchmark_enabled": False,
        }

    def test_verified_gpu_startup_attaches_and_records_attestation(self) -> None:
        written = []
        with mock.patch.object(lg_launch, "resolve_vulkan_selection", return_value=self.selection()), \
             mock.patch.object(lg_launch, "send_request", return_value=self.status()), \
             mock.patch.object(lg_launch, "_read_state", return_value=None), \
             mock.patch.object(lg_launch, "_write_state", side_effect=written.append):
            result = lg_launch.ensure_client(renderer="gpu")
        self.assertTrue(result["gpu_verified"])
        self.assertEqual(result["gpu_verification_state"], "verified")
        self.assertEqual(written[0]["launch"]["actual_renderer"], lg_launch.GPU_RENDERER)

    def test_status_keeps_default_loader_environment_empty(self) -> None:
        launch = {
            **self.status(),
            "requested_renderer": "gpu",
            "vulkan_selection_source": "default-loader",
            "vulkan_driver_environment": {},
            "vulkan_icd_manifest_records": [{
                "path": r"C:\verified\igvk64.json",
                "library_path": r"C:\verified\igvk64.dll",
            }],
        }
        state = {"control_port": 27961, "launch": launch}
        with mock.patch.object(lg_launch, "send_request", return_value=self.status()), \
             mock.patch.object(lg_launch, "_read_state", return_value=state):
            result = lg_launch.status_with_state()
        self.assertEqual(result["vulkan_driver_environment"], {})
        self.assertEqual(
            result["vulkan_icd_manifest_records"][0]["library_path"],
            r"C:\verified\igvk64.dll",
        )

    def test_verified_gpu_startup_launches_owned_processes(self) -> None:
        class FakeProcess:
            def __init__(self, pid: int) -> None:
                self.pid = pid

            def poll(self):
                return None

        written = []
        with tempfile.TemporaryDirectory() as temporary:
            build = Path(temporary) / "build"
            state_dir = Path(temporary) / "state"
            build.mkdir()
            (build / "lg_duel_client.exe").touch()
            (build / "lg_duel_server.exe").touch()
            expected_server_path = str((build / "lg_duel_server.exe").resolve())
            with mock.patch.object(lg_launch, "STATE_DIR", state_dir), \
                 mock.patch.object(lg_launch, "resolve_vulkan_selection", return_value=self.selection()), \
                 mock.patch.object(
                     lg_launch, "send_request",
                     side_effect=[ControlError("offline"), self.status()],
                 ), \
                 mock.patch.object(lg_launch, "_existing_server_entry", return_value=None), \
                 mock.patch.object(
                     lg_launch, "_launch_process",
                     side_effect=[FakeProcess(301), FakeProcess(302)],
                 ) as launch, \
                 mock.patch.object(lg_launch, "_write_state", side_effect=written.append):
                result = lg_launch.ensure_client(renderer="gpu", timeout=1, build_dir=build)
        self.assertTrue(result["gpu_verified"])
        self.assertEqual(launch.call_count, 2)
        self.assertTrue(all(call.args[5] == build.resolve() for call in launch.call_args_list))
        self.assertEqual(result["build_directory"], str(build.resolve()))
        self.assertEqual(
            written[0]["server"],
            {"pid": 301, "owned": True, "path": expected_server_path},
        )
        self.assertEqual(written[0]["client"]["pid"], 302)
        self.assertTrue(written[0]["client"]["owned"])

    def test_benchmark_launch_removes_vulkan_driver_overrides(self) -> None:
        class FakeProcess:
            def __init__(self, pid: int) -> None:
                self.pid = pid

            def poll(self):
                return None

        with tempfile.TemporaryDirectory() as temporary:
            build = Path(temporary) / "build"
            state_dir = Path(temporary) / "state"
            build.mkdir()
            (build / "lg_duel_client.exe").touch()
            (build / "lg_duel_server.exe").touch()
            status = self.status()
            status["benchmark_enabled"] = True
            with mock.patch.dict(
                os.environ,
                {
                    "VK_DRIVER_FILES": r"C:\inherited\driver.json",
                    "VK_ICD_FILENAMES": r"C:\inherited\legacy.json",
                },
                clear=False,
            ), mock.patch.object(
                lg_launch, "STATE_DIR", state_dir
            ), mock.patch.object(
                lg_launch, "_probe_default_vulkan", return_value=self.selection()
            ), mock.patch.object(
                lg_launch, "send_request", side_effect=[ControlError("offline"), status]
            ), mock.patch.object(
                lg_launch, "_existing_server_entry", return_value=None
            ), mock.patch.object(
                lg_launch, "_launch_process",
                side_effect=[FakeProcess(301), FakeProcess(302)],
            ) as launch, mock.patch.object(
                lg_launch, "_write_state"
            ):
                lg_launch.ensure_client(
                    renderer="gpu", benchmark=True, timeout=1, build_dir=build
                )

        for call in launch.call_args_list:
            environment = call.args[4]
            self.assertNotIn("VK_DRIVER_FILES", environment)
            self.assertNotIn("VK_ICD_FILENAMES", environment)

    def test_external_server_launch_owns_only_client(self) -> None:
        class FakeProcess:
            pid = 302

            def poll(self):
                return None

        written = []
        with tempfile.TemporaryDirectory() as temporary:
            build = Path(temporary) / "build"
            state_dir = Path(temporary) / "state"
            build.mkdir()
            (build / "lg_duel_client.exe").touch()
            with mock.patch.object(lg_launch, "BUILD_DIR", build), \
                 mock.patch.object(lg_launch, "STATE_DIR", state_dir), \
                 mock.patch.object(lg_launch, "resolve_vulkan_selection", return_value=self.selection()), \
                 mock.patch.object(
                     lg_launch, "send_request",
                     side_effect=[ControlError("offline"), self.status()],
                 ), \
                 mock.patch.object(lg_launch, "_existing_server_entry", return_value=None), \
                 mock.patch.object(lg_launch, "_launch_process", return_value=FakeProcess()) as launch, \
                 mock.patch.object(lg_launch, "_write_state", side_effect=written.append):
                result = lg_launch.ensure_client(renderer="gpu", manage_server=False, timeout=1)
        self.assertTrue(result["gpu_verified"])
        launch.assert_called_once()
        self.assertFalse(written[0]["server"]["owned"])
        self.assertEqual(written[0]["server"]["pid"], 0)
        self.assertTrue(written[0]["client"]["owned"])

    def test_vulkan_failure_includes_selected_icd(self) -> None:
        failed = subprocess.CompletedProcess(
            ["vulkaninfo", "--summary"], 1, stdout="", stderr="VK_ERROR_INCOMPATIBLE_DRIVER"
        )
        with self.assertRaisesRegex(LaunchError, r"igvk64\.json.*VK_ERROR_INCOMPATIBLE_DRIVER"):
            lg_launch._probe_vulkan(
                lg_launch.Path(r"C:\verified\igvk64.json"), runner=mock.Mock(return_value=failed)
            )

    def test_default_vulkan_probe_removes_overrides_and_finds_active_icd(self) -> None:
        summary = """Devices:
========
GPU0:
    apiVersion = 1.4.323
    driverVersion = 101.7026
    deviceType = PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU
    deviceName = Intel(R) Arc(TM) Test GPU
    driverName = Intel Corporation
    driverInfo = 101.7026
"""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            manifest = root / "igvk64.json"
            library = root / "igvk64.dll"
            manifest.write_text(
                '{"ICD":{"library_path":".\\\\igvk64.dll"}}', encoding="utf-8"
            )
            library.touch()
            loader = (
                f"Found ICD manifest file {manifest}, version 1.0.0\n"
                f'Using "Intel(R) Arc(TM) Test GPU" with driver: "{library}"\n'
            )

            def run(*args, **kwargs):
                environment = kwargs["env"]
                self.assertNotIn("VK_DRIVER_FILES", environment)
                self.assertNotIn("VK_ICD_FILENAMES", environment)
                self.assertNotIn("VK_ADD_DRIVER_FILES", environment)
                return subprocess.CompletedProcess(
                    ["vulkaninfo", "--summary"], 0, stdout=summary, stderr=loader
                )

            with mock.patch.dict(
                os.environ,
                {
                    "VK_DRIVER_FILES": "driver.json",
                    "VK_ICD_FILENAMES": "legacy.json",
                    "VK_ADD_DRIVER_FILES": "extra.json",
                },
                clear=False,
            ):
                selection = lg_launch._probe_default_vulkan(runner=run)

        self.assertEqual(selection["source"], "default-loader")
        self.assertEqual(selection["icd_path"], str(manifest.resolve()))
        self.assertEqual(selection["vulkan_driver_environment"], {})

    def test_silent_fallback_is_rejected(self) -> None:
        for renderer in ("SDL_Renderer/direct3d11", "SDL_Renderer/software", "SwiftShader"):
            with self.subTest(renderer=renderer), self.assertRaisesRegex(LaunchError, "fallback"):
                lg_launch.verify_control_status(
                    self.status(renderer), requested_renderer="gpu", selection=self.selection()
                )

    def test_existing_fallback_attachment_is_rejected_without_launching(self) -> None:
        state = {
            "client": {"pid": 10, "owned": True, "path": "client.exe"},
            "server": {"pid": 11, "owned": True, "path": "server.exe"},
        }
        with mock.patch.object(lg_launch, "resolve_vulkan_selection", return_value=self.selection()), \
             mock.patch.object(lg_launch, "send_request", return_value=self.status("SDL_Renderer/direct3d11")), \
             mock.patch.object(lg_launch, "_read_state", return_value=state), \
             mock.patch.object(lg_launch, "STATE_PATH", mock.Mock(exists=mock.Mock(return_value=False))), \
             mock.patch.object(lg_launch, "cleanup_owned", return_value=["client", "server"]) as cleanup, \
             mock.patch.object(lg_launch, "_launch_process") as launch:
            with self.assertRaisesRegex(LaunchError, r"GPU renderer required[\s\S]*Selected ICD"):
                lg_launch.ensure_client(renderer="gpu")
        cleanup.assert_called_once_with(state)
        launch.assert_not_called()

    def test_stale_verified_state_cannot_mask_fallback_status(self) -> None:
        launch = {
            **self.status(),
            "requested_renderer": "gpu",
            "vulkan_selection_source": "test",
        }
        state = {"control_port": 27961, "launch": launch}
        with mock.patch.object(lg_launch, "send_request", return_value=self.status("SDL_Renderer/direct3d11")), \
             mock.patch.object(lg_launch, "_read_state", return_value=state):
            result = lg_launch.status_with_state()
        self.assertEqual(result["renderer"], "SDL_Renderer/direct3d11")
        self.assertFalse(result["gpu_verified"])
        self.assertEqual(result["gpu_verification_state"], "stored-attestation-mismatch")

    def test_explicit_fallback_opt_in_is_accepted(self) -> None:
        with mock.patch.object(lg_launch, "send_request", return_value=self.status("SDL_Renderer/direct3d11")), \
             mock.patch.object(lg_launch, "_read_state", return_value=None), \
             mock.patch.object(lg_launch, "_write_state"):
            result = lg_launch.ensure_client(renderer="fallback", allow_fallback=True)
        self.assertEqual(result["gpu_verification_state"], "explicit-fallback")
        self.assertFalse(result["gpu_verified"])

    def test_cleanup_terminates_only_owned_matching_processes(self) -> None:
        state = {
            "client": {"pid": 101, "owned": True, "path": "client.exe"},
            "server": {"pid": 202, "owned": False, "path": "server.exe"},
        }
        with mock.patch.object(lg_launch, "_entry_matches", side_effect=[True, False]), \
             mock.patch.object(os, "kill") as kill:
            stopped = lg_launch.cleanup_owned(state)
        self.assertEqual(stopped, ["client"])
        kill.assert_called_once_with(101, lg_launch.signal.SIGTERM)

    def test_stop_owned_keeps_state_when_one_owned_process_remains(self) -> None:
        state = {
            "client": {"pid": 101, "owned": True, "path": "client.exe"},
            "server": {"pid": 202, "owned": True, "path": "server.exe"},
        }
        state_path = mock.Mock()
        state_path.exists.return_value = True
        with mock.patch.object(lg_launch, "_read_state", return_value=state), \
             mock.patch.object(lg_launch, "cleanup_owned", return_value=["client"]), \
             mock.patch.object(lg_launch, "_entry_matches", side_effect=[False, True]), \
             mock.patch.object(lg_launch, "STATE_PATH", state_path):
            result = lg_launch.stop_owned()
        self.assertTrue(result["left_owned_running"])
        self.assertEqual(result["remaining"], ["server"])
        state_path.unlink.assert_not_called()

    def test_restart_requires_an_owned_matching_client(self) -> None:
        state = {"client": {"pid": 101, "owned": False, "path": "client.exe"}}
        with mock.patch.object(lg_launch, "_read_state", return_value=state), \
             self.assertRaisesRegex(LaunchError, "owned"):
            lg_launch.restart_owned()

    def test_restart_keeps_external_server_ownership_boundary(self) -> None:
        state = {
            "client": {"pid": 101, "owned": True, "path": "client.exe"},
            "server": {"pid": 202, "owned": False, "path": "server.exe"},
            "server_port": 28060,
            "control_port": 28061,
        }
        status = {"renderer": lg_launch.GPU_RENDERER}
        with mock.patch.object(lg_launch, "_read_state", return_value=state), \
             mock.patch.object(lg_launch, "_entry_matches", side_effect=[True, False, False]), \
             mock.patch.object(lg_launch, "cleanup_owned", return_value=["client"]), \
             mock.patch.object(lg_launch, "STATE_PATH", mock.Mock(exists=mock.Mock(return_value=False))), \
             mock.patch.object(lg_launch, "ensure_client", return_value=status) as ensure:
            result = lg_launch.restart_owned(renderer="gpu", timeout=3)
        self.assertEqual(result, {"stopped": ["client"], "status": status})
        ensure.assert_called_once_with(
            renderer="gpu", allow_fallback=False, benchmark=False, manage_server=False,
            server_port=28060, control_port=28061, timeout=3,
        )

    def test_restart_cli_exposes_renderer_and_timeout(self) -> None:
        arguments = lg_launch.build_parser().parse_args(
            ["restart", "--renderer", "fallback", "--allow-fallback", "--timeout", "4"]
        )
        self.assertEqual(arguments.action, "restart")
        self.assertEqual(arguments.renderer, "fallback")
        self.assertTrue(arguments.allow_fallback)
        self.assertEqual(arguments.timeout, 4.0)

    def scenario_files(self, root: Path) -> tuple[Path, Path, Path]:
        build = root / "build"
        build.mkdir()
        client = build / "lg_duel_client.exe"
        server = build / "lg_duel_server.exe"
        scenario = root / "scenario.json"
        client.write_bytes(b"client")
        server.write_bytes(b"server")
        scenario.write_text("{}", encoding="utf-8")
        return build, scenario, root / "run"

    def test_scenario_launch_owns_both_processes_and_records_identity(self) -> None:
        class FakeProcess:
            def __init__(self, pid: int) -> None:
                self.pid = pid

            def poll(self):
                return None

        with tempfile.TemporaryDirectory() as temporary:
            build, scenario, run_dir = self.scenario_files(Path(temporary))
            with mock.patch.object(
                lg_launch, "send_request",
                side_effect=[ControlError("offline"), self.status()],
            ), mock.patch.object(
                lg_launch, "_control_endpoint_listening", return_value=False
            ), mock.patch.object(
                lg_launch, "resolve_vulkan_selection", return_value=self.selection()
            ), mock.patch.object(
                lg_launch, "_process_creation_time", side_effect=[1001, 1002]
            ), mock.patch.object(
                lg_launch, "_launch_process",
                side_effect=[FakeProcess(301), FakeProcess(302)],
            ) as launch:
                result = lg_launch.launch_scenario_session(
                    scenario, run_dir, "run-7", 28060, 28061, build_dir=build
                )

        self.assertEqual(result["server"]["run_token"], "run-7")
        self.assertEqual(result["client"]["creation_time"], 1002)
        self.assertTrue(result["server"]["owned"])
        self.assertTrue(result["client"]["owned"])
        self.assertTrue(result["status"]["gpu_verified"])
        self.assertEqual(len(result["binary_sha256"]["client"]), 64)
        self.assertEqual(launch.call_args_list[0].args[1], [
            "28060", "--live-scenario", str(scenario.resolve()),
            "--scenario-run-dir", str(run_dir.resolve()), "--scenario-token", "run-7",
        ])
        self.assertEqual(launch.call_args_list[1].args[1], [
            "127.0.0.1", "28060", "--dev-control", "--control-port", "28061",
        ])

    def test_scenario_launch_rejects_any_listening_control_endpoint(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            build, scenario, run_dir = self.scenario_files(Path(temporary))
            with mock.patch.object(
                lg_launch, "send_request", side_effect=ControlError("not dev control")
            ), mock.patch.object(
                lg_launch, "_control_endpoint_listening", return_value=True
            ), mock.patch.object(lg_launch, "_launch_process") as launch:
                with self.assertRaisesRegex(LaunchError, "already listening"):
                    lg_launch.launch_scenario_session(
                        scenario, run_dir, "run-8", 28060, 28061,
                        renderer="fallback", allow_fallback=True, build_dir=build,
                    )
        launch.assert_not_called()

    def test_scenario_launch_rejects_missing_build_before_launch(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            scenario = root / "scenario.json"
            scenario.write_text("{}", encoding="utf-8")
            with mock.patch.object(lg_launch, "_launch_process") as launch:
                with self.assertRaisesRegex(LaunchError, "unavailable"):
                    lg_launch.launch_scenario_session(
                        scenario, root / "run", "run-9", 28060, 28061,
                        renderer="fallback", allow_fallback=True, build_dir=root / "missing",
                    )
        launch.assert_not_called()

    def test_scenario_launch_failure_cleans_started_server_and_includes_logs(self) -> None:
        class FakeProcess:
            pid = 301

            def __init__(self) -> None:
                self.terminated = False

            def poll(self):
                return None

            def terminate(self):
                self.terminated = True

            def wait(self, timeout):
                return 0

        server_process = FakeProcess()
        cleanup_result = {"stopped": ["server"], "failures": []}
        with tempfile.TemporaryDirectory() as temporary:
            build, scenario, run_dir = self.scenario_files(Path(temporary))
            with mock.patch.object(
                lg_launch, "send_request", side_effect=ControlError("offline")
            ), mock.patch.object(
                lg_launch, "_control_endpoint_listening", return_value=False
            ), mock.patch.object(
                lg_launch, "_process_creation_time", return_value=1001
            ), mock.patch.object(
                lg_launch, "_launch_process",
                side_effect=[server_process, OSError("client failed")],
            ), mock.patch.object(
                lg_launch, "cleanup_scenario_session", return_value=cleanup_result
            ) as cleanup:
                with self.assertRaisesRegex(LaunchError, r"client failed[\s\S]*server"):
                    lg_launch.launch_scenario_session(
                        scenario, run_dir, "run-10", 28060, 28061,
                        renderer="fallback", allow_fallback=True, build_dir=build,
                    )
        cleanup.assert_called_once()
        self.assertTrue(server_process.terminated)

    def test_scenario_cleanup_stops_only_owned_matching_processes(self) -> None:
        state = {
            "cleanup_timeout": 0.1,
            "run_token": "run-11",
            "client": {"pid": 101, "owned": True, "path": "client.exe",
                       "creation_time": 1, "run_token": "run-11"},
            "server": {"pid": 202, "owned": False, "path": "server.exe",
                       "creation_time": 2, "run_token": "run-11"},
        }
        with mock.patch.object(
            lg_launch, "_scenario_entry_matches", side_effect=[True, False]
        ), mock.patch.object(os, "kill") as kill:
            result = lg_launch.cleanup_scenario_session(state)
        self.assertEqual(
            result,
            {"stopped": ["client"], "already_exited": [], "failures": []},
        )
        termination_calls = [
            call
            for call in kill.call_args_list
            if call.args[1] != 0
        ]
        self.assertEqual(
            termination_calls,
            [mock.call(101, lg_launch.signal.SIGTERM)],
        )

    def test_scenario_cleanup_reports_identity_failure_without_killing(self) -> None:
        state = {
            "run_token": "run-12",
            "client": {"pid": 101, "owned": True, "path": "client.exe",
                       "creation_time": 1, "run_token": "run-12"},
        }
        with mock.patch.object(
            lg_launch, "_scenario_entry_matches", return_value=False
        ), mock.patch.object(
            lg_launch, "_process_running", return_value=True
        ), mock.patch.object(os, "kill") as kill:
            result = lg_launch.cleanup_scenario_session(state)
        self.assertEqual(result["stopped"], [])
        self.assertRegex(result["failures"][0], "identity")
        kill.assert_not_called()

    def test_scenario_cleanup_accepts_owned_process_that_already_exited(self) -> None:
        state = {
            "run_token": "run-13",
            "server": {"pid": 202, "owned": True, "path": "server.exe",
                       "creation_time": 2, "run_token": "run-13"},
        }
        with mock.patch.object(
            lg_launch, "_scenario_entry_matches", return_value=False
        ), mock.patch.object(
            lg_launch, "_process_running", return_value=False
        ), mock.patch.object(os, "kill") as kill:
            result = lg_launch.cleanup_scenario_session(state)
        self.assertEqual(result["already_exited"], ["server"])
        self.assertEqual(result["failures"], [])
        kill.assert_not_called()

    def test_scenario_cleanup_accepts_exit_race_during_signal(self) -> None:
        state = {
            "run_token": "run-13b",
            "server": {
                "pid": 203,
                "owned": True,
                "path": "server.exe",
                "creation_time": 2,
                "run_token": "run-13b",
            },
        }
        with mock.patch.object(
            lg_launch,
            "_scenario_entry_matches",
            return_value=True,
        ), mock.patch.object(
            lg_launch,
            "_process_running",
            return_value=False,
        ), mock.patch.object(
            os,
            "kill",
            side_effect=PermissionError("process exited"),
        ):
            result = lg_launch.cleanup_scenario_session(state)
        self.assertEqual(result["already_exited"], ["server"])
        self.assertEqual(result["failures"], [])

    def test_scenario_cleanup_stops_tracking_after_process_exits(self) -> None:
        state = {
            "cleanup_timeout": 1.0,
            "run_token": "run-14",
            "client": {
                "pid": 101,
                "owned": True,
                "path": "client.exe",
                "creation_time": 1,
                "run_token": "run-14",
            },
        }
        with mock.patch.object(
            lg_launch, "_scenario_entry_matches", return_value=True
        ), mock.patch.object(
            lg_launch, "_process_running", side_effect=[True, False]
        ), mock.patch.object(os, "kill"):
            result = lg_launch.cleanup_scenario_session(state)
        self.assertEqual(result["stopped"], ["client"])
        self.assertEqual(result["failures"], [])


if __name__ == "__main__":
    unittest.main()
