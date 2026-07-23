#!/usr/bin/env python3
"""CLI client shared by PowerShell and the LG Duel MCP server."""

from __future__ import annotations

import argparse
import json
import re
import socket
import sys
import time
import uuid
from pathlib import Path
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = 27961
PRESET_PATH = REPO_ROOT / "config" / "dev-camera-presets.json"


class ControlError(RuntimeError):
    pass


def send_request(
    operation: str,
    *,
    host: str = DEFAULT_HOST,
    port: int = DEFAULT_PORT,
    timeout: float = 60.0,
    **parameters: Any,
) -> dict[str, Any]:
    request = {
        "id": str(uuid.uuid4()),
        "control_protocol": 1,
        "operation": operation,
        **{key: value for key, value in parameters.items() if value is not None},
    }
    encoded = (json.dumps(request, separators=(",", ":")) + "\n").encode("utf-8")
    try:
        with socket.create_connection((host, port), timeout=min(timeout, 5.0)) as connection:
            connection.settimeout(timeout)
            connection.sendall(encoded)
            chunks: list[bytes] = []
            total = 0
            while True:
                chunk = connection.recv(65536)
                if not chunk:
                    raise ControlError("game closed the control connection without a response")
                newline = chunk.find(b"\n")
                if newline >= 0:
                    chunks.append(chunk[:newline])
                    break
                chunks.append(chunk)
                total += len(chunk)
                if total > 4 * 1024 * 1024:
                    raise ControlError("control response exceeded 4 MiB")
    except ConnectionRefusedError as error:
        raise ControlError(
            f"no LG Duel development-control client is listening on {host}:{port}; "
            "launch the client with --dev-control"
        ) from error
    except (TimeoutError, socket.timeout) as error:
        raise ControlError(
            f"control request timed out after {timeout:g} seconds; verify the client was "
            "launched with --dev-control and the control port matches"
        ) from error
    except OSError as error:
        raise ControlError(f"control connection failed: {error}") from error

    try:
        response = json.loads(b"".join(chunks).decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ControlError(f"game returned invalid JSON: {error}") from error
    if not isinstance(response, dict):
        raise ControlError("game returned a non-object response")
    if not response.get("ok"):
        detail = response.get("error", {})
        code = detail.get("code", "control_error") if isinstance(detail, dict) else "control_error"
        message = detail.get("message", str(detail)) if isinstance(detail, dict) else str(detail)
        raise ControlError(f"{code}: {message}")
    result = response.get("result")
    if not isinstance(result, dict):
        raise ControlError("game response did not contain an object result")
    return result


def load_preset(map_name: str, preset_name: str) -> list[dict[str, Any]]:
    try:
        document = json.loads(PRESET_PATH.read_text(encoding="utf-8"))
    except FileNotFoundError as error:
        raise ControlError(f"camera preset file is missing: {PRESET_PATH}") from error
    except json.JSONDecodeError as error:
        raise ControlError(f"camera preset file is invalid JSON: {error}") from error
    try:
        views = document["maps"][map_name]["presets"][preset_name]
    except (KeyError, TypeError) as error:
        raise ControlError(f"camera preset '{preset_name}' is not defined for map '{map_name}'") from error
    if not isinstance(views, list) or not views:
        raise ControlError(f"camera preset '{preset_name}' for '{map_name}' has no viewpoints")
    return views


def parse_position(text: str | list[str]) -> list[float]:
    parts = [text] if isinstance(text, str) else text
    try:
        values = [
            float(value.strip())
            for part in parts
            for value in part.split(",")
            if value.strip()
        ]
    except ValueError as error:
        raise argparse.ArgumentTypeError("position must contain three comma-separated numbers") from error
    if len(values) != 3:
        raise argparse.ArgumentTypeError("position must contain exactly three values")
    return values


def collision_debug_mode(text: str) -> int:
    try:
        mode = int(text)
    except ValueError as error:
        raise argparse.ArgumentTypeError("collision debug mode must be an integer from 0 to 5") from error
    if not 0 <= mode <= 5:
        raise argparse.ArgumentTypeError("collision debug mode must be an integer from 0 to 5")
    return mode


def bounded_integer(minimum: int, maximum: int):
    def parse(text: str) -> int:
        try:
            value = int(text)
        except ValueError as error:
            raise argparse.ArgumentTypeError(f"must be an integer from {minimum} to {maximum}") from error
        if not minimum <= value <= maximum:
            raise argparse.ArgumentTypeError(f"must be an integer from {minimum} to {maximum}")
        return value
    return parse


def bounded_number(minimum: float, maximum: float):
    def parse(text: str) -> float:
        try:
            value = float(text)
        except ValueError as error:
            raise argparse.ArgumentTypeError(f"must be a number from {minimum:g} to {maximum:g}") from error
        if not minimum <= value <= maximum:
            raise argparse.ArgumentTypeError(f"must be a number from {minimum:g} to {maximum:g}")
        return value
    return parse


def bounded_text(maximum: int, label: str, *, allow_empty: bool = False):
    def parse(text: str) -> str:
        if (not allow_empty and not text) or len(text) > maximum or any(
            not character.isprintable() for character in text
        ):
            minimum = 0 if allow_empty else 1
            raise argparse.ArgumentTypeError(
                f"{label} must contain {minimum} to {maximum} printable characters"
            )
        return text
    return parse


def cvar_name(text: str) -> str:
    if not re.fullmatch(r"[A-Za-z0-9_]{1,64}", text):
        raise argparse.ArgumentTypeError("cvar name must use letters, digits, or '_'")
    return text


def human_output(result: dict[str, Any]) -> str:
    return json.dumps(result, indent=2, ensure_ascii=False)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Control an opt-in LG Duel development client")
    parser.add_argument("--host", default=DEFAULT_HOST)
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    parser.add_argument("--timeout", type=float, default=60.0)
    parser.add_argument("--json", action="store_true", help="emit compact machine-readable JSON")
    parser.add_argument(
        "--allow-fallback", action="store_true",
        help="explicitly permit the SDL_Renderer diagnostic path for this control workflow",
    )
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("status")
    console = commands.add_parser("exec-console")
    console.add_argument("console_command", type=bounded_text(1024, "console command"))
    get_cvar = commands.add_parser("get-cvar")
    get_cvar.add_argument("name", type=cvar_name)
    set_cvar = commands.add_parser("set-cvar")
    set_cvar.add_argument("name", type=cvar_name)
    set_cvar.add_argument("value", type=bounded_text(256, "cvar value"))
    send_input = commands.add_parser("send-input")
    send_input.add_argument("--ticks", type=bounded_integer(1, 1250), required=True)
    send_input.add_argument("--forward", type=bounded_number(-1, 1))
    send_input.add_argument("--right", type=bounded_number(-1, 1))
    send_input.add_argument("--up", type=bounded_number(-1, 1))
    send_input.add_argument("--yaw", type=bounded_number(-1000000, 1000000))
    send_input.add_argument("--pitch", type=bounded_number(-89.9, 89.9))
    for action in ("attack", "jump", "dash", "crouch", "sneak", "zoom"):
        send_input.add_argument(
            f"--{action}", action=argparse.BooleanOptionalAction, default=None
        )
    send_input.add_argument("--weapon", type=bounded_text(32, "weapon"))
    wait_frames = commands.add_parser("wait-frames")
    wait_frames.add_argument("frames", type=bounded_integer(1, 600))
    player_view = commands.add_parser("set-player-view")
    player_view.add_argument("--yaw", type=bounded_number(-1000000, 1000000), required=True)
    player_view.add_argument("--pitch", type=bounded_number(-89.9, 89.9), required=True)
    player_weapon = commands.add_parser("set-player-weapon")
    player_weapon.add_argument("weapon", type=bounded_text(32, "weapon"))
    load = commands.add_parser("load-map")
    load.add_argument("map")
    reload_map = commands.add_parser("reload-map")
    reload_map.add_argument("map", nargs="?", help="optional safety check for the current map")
    commands.add_parser("get-camera")
    camera = commands.add_parser("set-camera")
    # PowerShell expands an unquoted comma expression into separate arguments,
    # while native shells pass one comma-delimited value. Accept both forms.
    camera.add_argument("--position", nargs="+", required=True)
    camera.add_argument("--yaw", type=float, required=True)
    camera.add_argument("--pitch", type=float, required=True)
    camera.add_argument("--fov", type=float)
    collision_debug = commands.add_parser("set-collision-debug")
    collision_debug.add_argument("mode", type=collision_debug_mode)
    capture = commands.add_parser("capture")
    capture.add_argument("--name")
    capture.add_argument("--show-hud", action="store_true")
    capture.add_argument("--show-overlays", action="store_true")
    views = commands.add_parser("capture-map-views")
    views.add_argument("--map", required=True)
    views.add_argument("--preset", default="standard")
    return parser


def execute(arguments: argparse.Namespace) -> dict[str, Any]:
    common = {"host": arguments.host, "port": arguments.port, "timeout": arguments.timeout}
    if arguments.command == "status":
        if arguments.host != DEFAULT_HOST:
            return send_request("status", **common)
        from lg_launch import status_with_state
        return status_with_state(port=arguments.port, timeout=arguments.timeout)
    if arguments.command == "send-input" and ((arguments.yaw is None) != (arguments.pitch is None)):
        raise ControlError("send-input requires --yaw and --pitch together")
    if arguments.host != DEFAULT_HOST:
        raise ControlError("development-control launch and visual verification require host 127.0.0.1")
    from lg_launch import LaunchError, ensure_client
    try:
        ensure_client(
            renderer="fallback" if arguments.allow_fallback else "gpu",
            allow_fallback=arguments.allow_fallback,
            control_port=arguments.port,
            timeout=min(arguments.timeout, 30.0),
        )
    except LaunchError as error:
        raise ControlError(str(error)) from error
    if arguments.command == "load-map":
        return send_request("load_map", map=arguments.map, **common)
    if arguments.command == "exec-console":
        return send_request("exec_console", command=arguments.console_command, **common)
    if arguments.command == "get-cvar":
        return send_request("get_cvar", name=arguments.name, **common)
    if arguments.command == "set-cvar":
        return send_request("set_cvar", name=arguments.name, value=arguments.value, **common)
    if arguments.command == "send-input":
        fields = (
            "ticks", "forward", "right", "up", "yaw", "pitch", "attack", "jump",
            "dash", "crouch", "sneak", "zoom", "weapon",
        )
        values = {field: getattr(arguments, field) for field in fields if getattr(arguments, field) is not None}
        return send_request("send_input", **values, **common)
    if arguments.command == "wait-frames":
        return send_request("wait_frames", frames=arguments.frames, **common)
    if arguments.command == "set-player-view":
        return send_request("set_player_view", yaw=arguments.yaw, pitch=arguments.pitch, **common)
    if arguments.command == "set-player-weapon":
        return send_request("set_player_weapon", weapon=arguments.weapon, **common)
    if arguments.command == "reload-map":
        status = send_request("status", **common)
        if arguments.map and status.get("map") != arguments.map.removesuffix(".map"):
            raise ControlError(
                f"current map is '{status.get('map', '')}', not requested safety check '{arguments.map}'"
            )
        return send_request("reload_map", **common)
    if arguments.command == "get-camera":
        return send_request("get_camera", **common)
    if arguments.command == "set-camera":
        return send_request(
            "set_camera", position=parse_position(arguments.position), yaw=arguments.yaw,
            pitch=arguments.pitch, fov=arguments.fov, **common
        )
    if arguments.command == "set-collision-debug":
        return send_request("set_collision_debug", mode=arguments.mode, **common)
    if arguments.command == "capture":
        return send_request(
            "capture_screenshot", name=arguments.name,
            hide_hud=not arguments.show_hud,
            hide_overlays=not arguments.show_overlays,
            **common,
        )
    if arguments.command == "capture-map-views":
        views = load_preset(arguments.map.removesuffix(".map"), arguments.preset)
        return send_request(
            "capture_map_views", map=arguments.map, preset=arguments.preset,
            views=views, **common
        )
    raise ControlError(f"unsupported command: {arguments.command}")


def main(argv: list[str] | None = None) -> int:
    raw = list(sys.argv[1:] if argv is None else argv)
    # PowerShell users naturally place --json after the subcommand; argparse
    # global options normally require it first, so normalize that one flag.
    json_requested = "--json" in raw
    fallback_requested = "--allow-fallback" in raw
    raw = [value for value in raw if value not in {"--json", "--allow-fallback"}]
    if json_requested:
        raw.insert(0, "--json")
    if fallback_requested:
        raw.insert(0, "--allow-fallback")
    parser = build_parser()
    arguments = parser.parse_args(raw)
    try:
        result = execute(arguments)
    except ControlError as error:
        if arguments.json:
            print(json.dumps({"ok": False, "error": str(error)}, separators=(",", ":")))
        else:
            print(f"LG control error: {error}", file=sys.stderr)
        return 1
    print(
        json.dumps(result, separators=(",", ":"), ensure_ascii=False)
        if arguments.json else human_output(result)
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
