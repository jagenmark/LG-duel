from __future__ import annotations

import contextlib
import json
import os
import subprocess
import sys
import tempfile
import threading
import unittest
from pathlib import Path
from unittest import mock

import lg_launch
from lg_control import ControlError
from lg_launch import LaunchError


class LaunchTests(unittest.TestCase):
    def test_executable_names_follow_platform(self) -> None:
        with mock.patch.object(lg_launch.platform, "system", return_value="Linux"):
            self.assertEqual(lg_launch._executable_name("lg_duel_client"), "lg_duel_client")
        with mock.patch.object(lg_launch.platform, "system", return_value="Windows"):
            self.assertEqual(lg_launch._executable_name("lg_duel_client"), "lg_duel_client.exe")

    def test_executable_path_accepts_existing_cross_platform_fixture(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            build = Path(temporary)
            fixture = build / "lg_duel_client.exe"
            fixture.touch()
            with mock.patch.object(lg_launch.platform, "system", return_value="Linux"):
                self.assertEqual(
                    lg_launch._executable_path(build, "lg_duel_client"), fixture
                )

    def test_launch_process_new_session_is_opt_in(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            executable = root / "game"
            executable.touch()
            process = mock.Mock()
            with mock.patch.object(lg_launch.os, "name", "posix"), \
                 mock.patch.object(
                     lg_launch.subprocess, "Popen", return_value=process
                 ) as popen:
                result = lg_launch._launch_process(
                    executable,
                    ["--test"],
                    root / "stdout.log",
                    root / "stderr.log",
                    {},
                    root,
                )
                default_call = popen.call_args
                lg_launch._launch_process(
                    executable,
                    ["--test"],
                    root / "stdout.log",
                    root / "stderr.log",
                    {},
                    root,
                    survive_parent_exit=True,
                )
                detached_call = popen.call_args

        self.assertIs(result, process)
        self.assertFalse(default_call.kwargs["start_new_session"])
        self.assertTrue(detached_call.kwargs["start_new_session"])

    def setUp(self) -> None:
        self.real_lifecycle_lock = lg_launch._lifecycle_lock
        lifecycle_patch = mock.patch.object(
            lg_launch,
            "_lifecycle_lock",
            side_effect=lambda: contextlib.nullcontext(),
        )
        lifecycle_patch.start()
        self.addCleanup(lifecycle_patch.stop)

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
            "control_protocol": 1,
            "client_running": True,
            "server_running": True,
            "connected": True,
            "map": "eyetoeye",
            "map_revision": 1,
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
        state = {
            "phase": "starting",
            "control_port": 27961,
            "client": {
                "pid": 10, "owned": True, "path": "client.exe",
                "creation_time": 1001,
            },
            "server": {"pid": 0, "owned": False, "path": ""},
        }
        with mock.patch.object(lg_launch, "resolve_vulkan_selection", return_value=self.selection()), \
             mock.patch.object(lg_launch, "send_request", return_value=self.status()), \
             mock.patch.object(lg_launch, "_read_state", return_value=state), \
             mock.patch.object(lg_launch, "_entry_matches", return_value=True), \
             mock.patch.object(lg_launch, "_write_state", side_effect=written.append):
            result = lg_launch.ensure_client(renderer="gpu")
        self.assertTrue(result["gpu_verified"])
        self.assertEqual(result["gpu_verification_state"], "verified")
        self.assertTrue(result["process_state_updated"])
        self.assertEqual(written[0]["launch"]["actual_renderer"], lg_launch.GPU_RENDERER)

    def test_active_unverified_attachment_preserves_unrelated_state(self) -> None:
        old_state = {
            "phase": "ready",
            "control_port": 28061,
            "client": {
                "pid": 77, "owned": True, "path": "other-client.exe",
                "creation_time": 7001,
            },
            "launch": {"marker": "other-session"},
        }
        before = json.loads(json.dumps(old_state))
        with mock.patch.object(
            lg_launch, "send_request",
            return_value=self.status("SDL_Renderer/direct3d11"),
        ), mock.patch.object(
            lg_launch, "_read_state", return_value=old_state
        ), mock.patch.object(
            lg_launch, "_write_state"
        ) as write:
            result = lg_launch.ensure_client(
                renderer="fallback", allow_fallback=True
            )
        self.assertFalse(result["process_state_updated"])
        self.assertEqual(
            result["process_state_verification"], "unverified-attachment"
        )
        self.assertEqual(old_state, before)
        write.assert_not_called()

    def test_normal_control_does_not_attach_to_benchmark_session(self) -> None:
        status = self.status()
        status["benchmark_enabled"] = True
        with mock.patch.object(
            lg_launch, "send_request", return_value=status
        ), mock.patch.object(
            lg_launch, "resolve_vulkan_selection"
        ) as resolve_vulkan, mock.patch.object(
            lg_launch, "_read_state"
        ) as read_state, mock.patch.object(
            lg_launch, "_write_state"
        ) as write_state:
            with self.assertRaisesRegex(
                LaunchError, "reserved for benchmarking"
            ):
                lg_launch.ensure_client(renderer="gpu")
        resolve_vulkan.assert_not_called()
        read_state.assert_not_called()
        write_state.assert_not_called()

    def test_failed_probe_preserves_saved_live_benchmark_session(self) -> None:
        state = {
            "phase": "ready",
            "benchmark": True,
            "control_port": 27961,
            "client": {
                "pid": 101,
                "owned": True,
                "path": "client.exe",
                "creation_time": 1001,
            },
            "server": {
                "pid": 202,
                "owned": True,
                "path": "server.exe",
                "creation_time": 2002,
            },
        }
        with mock.patch.object(
            lg_launch, "resolve_vulkan_selection",
            return_value=self.selection(),
        ), mock.patch.object(
            lg_launch, "send_request", side_effect=ControlError("offline")
        ), mock.patch.object(
            lg_launch, "_read_state", return_value=state
        ), mock.patch.object(
            lg_launch, "_matching_owned_processes",
            return_value=["client", "server"],
        ), mock.patch.object(
            lg_launch, "_unverified_live_owned_processes", return_value=[]
        ), mock.patch.object(
            lg_launch, "cleanup_owned"
        ) as cleanup, mock.patch.object(
            lg_launch, "_launch_process"
        ) as launch, mock.patch.object(
            lg_launch, "_write_state"
        ) as write:
            with self.assertRaisesRegex(
                LaunchError, "live benchmark session"
            ):
                lg_launch.ensure_client(renderer="gpu")
        cleanup.assert_not_called()
        launch.assert_not_called()
        write.assert_not_called()

    def test_readiness_polls_empty_renderer_and_attestation(self) -> None:
        incomplete = self.status()
        incomplete.update({
            "renderer": "",
            "gpu_name": "",
            "graphics_driver_version": "",
            "vulkan_api_version": "",
            "vulkan_icd_path": "",
            "vulkan_icd_sha256": "",
            "software_renderer": None,
        })
        with mock.patch.object(
            lg_launch, "send_request", return_value=self.status()
        ) as sender:
            result = lg_launch._wait_for_ready_status(
                initial_status=incomplete,
                deadline=lg_launch.time.monotonic() + 1.0,
                control_port=27961,
                renderer="gpu",
                selection=self.selection(),
                allow_fallback=False,
                benchmark=False,
            )
        self.assertTrue(result["gpu_verified"])
        sender.assert_called_once()

    def test_readiness_fails_promptly_on_named_wrong_renderer(self) -> None:
        wrong = self.status("SDL_Renderer/direct3d11")
        wrong["connected"] = False
        with mock.patch.object(lg_launch, "send_request") as sender:
            with self.assertRaisesRegex(LaunchError, "GPU renderer required"):
                lg_launch._wait_for_ready_status(
                    initial_status=wrong,
                    deadline=lg_launch.time.monotonic() + 1.0,
                    control_port=27961,
                    renderer="gpu",
                    selection=self.selection(),
                    allow_fallback=False,
                    benchmark=False,
                )
        sender.assert_not_called()

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
        state = {
            "phase": "ready",
            "control_port": 27961,
            "client": {"pid": 10, "owned": True, "path": "client.exe"},
            "launch": launch,
        }
        with mock.patch.object(lg_launch, "send_request", return_value=self.status()), \
             mock.patch.object(lg_launch, "_read_state", return_value=state), \
             mock.patch.object(lg_launch, "_entry_matches", return_value=True):
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
                 mock.patch.object(lg_launch, "_read_state", return_value=None), \
                 mock.patch.object(lg_launch, "_existing_server_entry", return_value=None), \
                 mock.patch.object(
                     lg_launch, "_launch_process",
                     side_effect=[FakeProcess(301), FakeProcess(302)],
                 ) as launch, \
                 mock.patch.object(
                     lg_launch, "_process_creation_time",
                     side_effect=[1001, 1002],
                 ), \
                 mock.patch.object(
                     lg_launch, "_write_state",
                     side_effect=lambda value: written.append(
                         json.loads(json.dumps(value))
                     ),
                 ):
                result = lg_launch.ensure_client(renderer="gpu", timeout=1, build_dir=build)
        self.assertTrue(result["gpu_verified"])
        self.assertEqual(launch.call_count, 2)
        self.assertTrue(all(call.args[5] == build.resolve() for call in launch.call_args_list))
        self.assertTrue(all(
            call.kwargs["survive_parent_exit"] for call in launch.call_args_list
        ))
        self.assertEqual(result["build_directory"], str(build.resolve()))
        self.assertFalse(written[0]["server"]["owned"])
        self.assertTrue(written[0]["server"]["pending_launch"])
        self.assertFalse(written[0]["client"]["owned"])
        self.assertTrue(written[0]["client"]["pending_launch"])
        self.assertEqual(
            written[1]["server"],
            {
                "pid": 301, "owned": True, "path": expected_server_path,
                "creation_time": 1001,
            },
        )
        self.assertFalse(written[1]["client"]["owned"])
        self.assertEqual(written[2]["client"]["pid"], 302)
        self.assertEqual(written[0]["phase"], "starting")
        self.assertEqual(written[1]["phase"], "starting")
        self.assertEqual(written[2]["phase"], "starting")
        self.assertEqual(written[-1]["phase"], "ready")
        self.assertTrue(written[2]["client"]["owned"])

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
                lg_launch, "_read_state", return_value=None
            ), mock.patch.object(
                lg_launch, "_existing_server_entry", return_value=None
            ), mock.patch.object(
                lg_launch, "_launch_process",
                side_effect=[FakeProcess(301), FakeProcess(302)],
            ) as launch, mock.patch.object(
                lg_launch, "_process_creation_time", side_effect=[1001, 1002]
            ), mock.patch.object(
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
                 mock.patch.object(lg_launch, "_read_state", return_value=None), \
                 mock.patch.object(lg_launch, "_existing_server_entry", return_value=None), \
                 mock.patch.object(lg_launch, "_launch_process", return_value=FakeProcess()) as launch, \
                 mock.patch.object(lg_launch, "_process_creation_time", return_value=1002), \
                 mock.patch.object(lg_launch, "_write_state", side_effect=written.append):
                result = lg_launch.ensure_client(renderer="gpu", manage_server=False, timeout=1)
        self.assertTrue(result["gpu_verified"])
        launch.assert_called_once()
        self.assertFalse(written[0]["server"]["owned"])
        self.assertEqual(written[0]["server"]["pid"], 0)
        self.assertTrue(written[0]["client"]["pending_launch"])
        self.assertTrue(written[1]["client"]["owned"])

    def test_vulkan_failure_includes_selected_icd(self) -> None:
        failed = subprocess.CompletedProcess(
            ["vulkaninfo", "--summary"], 1, stdout="", stderr="VK_ERROR_INCOMPATIBLE_DRIVER"
        )
        with self.assertRaisesRegex(LaunchError, r"igvk64\.json.*VK_ERROR_INCOMPATIBLE_DRIVER"):
            lg_launch._probe_vulkan(
                lg_launch.Path(r"C:\verified\igvk64.json"), runner=mock.Mock(return_value=failed)
            )

    def test_resolve_selection_keeps_loader_resolved_library_path(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            manifest = root / "intel_icd.json"
            manifest.write_text(
                '{"ICD":{"library_path":"libvulkan_intel.so"}}', encoding="utf-8"
            )
            selection = {
                **self.selection(),
                "icd_path": str(manifest),
                "icd_sha256": lg_launch._sha256(manifest),
                "icd_library_path": "/usr/lib/x86_64-linux-gnu/libvulkan_intel.so",
            }
            with mock.patch.object(
                lg_launch, "discover_vulkan_selection", return_value=selection
            ), mock.patch.object(
                lg_launch,
                "_probe_vulkan",
                return_value={
                    "gpu_name": selection["gpu_name"],
                    "gpu_type": selection["gpu_type"],
                    "graphics_driver_name": selection["graphics_driver_name"],
                    "graphics_driver_version": selection["graphics_driver_version"],
                    "vulkan_api_version": selection["vulkan_api_version"],
                    "software_renderer": False,
                },
            ):
                resolved = lg_launch.resolve_vulkan_selection()

        self.assertEqual(
            resolved["icd_library_path"],
            "/usr/lib/x86_64-linux-gnu/libvulkan_intel.so",
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
        self.assertEqual(selection["graphics_driver_version"], "101.7026")
        self.assertEqual(selection["graphics_driver_info"], "101.7026")

    def test_linux_default_probe_matches_selected_gpu_to_bare_manifest_library(self) -> None:
        summary = """Devices:
========
GPU0:
    apiVersion = 1.4.335
    driverVersion = 26.0.3
    deviceType = PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU
    deviceName = Intel(R) Graphics (LNL)
    driverName = Intel open-source Mesa driver
    driverInfo = Mesa 26.0.3
GPU1:
    apiVersion = 1.4.335
    driverVersion = 26.0.3
    deviceType = PHYSICAL_DEVICE_TYPE_CPU
    deviceName = llvmpipe
    driverName = llvmpipe
    driverInfo = Mesa 26.0.3
"""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            intel_manifest = root / "intel_icd.json"
            software_manifest = root / "lvp_icd.json"
            intel_manifest.write_text(
                '{"ICD":{"library_path":"libvulkan_intel.so"}}', encoding="utf-8"
            )
            software_manifest.write_text(
                '{"ICD":{"library_path":"libvulkan_lvp.so"}}', encoding="utf-8"
            )
            loader = (
                f"Found ICD manifest file {intel_manifest}, version 1.0.1\n"
                f"Found ICD manifest file {software_manifest}, version 1.0.1\n"
                'Using "Intel(R) Graphics (LNL)" with driver: '
                '"/usr/lib/x86_64-linux-gnu/libvulkan_intel.so"\n'
                'Using "llvmpipe" with driver: '
                '"/usr/lib/x86_64-linux-gnu/libvulkan_lvp.so"\n'
            )
            completed = subprocess.CompletedProcess(
                ["vulkaninfo", "--summary"], 0, stdout=summary, stderr=loader
            )
            selection = lg_launch._probe_default_vulkan(
                runner=mock.Mock(return_value=completed)
            )

        self.assertEqual(selection["icd_path"], str(intel_manifest.resolve()))
        self.assertEqual(
            selection["icd_library_path"],
            "/usr/lib/x86_64-linux-gnu/libvulkan_intel.so",
        )

    def test_fresh_linux_worktree_uses_default_loader(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            state_dir = root / "build" / "dev-control"
            loader_selection = {
                **self.selection(),
                "source": "default-loader",
                "vulkan_driver_environment": {},
            }
            with mock.patch.object(lg_launch.platform, "system", return_value="Linux"), \
                 mock.patch.object(lg_launch, "LOCAL_VULKAN_CONFIGS", ()), \
                 mock.patch.object(lg_launch, "BENCHMARK_ROOT", root / "missing"), \
                 mock.patch.object(lg_launch, "STATE_DIR", state_dir), \
                 mock.patch.object(lg_launch, "_probe_default_vulkan", return_value=loader_selection), \
                 mock.patch.dict(os.environ, {}, clear=True):
                selection = lg_launch.discover_vulkan_selection()

            saved = json.loads((state_dir / "vulkan.json").read_text(encoding="utf-8"))
        self.assertEqual(selection["source"], "default-loader")
        self.assertEqual(saved["generated_from"], "default-loader")

    def test_fresh_worktree_discovers_and_records_windows_icd(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            manifest = root / "igvk64.json"
            library = root / "igvk64.dll"
            manifest.write_text(
                '{"ICD":{"library_path":"./igvk64.dll"}}', encoding="utf-8"
            )
            library.touch()
            state_dir = root / "build" / "dev-control"
            probe = {
                "gpu_name": "Intel(R) Arc(TM) Test GPU",
                "gpu_type": "PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU",
                "graphics_driver_name": "Intel Corporation",
                "graphics_driver_version": "101.9999",
                "vulkan_api_version": "1.4.999",
                "software_renderer": False,
            }
            with mock.patch.object(lg_launch.platform, "system", return_value="Windows"), \
                 mock.patch.object(lg_launch, "LOCAL_VULKAN_CONFIGS", ()), \
                 mock.patch.object(lg_launch, "BENCHMARK_ROOT", root / "missing"), \
                 mock.patch.object(lg_launch, "STATE_DIR", state_dir), \
                 mock.patch.object(lg_launch, "_windows_vulkan_manifest_paths", return_value=[manifest]), \
                 mock.patch.object(lg_launch, "_probe_vulkan", return_value=probe), \
                 mock.patch.dict(os.environ, {}, clear=True):
                selection = lg_launch.discover_vulkan_selection()

            saved = json.loads((state_dir / "vulkan.json").read_text(encoding="utf-8"))
        self.assertEqual(selection["source"], f"windows-registry:{lg_launch.WINDOWS_VULKAN_DRIVERS_KEY}")
        self.assertEqual(selection["icd_path"], str(manifest.resolve()))
        self.assertEqual(saved["icd_path"], str(manifest.resolve()))
        self.assertEqual(saved["generated_from"], selection["source"])

    def test_fresh_worktree_rejects_missing_or_invalid_windows_icds(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            missing = root / "missing.json"
            invalid = root / "invalid.json"
            invalid.write_text("not JSON", encoding="utf-8")
            with mock.patch.object(lg_launch.platform, "system", return_value="Windows"), \
                 mock.patch.object(lg_launch, "LOCAL_VULKAN_CONFIGS", ()), \
                 mock.patch.object(lg_launch, "BENCHMARK_ROOT", root / "missing-results"), \
                 mock.patch.object(lg_launch, "_windows_vulkan_manifest_paths", return_value=[missing, invalid]), \
                 mock.patch.object(lg_launch, "_probe_default_vulkan", side_effect=LaunchError("no loader ICD")), \
                 mock.patch.dict(os.environ, {}, clear=True):
                with self.assertRaisesRegex(LaunchError, "no verified Intel Vulkan ICD"):
                    lg_launch.discover_vulkan_selection()

    def test_fresh_worktree_uses_default_loader_when_registry_is_empty(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            state_dir = root / "build" / "dev-control"
            loader_selection = {
                **self.selection(),
                "source": "default-loader",
                "vulkan_driver_environment": {},
            }
            with mock.patch.object(lg_launch.platform, "system", return_value="Windows"), \
                 mock.patch.object(lg_launch, "LOCAL_VULKAN_CONFIGS", ()), \
                 mock.patch.object(lg_launch, "BENCHMARK_ROOT", root / "missing"), \
                 mock.patch.object(lg_launch, "STATE_DIR", state_dir), \
                 mock.patch.object(lg_launch, "_windows_vulkan_manifest_paths", return_value=[]), \
                 mock.patch.object(lg_launch, "_probe_default_vulkan", return_value=loader_selection), \
                 mock.patch.dict(os.environ, {}, clear=True):
                selection = lg_launch.discover_vulkan_selection()

            saved = json.loads((state_dir / "vulkan.json").read_text(encoding="utf-8"))
        self.assertEqual(selection["source"], "default-loader")
        self.assertEqual(saved["generated_from"], "default-loader")

    def test_explicit_config_precedes_windows_fresh_worktree_fallback(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            explicit = root / "explicit.json"
            explicit.write_text(
                json.dumps({
                    "icd_path": r"C:\\explicit\\igvk64.json",
                    "icd_sha256": "a" * 64,
                    "gpu_name": "Intel Explicit GPU",
                }),
                encoding="utf-8",
            )
            with mock.patch.object(lg_launch.platform, "system", return_value="Windows"), \
                 mock.patch.object(lg_launch, "LOCAL_VULKAN_CONFIGS", ()), \
                 mock.patch.object(lg_launch, "BENCHMARK_ROOT", root / "missing"), \
                 mock.patch.object(lg_launch, "_discover_windows_vulkan_selection") as fallback, \
                 mock.patch.dict(os.environ, {"LG_DUEL_VULKAN_CONFIG": str(explicit)}, clear=True):
                selection = lg_launch.discover_vulkan_selection()

        self.assertEqual(selection["source"], f"local-config:{explicit}")
        fallback.assert_not_called()

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
        cleanup.assert_not_called()
        launch.assert_not_called()

    def test_stale_verified_state_cannot_mask_fallback_status(self) -> None:
        launch = {
            **self.status(),
            "requested_renderer": "gpu",
            "vulkan_selection_source": "test",
        }
        state = {
            "phase": "ready",
            "control_port": 27961,
            "client": {"pid": 10, "owned": True, "path": "client.exe"},
            "launch": launch,
        }
        with mock.patch.object(lg_launch, "send_request", return_value=self.status("SDL_Renderer/direct3d11")), \
             mock.patch.object(lg_launch, "_read_state", return_value=state), \
             mock.patch.object(lg_launch, "_entry_matches", return_value=True):
            result = lg_launch.status_with_state()
        self.assertEqual(result["renderer"], "SDL_Renderer/direct3d11")
        self.assertFalse(result["gpu_verified"])
        self.assertEqual(result["gpu_verification_state"], "stored-attestation-mismatch")

    def test_status_does_not_trust_launch_data_for_a_stale_client_entry(self) -> None:
        state = {
            "phase": "ready",
            "control_port": 27961,
            "client": {
                "pid": 10, "owned": True, "path": "client.exe",
                "creation_time": 1001,
            },
            "launch": {
                **self.status(),
                "requested_renderer": "gpu",
                "vulkan_selection_source": "test",
            },
        }
        with mock.patch.object(
            lg_launch, "send_request", return_value=self.status()
        ), mock.patch.object(
            lg_launch, "_read_state", return_value=state
        ), mock.patch.object(
            lg_launch, "_entry_matches", return_value=False
        ):
            result = lg_launch.status_with_state()
        self.assertFalse(result["gpu_verified"])
        self.assertEqual(
            result["gpu_verification_state"], "unverified-external"
        )

    def test_explicit_fallback_opt_in_is_accepted(self) -> None:
        with mock.patch.object(lg_launch, "send_request", return_value=self.status("SDL_Renderer/direct3d11")), \
             mock.patch.object(lg_launch, "_read_state", return_value=None), \
             mock.patch.object(lg_launch, "_write_state"):
            result = lg_launch.ensure_client(renderer="fallback", allow_fallback=True)
        self.assertEqual(result["gpu_verification_state"], "explicit-fallback")
        self.assertFalse(result["gpu_verified"])

    def test_existing_client_is_polled_until_protocol_and_map_are_ready(self) -> None:
        not_ready = {
            **self.status(),
            "server_running": False,
            "connected": False,
            "map": "",
            "map_revision": 0,
        }
        with mock.patch.object(
            lg_launch, "resolve_vulkan_selection", return_value=self.selection()
        ), mock.patch.object(
            lg_launch, "send_request", side_effect=[not_ready, self.status()]
        ) as sender, mock.patch.object(
            lg_launch, "_read_state", return_value=None
        ), mock.patch.object(
            lg_launch, "_write_state"
        ):
            result = lg_launch.ensure_client(renderer="gpu", timeout=1)
        self.assertTrue(result["gpu_verified"])
        self.assertEqual(sender.call_count, 2)

    def test_ready_wait_reports_when_client_never_replied(self) -> None:
        with self.assertRaisesRegex(
            LaunchError, "did not answer before the startup deadline"
        ):
            lg_launch._wait_for_ready_status(
                initial_status=None,
                deadline=lg_launch.time.monotonic() - 1,
                control_port=27961,
                renderer="fallback",
                selection=None,
                allow_fallback=True,
                benchmark=False,
            )

    def test_ready_wait_reports_early_client_crash_instead_of_timeout(self) -> None:
        client = mock.Mock()
        client.poll.return_value = 0xC0000005
        with mock.patch.object(lg_launch, "send_request") as sender:
            with self.assertRaisesRegex(
                LaunchError,
                r"client exited before becoming ready with "
                r"status 0xC0000005 \(3221225477\)",
            ):
                lg_launch._wait_for_ready_status(
                    initial_status=None,
                    deadline=lg_launch.time.monotonic() + 120,
                    control_port=27961,
                    renderer="fallback",
                    selection=None,
                    allow_fallback=True,
                    benchmark=False,
                    client_process=client,
                )
        sender.assert_not_called()

    def test_ready_wait_keeps_last_status_when_client_exits(self) -> None:
        client = mock.Mock()
        client.poll.return_value = 1
        not_ready = {
            **self.status(),
            "connected": False,
            "map": "",
            "map_revision": 0,
        }
        with self.assertRaisesRegex(
            LaunchError,
            r"exit code 1; last readiness issues: "
            r"connected is not true, map is empty, "
            r"map_revision is not a positive integer",
        ):
            lg_launch._wait_for_ready_status(
                initial_status=not_ready,
                deadline=lg_launch.time.monotonic() + 120,
                control_port=27961,
                renderer="gpu",
                selection=self.selection(),
                allow_fallback=False,
                benchmark=False,
                client_process=client,
            )

    def test_failed_probe_does_not_launch_over_owned_client_when_cleanup_fails(self) -> None:
        state = {
            "phase": "ready",
            "control_port": 27961,
            "client": {
                "pid": 101, "owned": True, "path": "client.exe",
                "creation_time": 1001,
            },
            "server": {
                "pid": 202, "owned": True, "path": "server.exe",
                "creation_time": 2002,
            },
        }
        with mock.patch.object(
            lg_launch, "send_request", side_effect=ControlError("transient")
        ), mock.patch.object(
            lg_launch, "_read_state", return_value=state
        ), mock.patch.object(
            lg_launch, "_matching_owned_processes",
            side_effect=[["client", "server"], ["client"]],
        ), mock.patch.object(
            lg_launch, "_unverified_live_owned_processes", return_value=[]
        ), mock.patch.object(
            lg_launch, "cleanup_owned", return_value=["server"]
        ) as cleanup, mock.patch.object(
            lg_launch, "_launch_process"
        ) as launch, mock.patch.object(
            lg_launch, "_write_state"
        ) as write:
            with self.assertRaisesRegex(
                LaunchError, "did not stop completely before relaunch: client"
            ):
                lg_launch.ensure_client(
                    renderer="fallback", allow_fallback=True
                )
        cleanup.assert_called_once_with(state)
        launch.assert_not_called()
        write.assert_not_called()

    def test_fresh_client_that_replies_but_never_gets_ready_is_cleaned(self) -> None:
        class FakeProcess:
            def __init__(self, pid: int) -> None:
                self.pid = pid

            def poll(self):
                return None

        not_ready = {
            **self.status(),
            "server_running": False,
            "connected": False,
            "map": "",
            "map_revision": 0,
        }
        request_count = 0

        def status_probe(*_args, **_kwargs):
            nonlocal request_count
            request_count += 1
            if request_count == 1:
                raise ControlError("offline")
            return not_ready

        written = []
        with tempfile.TemporaryDirectory() as temporary:
            build = Path(temporary) / "build"
            state_dir = Path(temporary) / "state"
            build.mkdir()
            (build / "lg_duel_client.exe").touch()
            (build / "lg_duel_server.exe").touch()
            with mock.patch.object(
                lg_launch, "STATE_DIR", state_dir
            ), mock.patch.object(
                lg_launch, "resolve_vulkan_selection", return_value=self.selection()
            ), mock.patch.object(
                lg_launch, "send_request", side_effect=status_probe
            ), mock.patch.object(
                lg_launch, "_existing_server_entry", return_value=None
            ), mock.patch.object(
                lg_launch, "_launch_process",
                side_effect=[FakeProcess(301), FakeProcess(302)],
            ), mock.patch.object(
                lg_launch, "_process_creation_time",
                side_effect=lambda pid: {301: 1001, 302: 1002}[pid],
            ), mock.patch.object(
                lg_launch, "_write_state", side_effect=written.append
            ), mock.patch.object(
                lg_launch, "_read_state", return_value=None
            ), mock.patch.object(
                lg_launch, "cleanup_owned", return_value=["client", "server"]
            ) as cleanup:
                with self.assertRaisesRegex(
                    LaunchError, "replied but did not become ready"
                ):
                    lg_launch.ensure_client(
                        renderer="gpu", timeout=0.1, build_dir=build
                    )
        self.assertEqual(written[0]["phase"], "starting")
        cleaned = cleanup.call_args.args[0]
        self.assertEqual(cleaned["client"]["creation_time"], 1002)
        self.assertEqual(cleaned["server"]["creation_time"], 1001)

    def test_atomic_state_write_replaces_complete_document(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            state_path = Path(temporary) / "processes.json"
            with mock.patch.object(lg_launch, "STATE_PATH", state_path):
                lg_launch._write_state({"phase": "starting", "control_port": 9})
            saved = json.loads(state_path.read_text(encoding="utf-8"))
            leftovers = list(state_path.parent.glob(".processes.json.*.tmp"))
        self.assertEqual(saved["phase"], "starting")
        self.assertEqual(leftovers, [])

    def test_creation_mismatch_clears_stale_state_without_killing_pid(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            state_path = Path(temporary) / "processes.json"
            state = {
                "phase": "ready",
                "client": {
                    "pid": 101,
                    "owned": True,
                    "path": r"C:\game\client.exe",
                    "creation_time": 1001,
                },
                "server": {
                    "pid": 0, "owned": False, "path": "",
                    "creation_time": None,
                },
            }
            state_path.write_text(json.dumps(state), encoding="utf-8")
            with mock.patch.object(
                lg_launch, "STATE_PATH", state_path
            ), mock.patch.object(
                lg_launch, "_process_path", return_value=r"C:\game\client.exe"
            ), mock.patch.object(
                lg_launch, "_process_creation_time", return_value=2002
            ), mock.patch.object(os, "kill") as kill:
                result = lg_launch.stop_owned()
            exists = state_path.exists()
        self.assertEqual(result["stopped"], [])
        self.assertFalse(result["left_owned_running"])
        self.assertFalse(exists)
        kill.assert_not_called()

    def test_live_legacy_owned_entry_is_reported_and_preserved(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            state_path = Path(temporary) / "processes.json"
            state = {
                "phase": "ready",
                "control_port": 27961,
                "client": {
                    "pid": 101,
                    "owned": True,
                    "path": r"C:\game\client.exe",
                },
                "server": {"pid": 0, "owned": False, "path": ""},
            }
            state_path.write_text(json.dumps(state), encoding="utf-8")
            with mock.patch.object(
                lg_launch, "STATE_PATH", state_path
            ), mock.patch.object(
                lg_launch, "_process_path", return_value=r"C:\game\client.exe"
            ), mock.patch.object(os, "kill") as kill:
                result = lg_launch.stop_owned()
            exists = state_path.exists()
        self.assertTrue(result["left_owned_running"])
        self.assertFalse(result["left_unowned_running"])
        self.assertEqual(result["unverified_owned"], ["client"])
        self.assertTrue(result["state_preserved"])
        self.assertTrue(exists)
        kill.assert_not_called()

    def test_cleanup_terminates_only_owned_matching_processes(self) -> None:
        state = {
            "client": {"pid": 101, "owned": True, "path": "client.exe"},
            "server": {"pid": 202, "owned": False, "path": "server.exe"},
        }
        matches = iter([True, False])
        with mock.patch.object(
            lg_launch, "_entry_matches",
            side_effect=lambda unused: next(matches, False),
        ), \
             mock.patch.object(os, "kill") as kill:
            stopped = lg_launch.cleanup_owned(state)
        self.assertEqual(stopped, ["client"])
        kill.assert_called_once_with(101, lg_launch.signal.SIGTERM)

    def test_cleanup_uses_shared_soft_and_force_deadlines(self) -> None:
        state = {
            "client": {"pid": 101, "owned": True, "path": "client.exe"},
            "server": {"pid": 202, "owned": True, "path": "server.exe"},
        }
        matches = iter([True, True, True, True, False, False])
        with mock.patch.object(
            lg_launch, "_entry_matches",
            side_effect=lambda unused: next(matches),
        ), mock.patch.object(
            lg_launch.time, "monotonic", side_effect=[0.0, 6.0, 6.0]
        ), mock.patch.object(os, "kill") as kill:
            stopped = lg_launch.cleanup_owned(state)
        self.assertEqual(stopped, ["client", "server"])
        self.assertEqual(
            kill.call_args_list,
            [
                mock.call(101, lg_launch.signal.SIGTERM),
                mock.call(202, lg_launch.signal.SIGTERM),
                mock.call(
                    101, getattr(lg_launch.signal, "SIGKILL",
                                 lg_launch.signal.SIGTERM)
                ),
                mock.call(
                    202, getattr(lg_launch.signal, "SIGKILL",
                                 lg_launch.signal.SIGTERM)
                ),
            ],
        )

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
        self.assertFalse(result["left_unowned_running"])
        self.assertEqual(result["remaining"], ["server"])
        state_path.unlink.assert_not_called()

    def test_stop_owned_reports_state_clear_failure(self) -> None:
        state = {
            "client": {"pid": 101, "owned": True, "path": "client.exe"},
        }
        state_path = mock.Mock()
        state_path.exists.return_value = True
        with mock.patch.object(
            lg_launch, "_read_state", return_value=state
        ), mock.patch.object(
            lg_launch, "cleanup_owned", return_value=["client"]
        ), mock.patch.object(
            lg_launch, "_entry_matches", return_value=False
        ), mock.patch.object(
            lg_launch, "_unverified_live_owned_processes", return_value=[]
        ), mock.patch.object(
            lg_launch, "_clear_state_if_same", return_value=False
        ), mock.patch.object(
            lg_launch, "STATE_PATH", state_path
        ):
            result = lg_launch.stop_owned()
        self.assertFalse(result["left_owned_running"])
        self.assertTrue(result["state_preserved"])
        self.assertTrue(result["state_clear_failed"])

    def test_lifecycle_lock_reports_busy_to_a_second_thread(self) -> None:
        entered = threading.Event()
        release = threading.Event()
        errors = []
        with tempfile.TemporaryDirectory() as temporary:
            state_path = Path(temporary) / "processes.json"

            def hold_lock() -> None:
                try:
                    with self.real_lifecycle_lock(timeout=0.2):
                        entered.set()
                        release.wait(2)
                except Exception as error:
                    errors.append(error)

            with mock.patch.object(lg_launch, "STATE_PATH", state_path):
                thread = threading.Thread(target=hold_lock)
                thread.start()
                self.assertTrue(entered.wait(1))
                with self.assertRaisesRegex(LaunchError, "lifecycle_busy"):
                    with self.real_lifecycle_lock(timeout=0.05):
                        pass
                release.set()
                thread.join(1)
        self.assertFalse(thread.is_alive())
        self.assertEqual(errors, [])

    def test_lifecycle_lock_reports_busy_across_processes(self) -> None:
        child_code = (
            "from pathlib import Path\n"
            "import sys\n"
            "import lg_launch\n"
            "lg_launch.STATE_PATH = Path(sys.argv[1])\n"
            "with lg_launch._lifecycle_lock(timeout=1.0):\n"
            "    print('locked', flush=True)\n"
            "    sys.stdin.readline()\n"
        )
        with tempfile.TemporaryDirectory() as temporary:
            state_path = Path(temporary) / "processes.json"
            child = subprocess.Popen(
                [sys.executable, "-c", child_code, str(state_path)],
                cwd=Path(lg_launch.__file__).resolve().parent,
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                encoding="utf-8",
            )
            try:
                self.assertEqual(child.stdout.readline().strip(), "locked")
                with mock.patch.object(lg_launch, "STATE_PATH", state_path):
                    with self.assertRaisesRegex(
                        LaunchError, "lifecycle_busy"
                    ):
                        with self.real_lifecycle_lock(timeout=0.05):
                            pass
            finally:
                stdout, stderr = child.communicate("\n", timeout=5)
            self.assertEqual(child.returncode, 0, stdout + stderr)

    def test_lifecycle_lock_preserves_corrupt_state(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            state_path = Path(temporary) / "processes.json"
            state_path.write_text("{broken", encoding="utf-8")
            with mock.patch.object(lg_launch, "STATE_PATH", state_path):
                with self.assertRaisesRegex(LaunchError, "corrupt"):
                    with self.real_lifecycle_lock():
                        self.fail("corrupt state must block lifecycle writes")
            self.assertEqual(
                state_path.read_text(encoding="utf-8"), "{broken"
            )

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
        self.assertTrue(all(
            not call.kwargs.get("survive_parent_exit", False)
            for call in launch.call_args_list
        ))

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
