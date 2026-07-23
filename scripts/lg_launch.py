#!/usr/bin/env python3
"""Shared verified launcher for LG Duel development and benchmark clients.

The launcher is the only place that selects Vulkan loader state for an owned
client.  Visual-control and benchmark callers reuse the same selection,
attestation, attachment, ownership, and cleanup rules.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import re
import signal
import socket
import subprocess
import sys
import time
from pathlib import Path
from typing import Any, Callable

from lg_control import ControlError, send_request


REPO_ROOT = Path(__file__).resolve().parents[1]
BUILD_DIR = REPO_ROOT / "build" / "default"
STATE_DIR = REPO_ROOT / "build" / "dev-control"
STATE_PATH = STATE_DIR / "processes.json"
LOCAL_VULKAN_CONFIGS = (
    STATE_DIR / "vulkan.json",
    REPO_ROOT / "build" / "vulkan.json",
)
BENCHMARK_ROOT = REPO_ROOT / "build" / "benchmarks"
GPU_RENDERER = "SDL_GPU/vulkan"
FALLBACK_PREFIX = "SDL_Renderer/"
SOFTWARE_MARKERS = ("swiftshader", "llvmpipe", "lavapipe", "software", "cpu")


class LaunchError(RuntimeError):
    pass


def _sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _normalized(path: Path | str) -> str:
    return os.path.normcase(os.path.abspath(os.fspath(path)))


def _parse_vulkan_devices(output: str) -> list[dict[str, Any]]:
    devices: list[dict[str, Any]] = []
    current: dict[str, str] | None = None
    for line in output.splitlines():
        if re.match(r"\s*GPU\d+:\s*$", line):
            if current:
                devices.append(current)
            current = {}
            continue
        match = re.match(
            r"\s*(apiVersion|driverVersion|deviceType|deviceName|driverName|driverInfo)\s*=\s*(.+?)\s*$",
            line,
        )
        if match:
            if current is None:
                current = {}
            current.setdefault(match.group(1), match.group(2))
    if current:
        devices.append(current)
    return [
        {
            "gpu_name": item.get("deviceName"),
            "gpu_type": item.get("deviceType"),
            "graphics_driver_name": item.get("driverName"),
            "graphics_driver_version": item.get("driverInfo") or item.get("driverVersion"),
            "vulkan_api_version": item.get("apiVersion"),
        }
        for item in devices
        if item.get("deviceName")
    ]


def _is_software_device(device: dict[str, Any]) -> bool:
    text = " ".join(str(device.get(key, "")) for key in ("gpu_name", "gpu_type", "graphics_driver_name")).lower()
    return "physical_device_type_cpu" in text or any(marker in text for marker in SOFTWARE_MARKERS)


def _manifest_record(path: Path) -> dict[str, Any]:
    record: dict[str, Any] = {
        "path": str(path.resolve()),
        "exists": path.is_file(),
    }
    if not path.is_file():
        return record
    try:
        record["sha256"] = _sha256(path)
        document = json.loads(path.read_text(encoding="utf-8-sig"))
        library = document.get("ICD", {}).get("library_path") if isinstance(document, dict) else None
        if isinstance(library, str):
            record["library_path"] = str((path.parent / library).resolve())
    except (OSError, json.JSONDecodeError) as error:
        record["read_error"] = str(error)
    return record


def _probe_vulkan(
    manifest: Path,
    *,
    expected_gpu: str | None = None,
    runner: Callable[..., subprocess.CompletedProcess[str]] = subprocess.run,
) -> dict[str, Any]:
    environment = os.environ.copy()
    environment["VK_DRIVER_FILES"] = str(manifest.resolve())
    environment.pop("VK_ICD_FILENAMES", None)
    environment.pop("VK_ADD_DRIVER_FILES", None)
    try:
        completed = runner(
            ["vulkaninfo", "--summary"],
            text=True,
            capture_output=True,
            timeout=8.0,
            check=False,
            env=environment,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        raise LaunchError(f"vulkaninfo failed for ICD '{manifest}': {error}") from error
    if completed.returncode != 0:
        detail = (completed.stderr or completed.stdout or f"vulkaninfo exited {completed.returncode}").strip()
        raise LaunchError(f"Vulkan initialization failed for ICD '{manifest}': {detail}")
    devices = _parse_vulkan_devices(completed.stdout)
    if expected_gpu:
        selected = next((device for device in devices if device.get("gpu_name") == expected_gpu), None)
    else:
        selected = next((device for device in devices if not _is_software_device(device)), None)
    if selected is None:
        names = ", ".join(str(device.get("gpu_name", "unknown")) for device in devices) or "none"
        raise LaunchError(
            f"configured Vulkan device '{expected_gpu or 'hardware GPU'}' was not reported; devices: {names}"
        )
    selected["software_renderer"] = _is_software_device(selected)
    if selected["software_renderer"]:
        raise LaunchError(f"configured Vulkan device is a software renderer: {selected.get('gpu_name')}")
    return selected


def _probe_default_vulkan(
    *,
    runner: Callable[..., subprocess.CompletedProcess[str]] = subprocess.run,
) -> dict[str, Any]:
    environment = os.environ.copy()
    for name in ("VK_DRIVER_FILES", "VK_ICD_FILENAMES", "VK_ADD_DRIVER_FILES"):
        environment.pop(name, None)
    environment["VK_LOADER_DEBUG"] = "driver"
    try:
        completed = runner(
            ["vulkaninfo", "--summary"],
            text=True,
            capture_output=True,
            timeout=8.0,
            check=False,
            env=environment,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        raise LaunchError(f"default Vulkan loader probe failed: {error}") from error
    if completed.returncode != 0:
        detail = (
            completed.stderr
            or completed.stdout
            or f"vulkaninfo exited {completed.returncode}"
        ).strip()
        raise LaunchError(f"default Vulkan loader initialization failed: {detail}")

    devices = _parse_vulkan_devices(completed.stdout)
    selected = next(
        (
            device
            for device in devices
            if "intel" in str(device.get("gpu_name", "")).lower()
            and not _is_software_device(device)
        ),
        None,
    )
    if selected is None:
        names = ", ".join(
            str(device.get("gpu_name", "unknown")) for device in devices
        ) or "none"
        raise LaunchError(f"default Vulkan loader reported no Intel hardware GPU; devices: {names}")

    loader_output = f"{completed.stdout}\n{completed.stderr}"
    manifest_paths: list[Path] = []
    for match in re.finditer(
        r"Found ICD manifest file\s+(.+?\.json)(?:,|\r?$)",
        loader_output,
        flags=re.IGNORECASE | re.MULTILINE,
    ):
        path = Path(match.group(1).strip().strip('"'))
        if path not in manifest_paths:
            manifest_paths.append(path)
    records = [_manifest_record(path) for path in manifest_paths]
    records = [
        record
        for record in records
        if record.get("exists") and record.get("sha256") and record.get("library_path")
    ]

    active_libraries = [
        match.group(1)
        for match in re.finditer(
            r'Using ".+?" with driver:\s*"([^"]+)"',
            loader_output,
            flags=re.IGNORECASE,
        )
    ]
    active_records = [
        record
        for record in records
        if any(
            _normalized(str(record["library_path"])) == _normalized(library)
            for library in active_libraries
        )
    ]
    if len(active_records) == 1:
        record = active_records[0]
    elif len(records) == 1:
        record = records[0]
    else:
        paths = ", ".join(str(record.get("path")) for record in records) or "none"
        raise LaunchError(
            "could not identify one active Intel ICD from the default Vulkan loader; "
            f"valid manifests: {paths}"
        )

    selected.update({
        "source": "default-loader",
        "software_renderer": False,
        "icd_path": str(record["path"]),
        "icd_sha256": str(record["sha256"]).lower(),
        "icd_library_path": record["library_path"],
        "verification_state": "preflight-verified",
        "vulkan_driver_environment": {},
    })
    return selected


def _selection_from_document(document: dict[str, Any], source: str) -> dict[str, Any] | None:
    environment = document.get("environment", document)
    aggregate = document.get("aggregate", {})
    if document.get("schema_version") and isinstance(aggregate, dict) and aggregate.get("valid") is False:
        return None
    if str(environment.get("renderer", "")).lower() != GPU_RENDERER.lower():
        return None
    if environment.get("vulkan_metadata_status") not in (None, "available"):
        return None
    manifests = environment.get("vulkan_icd_manifest_records", [])
    if not isinstance(manifests, list) or len(manifests) != 1 or not isinstance(manifests[0], dict):
        return None
    record = manifests[0]
    path_text = record.get("path")
    digest = record.get("sha256")
    gpu_name = environment.get("gpu_name")
    if not all(isinstance(value, str) and value for value in (path_text, digest, gpu_name)):
        return None
    return {
        "source": source,
        "icd_path": path_text,
        "icd_sha256": digest.lower(),
        "gpu_name": gpu_name,
        "gpu_type": environment.get("gpu_type"),
        "graphics_driver_name": environment.get("graphics_driver_name"),
        "graphics_driver_version": environment.get("graphics_driver_version"),
        "vulkan_api_version": environment.get("vulkan_api_version"),
    }


def _load_local_selection(path: Path) -> dict[str, Any] | None:
    try:
        document = json.loads(path.read_text(encoding="utf-8-sig"))
    except (OSError, json.JSONDecodeError):
        return None
    if not isinstance(document, dict):
        return None
    if "icd_path" in document:
        result = dict(document)
        result["source"] = f"local-config:{path}"
        return result
    return _selection_from_document(document, f"local-config:{path}")


def discover_vulkan_selection() -> dict[str, Any]:
    configured = os.environ.get("LG_DUEL_VULKAN_CONFIG")
    candidates = ([Path(configured)] if configured else []) + list(LOCAL_VULKAN_CONFIGS)
    for path in candidates:
        selection = _load_local_selection(path)
        if selection is not None and "intel" in str(selection.get("gpu_name", "")).lower():
            return selection

    artifacts = sorted(
        BENCHMARK_ROOT.glob("**/aggregate.json") if BENCHMARK_ROOT.is_dir() else [],
        key=lambda item: item.stat().st_mtime,
        reverse=True,
    )
    for path in artifacts:
        try:
            document = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            continue
        if not isinstance(document, dict):
            continue
        selection = _selection_from_document(document, f"verified-benchmark:{path}")
        if selection is not None and "intel" in str(selection.get("gpu_name", "")).lower():
            return selection
    raise LaunchError(
        "no verified Intel Vulkan ICD configuration was found; set LG_DUEL_VULKAN_CONFIG "
        "to an ignored local JSON file or create a valid local GPU benchmark baseline"
    )


def resolve_vulkan_selection(
    *, runner: Callable[..., subprocess.CompletedProcess[str]] = subprocess.run
) -> dict[str, Any]:
    selection = discover_vulkan_selection()
    manifest = Path(str(selection.get("icd_path", "")))
    record = _manifest_record(manifest)
    if not record.get("exists"):
        raise LaunchError(f"configured Vulkan ICD does not exist: {manifest}")
    actual_hash = str(record.get("sha256", "")).lower()
    expected_hash = str(selection.get("icd_sha256", "")).lower()
    if not expected_hash or actual_hash != expected_hash:
        raise LaunchError(
            f"configured Vulkan ICD hash mismatch for '{manifest}': expected {expected_hash or 'missing'}, "
            f"actual {actual_hash or 'unavailable'}"
        )
    try:
        probed = _probe_vulkan(manifest, expected_gpu=selection.get("gpu_name"), runner=runner)
    except LaunchError as error:
        raise LaunchError(
            f"{error}\nSelected ICD: {manifest.resolve()} sha256={actual_hash}"
        ) from error
    for key in ("graphics_driver_version", "vulkan_api_version"):
        expected = selection.get(key)
        actual = probed.get(key)
        if expected and actual != expected:
            raise LaunchError(
                f"configured Vulkan {key} mismatch for '{manifest}': expected {expected}, actual {actual}"
            )
    return {
        **selection,
        **probed,
        "icd_path": str(manifest.resolve()),
        "icd_sha256": actual_hash,
        "icd_library_path": record.get("library_path"),
        "verification_state": "preflight-verified",
        "vulkan_driver_environment": {"VK_DRIVER_FILES": str(manifest.resolve())},
    }


def gpu_environment(selection: dict[str, Any]) -> dict[str, str]:
    return {
        "LG_DUEL_RENDER_BACKEND": "gpu",
        "VK_DRIVER_FILES": str(selection["icd_path"]),
        "LG_DUEL_VULKAN_ICD_PATH": str(selection["icd_path"]),
        "LG_DUEL_VULKAN_ICD_SHA256": str(selection["icd_sha256"]),
        "LG_DUEL_VULKAN_API_VERSION": str(selection.get("vulkan_api_version") or ""),
    }


def verify_control_status(
    status: dict[str, Any],
    *,
    requested_renderer: str,
    selection: dict[str, Any] | None,
    allow_fallback: bool = False,
) -> dict[str, Any]:
    actual = str(status.get("renderer", ""))
    enriched = dict(status)
    enriched["requested_renderer"] = requested_renderer
    enriched["actual_renderer"] = actual
    if requested_renderer == "fallback":
        if not allow_fallback:
            raise LaunchError("fallback rendering requires explicit allow_fallback=true or -Renderer fallback")
        if not actual.startswith(FALLBACK_PREFIX):
            raise LaunchError(f"explicit fallback requested, but the active renderer is '{actual or 'unknown'}'")
        enriched["gpu_verification_state"] = "explicit-fallback"
        enriched["gpu_verified"] = False
        return enriched

    if actual != GPU_RENDERER:
        raise LaunchError(
            f"GPU renderer required, but the client reported '{actual or 'unknown'}'; "
            "SDL_Renderer, D3D11, SwiftShader, and other fallbacks are not accepted"
        )
    if selection is None:
        raise LaunchError("GPU status cannot be verified without a resolved Vulkan selection")
    checks = {
        "gpu_name": selection.get("gpu_name"),
        "graphics_driver_version": selection.get("graphics_driver_version"),
        "vulkan_api_version": selection.get("vulkan_api_version"),
        "vulkan_icd_path": selection.get("icd_path"),
        "vulkan_icd_sha256": selection.get("icd_sha256"),
    }
    mismatches: list[str] = []
    for key, expected in checks.items():
        if expected in (None, ""):
            continue
        actual_value = status.get(key)
        if key == "vulkan_icd_path":
            matched = isinstance(actual_value, str) and _normalized(actual_value) == _normalized(str(expected))
        else:
            matched = str(actual_value) == str(expected)
        if not matched:
            mismatches.append(f"{key}: expected '{expected}', actual '{actual_value}'")
    if status.get("software_renderer") is not False:
        mismatches.append(f"software_renderer: expected false, actual '{status.get('software_renderer')}'")
    if mismatches:
        raise LaunchError("GPU startup attestation failed: " + "; ".join(mismatches))
    enriched.update({
        "gpu_verification_state": "verified",
        "gpu_verified": True,
        "vulkan_selection_source": selection.get("source"),
        "gpu_type": selection.get("gpu_type"),
        "vulkan_metadata_status": "available",
        "vulkan_driver_environment": selection.get(
            "vulkan_driver_environment",
            {"VK_DRIVER_FILES": selection.get("icd_path")},
        ),
        "vulkan_icd_manifests": [selection.get("icd_path")],
        "vulkan_icd_manifest_records": [{
            "path": selection.get("icd_path"),
            "sha256": selection.get("icd_sha256"),
            "exists": True,
            "library_path": selection.get("icd_library_path"),
        }],
    })
    return enriched


def _read_state() -> dict[str, Any] | None:
    try:
        value = json.loads(STATE_PATH.read_text(encoding="utf-8-sig"))
        return value if isinstance(value, dict) else None
    except (OSError, json.JSONDecodeError):
        return None


def _write_state(value: dict[str, Any]) -> None:
    STATE_DIR.mkdir(parents=True, exist_ok=True)
    STATE_PATH.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def _process_path(pid: int) -> str | None:
    if pid <= 0 or platform.system() != "Windows":
        return None
    try:
        import ctypes
        from ctypes import wintypes

        process = ctypes.windll.kernel32.OpenProcess(0x1000, False, pid)
        if not process:
            return None
        try:
            size = wintypes.DWORD(32768)
            buffer = ctypes.create_unicode_buffer(size.value)
            if not ctypes.windll.kernel32.QueryFullProcessImageNameW(process, 0, buffer, ctypes.byref(size)):
                return None
            return buffer.value
        finally:
            ctypes.windll.kernel32.CloseHandle(process)
    except (AttributeError, OSError, ValueError):
        return None


def _scenario_process_path(pid: int) -> str | None:
    if platform.system() == "Windows":
        return _process_path(pid)
    try:
        return str(Path(f"/proc/{pid}/exe").resolve(strict=True))
    except (OSError, RuntimeError):
        return None


def _process_creation_time(pid: int) -> int | None:
    """Return a stable process start value, not wall-clock time."""
    if pid <= 0:
        return None
    if platform.system() != "Windows":
        try:
            fields = Path(f"/proc/{pid}/stat").read_text(encoding="ascii").split()
            return int(fields[21])
        except (OSError, ValueError, IndexError):
            return None
    try:
        import ctypes
        from ctypes import wintypes

        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel32.OpenProcess.argtypes = (wintypes.DWORD, wintypes.BOOL, wintypes.DWORD)
        kernel32.OpenProcess.restype = wintypes.HANDLE
        kernel32.GetProcessTimes.argtypes = (
            wintypes.HANDLE, ctypes.POINTER(wintypes.FILETIME),
            ctypes.POINTER(wintypes.FILETIME), ctypes.POINTER(wintypes.FILETIME),
            ctypes.POINTER(wintypes.FILETIME),
        )
        kernel32.GetProcessTimes.restype = wintypes.BOOL
        kernel32.CloseHandle.argtypes = (wintypes.HANDLE,)
        kernel32.CloseHandle.restype = wintypes.BOOL
        process = kernel32.OpenProcess(0x1000, False, pid)
        if not process:
            return None
        try:
            creation = wintypes.FILETIME()
            exit_time = wintypes.FILETIME()
            kernel = wintypes.FILETIME()
            user = wintypes.FILETIME()
            if not kernel32.GetProcessTimes(
                process, ctypes.byref(creation), ctypes.byref(exit_time),
                ctypes.byref(kernel), ctypes.byref(user),
            ):
                return None
            return (creation.dwHighDateTime << 32) | creation.dwLowDateTime
        finally:
            kernel32.CloseHandle(process)
    except (AttributeError, OSError, ValueError):
        return None


def _entry_matches(entry: dict[str, Any]) -> bool:
    try:
        pid = int(entry.get("pid", 0))
        expected = str(entry.get("path", ""))
    except (TypeError, ValueError):
        return False
    actual = _process_path(pid)
    return bool(actual and expected and _normalized(actual) == _normalized(expected))


def _terminate_entry(entry: dict[str, Any]) -> bool:
    if not entry.get("owned") or not _entry_matches(entry):
        return False
    try:
        os.kill(int(entry["pid"]), signal.SIGTERM)
    except (OSError, ProcessLookupError, ValueError):
        return False
    return True


def cleanup_owned(state: dict[str, Any] | None) -> list[str]:
    stopped: list[str] = []
    if not state:
        return stopped
    for name in ("client", "server"):
        entry = state.get(name)
        if isinstance(entry, dict) and _terminate_entry(entry):
            stopped.append(name)
    return stopped


def _tail(path: Path, limit: int = 30) -> str:
    try:
        return "\n".join(path.read_text(encoding="utf-8", errors="replace").splitlines()[-limit:])
    except OSError:
        return ""


def _launch_process(executable: Path, arguments: list[str], stdout_path: Path, stderr_path: Path,
                    environment: dict[str, str], working_directory: Path | None = None) -> subprocess.Popen[str]:
    creationflags = getattr(subprocess, "CREATE_NO_WINDOW", 0)
    stdout = stdout_path.open("w", encoding="utf-8")
    stderr = stderr_path.open("w", encoding="utf-8")
    try:
        return subprocess.Popen(
            [str(executable), *arguments], cwd=working_directory or BUILD_DIR, env=environment,
            stdout=stdout, stderr=stderr, text=True, creationflags=creationflags,
        )
    finally:
        stdout.close()
        stderr.close()


def _control_endpoint_listening(port: int, timeout: float) -> bool:
    try:
        with socket.create_connection(("127.0.0.1", port), timeout=max(0.05, min(timeout, 0.5))):
            return True
    except OSError:
        return False


def _scenario_process_record(
    process: subprocess.Popen[str], executable: Path, run_token: str
) -> dict[str, Any]:
    created = _process_creation_time(process.pid)
    if platform.system() == "Windows" and created is None:
        raise LaunchError(f"could not attest creation time for owned process {process.pid}")
    return {
        "pid": process.pid,
        "owned": True,
        "path": str(executable.resolve()),
        "creation_time": created,
        "run_token": run_token,
    }


def _scenario_entry_matches(entry: dict[str, Any]) -> bool:
    try:
        pid = int(entry.get("pid", 0))
        expected_path = str(entry.get("path", ""))
        expected_created = entry.get("creation_time")
        token = entry.get("run_token")
    except (TypeError, ValueError):
        return False
    if pid <= 0 or not expected_path or not isinstance(token, str) or not token:
        return False
    actual_path = _scenario_process_path(pid)
    if not actual_path or _normalized(actual_path) != _normalized(expected_path):
        return False
    actual_created = _process_creation_time(pid)
    if expected_created is None or actual_created is None:
        # Unknown systems fail closed. A PID and path alone can match a reused PID.
        return False
    return int(expected_created) == actual_created


def _process_running(pid: int) -> bool:
    if pid <= 0:
        return False
    if os.name == "nt":
        try:
            import ctypes

            process = ctypes.windll.kernel32.OpenProcess(0x1000, False, pid)
            if not process:
                return False
            try:
                exit_code = ctypes.c_ulong()
                return bool(
                    ctypes.windll.kernel32.GetExitCodeProcess(
                        process, ctypes.byref(exit_code)
                    )
                    and exit_code.value == 259
                )
            finally:
                ctypes.windll.kernel32.CloseHandle(process)
        except (AttributeError, OSError, ValueError):
            return False
    try:
        os.kill(pid, 0)
        return True
    except (OSError, ProcessLookupError, ValueError):
        return False


def cleanup_scenario_session(state: dict[str, Any] | None) -> dict[str, Any]:
    """Stop only scenario children whose path and start time still match."""
    stopped: list[str] = []
    already_exited: list[str] = []
    failures: list[str] = []
    if not isinstance(state, dict):
        return {
            "stopped": stopped,
            "already_exited": already_exited,
            "failures": failures,
        }
    timeout = max(0.0, float(state.get("cleanup_timeout", 5.0)))
    matched: list[tuple[str, dict[str, Any]]] = []
    for name in ("client", "server"):
        entry = state.get(name)
        if not isinstance(entry, dict) or not entry.get("owned"):
            continue
        if entry.get("run_token") != state.get("run_token"):
            failures.append(f"{name}: run token does not match")
            continue
        if not _scenario_entry_matches(entry):
            try:
                pid = int(entry.get("pid", 0))
            except (TypeError, ValueError):
                pid = 0
            if _process_running(pid):
                failures.append(f"{name}: process identity no longer matches")
            else:
                already_exited.append(name)
            continue
        try:
            os.kill(int(entry["pid"]), signal.SIGTERM)
            matched.append((name, entry))
        except (OSError, ProcessLookupError, ValueError) as error:
            try:
                pid = int(entry.get("pid", 0))
            except (TypeError, ValueError):
                pid = 0
            # The server may finish between the identity check and this call.
            # Count that race as a clean exit only after a fresh process check.
            if not _process_running(pid):
                already_exited.append(name)
            else:
                failures.append(f"{name}: could not request exit: {error}")

    deadline = time.monotonic() + timeout
    pending = list(matched)
    while pending and time.monotonic() < deadline:
        pending = [
            (name, entry)
            for name, entry in pending
            if _process_running(int(entry["pid"])) and _scenario_entry_matches(entry)
        ]
        if pending:
            time.sleep(min(0.05, max(0.0, deadline - time.monotonic())))
    for name, entry in matched:
        if any(pending_name == name for pending_name, _ in pending):
            failures.append(f"{name}: did not exit before cleanup deadline")
        else:
            stopped.append(name)
    return {
        "stopped": stopped,
        "already_exited": already_exited,
        "failures": failures,
    }


def launch_scenario_session(
    scenario_path: Path | str,
    run_dir: Path | str,
    run_token: str,
    server_port: int,
    control_port: int,
    renderer: str = "gpu",
    allow_fallback: bool = False,
    timeout: float = 20.0,
    build_dir: Path | str | None = None,
) -> dict[str, Any]:
    """Launch a new, owned server and client for one live scenario run."""
    if renderer not in {"gpu", "fallback"}:
        raise LaunchError("renderer must be 'gpu' or 'fallback'")
    if renderer == "fallback" and not allow_fallback:
        allow_fallback = True
    if not isinstance(run_token, str) or not run_token:
        raise LaunchError("scenario run token must not be empty")
    try:
        server_port = int(server_port)
        control_port = int(control_port)
    except (TypeError, ValueError) as error:
        raise LaunchError("server and control ports must be integers") from error
    if not (1 <= server_port <= 65535 and 1 <= control_port <= 65535):
        raise LaunchError("server and control ports must be between 1 and 65535")
    if server_port == control_port:
        raise LaunchError("server and control ports must differ")
    if timeout <= 0:
        raise LaunchError("startup timeout must be greater than zero")

    scenario = Path(scenario_path).resolve()
    output_dir = Path(run_dir).resolve()
    launch_build_dir = Path(build_dir or BUILD_DIR).resolve()
    client_exe = launch_build_dir / "lg_duel_client.exe"
    server_exe = launch_build_dir / "lg_duel_server.exe"
    missing = [
        str(path) for path in (scenario, client_exe, server_exe) if not path.is_file()
    ]
    if missing:
        raise LaunchError("required scenario session file(s) are unavailable: " + ", ".join(missing))

    try:
        send_request("status", port=control_port, timeout=min(timeout, 0.5))
    except ControlError:
        if _control_endpoint_listening(int(control_port), timeout):
            raise LaunchError(f"control endpoint 127.0.0.1:{control_port} is already listening")
    else:
        raise LaunchError(
            f"control endpoint 127.0.0.1:{control_port} already has a development-control session"
        )

    selection = resolve_vulkan_selection() if renderer == "gpu" else None
    output_dir.mkdir(parents=True, exist_ok=True)
    logs = {
        "server_stdout": str(output_dir / "server.stdout.log"),
        "server_stderr": str(output_dir / "server.stderr.log"),
        "client_stdout": str(output_dir / "client.stdout.log"),
        "client_stderr": str(output_dir / "client.stderr.log"),
    }
    environment = os.environ.copy()
    environment["LG_DUEL_LIVE_SCENARIO"] = "1"
    if renderer == "gpu":
        environment.update(gpu_environment(selection or {}))
        environment.pop("VK_ICD_FILENAMES", None)
        environment.pop("VK_ADD_DRIVER_FILES", None)
    else:
        environment["LG_DUEL_RENDER_BACKEND"] = "fallback"

    state: dict[str, Any] = {
        "run_token": run_token,
        "server_port": server_port,
        "control_port": control_port,
        "cleanup_timeout": min(5.0, timeout),
        "logs": logs,
        "binaries": {
            "client": str(client_exe),
            "server": str(server_exe),
            "scenario": str(scenario),
        },
        "binary_sha256": {
            "client": _sha256(client_exe),
            "server": _sha256(server_exe),
            "scenario": _sha256(scenario),
        },
        "build_directory": str(launch_build_dir),
    }
    state["build_identity"] = hashlib.sha256(
        (state["binary_sha256"]["client"] + state["binary_sha256"]["server"]).encode("ascii")
    ).hexdigest()
    handles: list[subprocess.Popen[str]] = []
    try:
        server_process = _launch_process(
            server_exe,
            [
                str(server_port), "--live-scenario", str(scenario),
                "--scenario-run-dir", str(output_dir), "--scenario-token", run_token,
            ],
            Path(logs["server_stdout"]), Path(logs["server_stderr"]),
            environment, launch_build_dir,
        )
        handles.append(server_process)
        state["server"] = _scenario_process_record(server_process, server_exe, run_token)
        client_process = _launch_process(
            client_exe,
            ["127.0.0.1", str(server_port), "--dev-control", "--control-port", str(control_port)],
            Path(logs["client_stdout"]), Path(logs["client_stderr"]),
            environment, launch_build_dir,
        )
        handles.append(client_process)
        state["client"] = _scenario_process_record(client_process, client_exe, run_token)

        deadline = time.monotonic() + timeout
        raw = None
        while time.monotonic() < deadline:
            if server_process.poll() is not None or client_process.poll() is not None:
                break
            try:
                raw = send_request(
                    "status", port=control_port,
                    timeout=min(2.0, max(0.05, deadline - time.monotonic())),
                )
                break
            except ControlError:
                time.sleep(min(0.25, max(0.0, deadline - time.monotonic())))
        if raw is None:
            raise LaunchError("scenario client did not answer before the startup deadline")
        state["status"] = verify_control_status(
            raw, requested_renderer=renderer, selection=selection, allow_fallback=allow_fallback
        )
        return state
    except Exception as error:
        cleanup = cleanup_scenario_session(state)
        # A just-spawned child remains ours even if identity capture failed.
        for process in handles:
            if process.poll() is None:
                try:
                    process.terminate()
                    process.wait(timeout=min(5.0, timeout))
                except (OSError, subprocess.TimeoutExpired):
                    pass
        log_text = "\n".join(
            f"{name}:\n{_tail(Path(path)) or '(none recorded)'}"
            for name, path in logs.items() if name.endswith("stderr")
        )
        raise LaunchError(
            f"{error}\nCleanup: {cleanup}\nScenario logs:\n{log_text}"
        ) from error


def _existing_server_entry(server_exe: Path) -> dict[str, Any] | None:
    state = _read_state()
    entry = state.get("server") if state else None
    if (
        isinstance(entry, dict) and
        Path(str(entry.get("path", ""))).resolve() == server_exe.resolve() and
        _entry_matches(entry)
    ):
        return dict(entry)
    return None


def status_with_state(*, port: int = 27961, timeout: float = 2.0) -> dict[str, Any]:
    status = send_request("status", port=port, timeout=timeout)
    state = _read_state()
    if state and int(state.get("control_port", -1)) == port:
        launch = state.get("launch", {})
        if isinstance(launch, dict):
            requested = str(launch.get("requested_renderer", "gpu"))
            selection = None
            if requested == "gpu":
                selection = {
                    "source": launch.get("vulkan_selection_source"),
                    "gpu_name": launch.get("gpu_name"),
                    "gpu_type": launch.get("gpu_type"),
                    "graphics_driver_version": launch.get("graphics_driver_version"),
                    "vulkan_api_version": launch.get("vulkan_api_version"),
                    "icd_path": launch.get("vulkan_icd_path"),
                    "icd_sha256": launch.get("vulkan_icd_sha256"),
                    "vulkan_driver_environment": launch.get(
                        "vulkan_driver_environment", {}
                    ),
                }
                manifest_records = launch.get("vulkan_icd_manifest_records")
                if (
                    isinstance(manifest_records, list)
                    and manifest_records
                    and isinstance(manifest_records[0], dict)
                ):
                    selection["icd_library_path"] = manifest_records[0].get(
                        "library_path"
                    )
            try:
                return verify_control_status(
                    status,
                    requested_renderer=requested,
                    selection=selection,
                    allow_fallback=requested == "fallback",
                )
            except LaunchError as error:
                # Never let stale process state turn a newly bound fallback
                # client into an apparently verified GPU status response.
                status["gpu_verification_state"] = "stored-attestation-mismatch"
                status["gpu_verified"] = False
                status["gpu_verification_error"] = str(error)
    status.setdefault("actual_renderer", status.get("renderer"))
    status.setdefault("gpu_verification_state", "unverified-external")
    status.setdefault("gpu_verified", False)
    return status


def ensure_client(
    *,
    renderer: str = "gpu",
    allow_fallback: bool = False,
    benchmark: bool = False,
    manage_server: bool = True,
    server_port: int = 27960,
    control_port: int = 27961,
    timeout: float = 20.0,
    build_dir: Path | None = None,
) -> dict[str, Any]:
    if renderer not in {"gpu", "fallback"}:
        raise LaunchError("renderer must be 'gpu' or 'fallback'")
    if renderer == "fallback" and not allow_fallback:
        # The renderer spelling itself is the PowerShell opt-in; normalize it
        # to the same explicit flag used by MCP callers.
        allow_fallback = True
    launch_build_dir = (build_dir or BUILD_DIR).resolve()
    client_exe = launch_build_dir / "lg_duel_client.exe"
    server_exe = launch_build_dir / "lg_duel_server.exe"
    selection = (
        _probe_default_vulkan() if renderer == "gpu" and benchmark
        else resolve_vulkan_selection() if renderer == "gpu"
        else None
    )
    try:
        raw = send_request("status", port=control_port, timeout=min(timeout, 2.0))
    except ControlError:
        raw = None
    if raw is not None:
        if build_dir is not None:
            state = _read_state()
            client_entry = state.get("client") if state else None
            server_entry = state.get("server") if state else None
            client_matches = isinstance(client_entry, dict) and (
                Path(str(client_entry.get("path", ""))).resolve() == client_exe.resolve()
            )
            server_matches = not manage_server or (
                isinstance(server_entry, dict) and
                Path(str(server_entry.get("path", ""))).resolve() == server_exe.resolve()
            )
            if not client_matches or not server_matches:
                raise LaunchError(
                    "an existing development-control session does not match the requested "
                    f"benchmark build directory '{launch_build_dir}'; stop it before benchmarking"
                )
        try:
            verified = verify_control_status(
                raw, requested_renderer=renderer, selection=selection, allow_fallback=allow_fallback
            )
            if benchmark and not raw.get("benchmark_enabled", False):
                raise LaunchError("the existing development-control client was not launched with --benchmark")
        except LaunchError as error:
            state = _read_state()
            cleanup_owned(state)
            if state and STATE_PATH.exists():
                STATE_PATH.unlink()
            stderr = _tail(STATE_DIR / "client.stderr.log")
            icd = "none" if selection is None else (
                f"{selection.get('icd_path')} sha256={selection.get('icd_sha256')}"
            )
            raise LaunchError(
                f"{error}\nSelected ICD: {icd}\nVulkan/client errors:\n{stderr or '(none recorded)'}"
            ) from error
        state = _read_state() or {
            "server": {"pid": 0, "owned": False, "path": ""},
            "client": {"pid": 0, "owned": False, "path": ""},
            "server_port": server_port,
            "control_port": control_port,
        }
        state["launch"] = verified
        _write_state(state)
        verified["client_executable"] = str(client_exe)
        verified["build_directory"] = str(launch_build_dir)
        return verified

    if not client_exe.is_file() or (manage_server and not server_exe.is_file()):
        required = "client and server" if manage_server else "client"
        raise LaunchError(
            f"LG Duel {required} executable(s) are unavailable in '{launch_build_dir}'; "
            "configure and build the requested CMake preset first"
        )
    STATE_DIR.mkdir(parents=True, exist_ok=True)
    environment = os.environ.copy()
    if renderer == "gpu":
        environment.update(gpu_environment(selection or {}))
        environment.pop("VK_ICD_FILENAMES", None)
        environment.pop("VK_ADD_DRIVER_FILES", None)
    else:
        environment["LG_DUEL_RENDER_BACKEND"] = "fallback"
    if benchmark:
        # Benchmarks use the loader's normal driver search. Do not let inherited
        # or launcher-set ICD overrides change which drivers it can find.
        environment.pop("VK_DRIVER_FILES", None)
        environment.pop("VK_ICD_FILENAMES", None)

    server_entry = _existing_server_entry(server_exe)
    server_process: subprocess.Popen[str] | None = None
    if server_entry is None:
        if manage_server:
            server_process = _launch_process(
                server_exe, [str(server_port)], STATE_DIR / "server.stdout.log",
                STATE_DIR / "server.stderr.log", environment, launch_build_dir,
            )
            server_entry = {"pid": server_process.pid, "owned": True, "path": str(server_exe)}
        else:
            # The separate server batch owns this process. Record that boundary
            # so rejection and stop paths can never terminate it.
            server_entry = {"pid": 0, "owned": False, "path": str(server_exe)}
    client_arguments = ["127.0.0.1", str(server_port), "--dev-control", "--control-port", str(control_port)]
    if benchmark:
        client_arguments.append("--benchmark")
    client_process = _launch_process(
        client_exe, client_arguments, STATE_DIR / "client.stdout.log",
        STATE_DIR / "client.stderr.log", environment, launch_build_dir,
    )
    pending_state = {
        "server": server_entry,
        "client": {"pid": client_process.pid, "owned": True, "path": str(client_exe)},
        "server_port": server_port,
        "control_port": control_port,
        "benchmark": benchmark,
    }
    deadline = time.monotonic() + timeout
    raw = None
    while time.monotonic() < deadline:
        if client_process.poll() is not None:
            break
        try:
            raw = send_request("status", port=control_port, timeout=min(2.0, max(0.25, deadline - time.monotonic())))
            break
        except ControlError:
            time.sleep(0.25)
    try:
        if raw is None:
            raise LaunchError("development-control client did not answer before the startup deadline")
        verified = verify_control_status(
            raw, requested_renderer=renderer, selection=selection, allow_fallback=allow_fallback
        )
        if benchmark and not raw.get("benchmark_enabled", False):
            raise LaunchError("client started without the required --benchmark option")
    except LaunchError as error:
        cleanup_owned(pending_state)
        stderr = _tail(STATE_DIR / "client.stderr.log")
        icd = "none" if selection is None else f"{selection.get('icd_path')} sha256={selection.get('icd_sha256')}"
        raise LaunchError(f"{error}\nSelected ICD: {icd}\nVulkan/client errors:\n{stderr or '(none recorded)'}") from error
    pending_state["launch"] = verified
    _write_state(pending_state)
    verified["client_executable"] = str(client_exe)
    verified["build_directory"] = str(launch_build_dir)
    return verified


def stop_owned() -> dict[str, Any]:
    state = _read_state()
    stopped = cleanup_owned(state)
    if STATE_PATH.exists():
        STATE_PATH.unlink()
    return {"stopped": stopped, "left_unowned_running": bool(state) and len(stopped) == 0}


def restart_owned(
    *, renderer: str = "gpu", allow_fallback: bool = False, timeout: float = 20.0
) -> dict[str, Any]:
    state = _read_state()
    client = state.get("client") if state else None
    if not isinstance(client, dict) or not client.get("owned") or not _entry_matches(client):
        raise LaunchError("restart requires a running client owned by the development launcher")
    server = state.get("server") if state else None
    manage_server = bool(isinstance(server, dict) and server.get("owned"))
    stopped = cleanup_owned(state)
    if "client" not in stopped:
        raise LaunchError("the owned client could not be stopped safely")

    deadline = time.monotonic() + min(timeout, 5.0)
    stopped_entries = [
        entry for name in stopped
        if isinstance((entry := state.get(name)), dict)
    ]
    while any(_entry_matches(entry) for entry in stopped_entries) and time.monotonic() < deadline:
        time.sleep(0.05)
    if any(_entry_matches(entry) for entry in stopped_entries):
        raise LaunchError("an owned LG Duel process did not stop before the restart deadline")
    if STATE_PATH.exists():
        STATE_PATH.unlink()

    status = ensure_client(
        renderer=renderer,
        allow_fallback=allow_fallback,
        benchmark=bool(state.get("benchmark", False)),
        manage_server=manage_server,
        server_port=int(state.get("server_port", 27960)),
        control_port=int(state.get("control_port", 27961)),
        timeout=timeout,
    )
    return {"stopped": stopped, "status": status}


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Launch LG Duel with renderer verification")
    commands = parser.add_subparsers(dest="action", required=True)
    start = commands.add_parser("start")
    start.add_argument("--server-port", type=int, default=27960)
    start.add_argument("--control-port", type=int, default=27961)
    start.add_argument("--renderer", choices=("gpu", "fallback"), default="gpu")
    start.add_argument("--allow-fallback", action="store_true")
    start.add_argument(
        "--external-server", action="store_true",
        help="launch and own only the client; connect to a separately managed server",
    )
    start.add_argument("--benchmark", action="store_true")
    start.add_argument("--timeout", type=float, default=20.0)
    status = commands.add_parser("status")
    status.add_argument("--control-port", type=int, default=27961)
    status.add_argument("--timeout", type=float, default=2.0)
    commands.add_parser("stop")
    restart = commands.add_parser("restart")
    restart.add_argument("--renderer", choices=("gpu", "fallback"), default="gpu")
    restart.add_argument("--allow-fallback", action="store_true")
    restart.add_argument("--timeout", type=float, default=20.0)
    parser.add_argument("--json", action="store_true")
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    arguments = parser.parse_args(argv)
    try:
        if arguments.action == "start":
            result = ensure_client(
                renderer=arguments.renderer, allow_fallback=arguments.allow_fallback,
                benchmark=arguments.benchmark, server_port=arguments.server_port,
                control_port=arguments.control_port, timeout=arguments.timeout,
                manage_server=not arguments.external_server,
            )
        elif arguments.action == "status":
            result = status_with_state(port=arguments.control_port, timeout=arguments.timeout)
        elif arguments.action == "restart":
            result = restart_owned(
                renderer=arguments.renderer,
                allow_fallback=arguments.allow_fallback,
                timeout=arguments.timeout,
            )
        else:
            result = stop_owned()
    except (LaunchError, ControlError) as error:
        if arguments.json:
            print(json.dumps({"ok": False, "error": str(error)}, separators=(",", ":")))
        else:
            print(f"LG launch error: {error}", file=sys.stderr)
        return 1
    print(json.dumps(result, separators=(",", ":")) if arguments.json else json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
