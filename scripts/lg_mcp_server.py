#!/usr/bin/env python3
"""Thin stdio MCP adapter for LG Duel's independent control socket."""

from __future__ import annotations

import base64
import json
import sys
from pathlib import Path
from typing import Any

from lg_control import ControlError, load_preset, send_request
from lg_launch import LaunchError, ensure_client, status_with_state
from lg_benchmark import (
    BenchmarkError, compare_results, create_baseline, list_scenarios, load_result, run_benchmark,
)


SERVER_INFO = {"name": "lg-duel-dev-control", "version": "1.0.0"}
PROTOCOL_VERSION = "2025-06-18"


TOOLS: list[dict[str, Any]] = [
    {
        "name": "lg_start",
        "description": "Start or attach to LG Duel and verify the requested renderer before reuse.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "renderer": {"type": "string", "enum": ["gpu", "fallback"], "default": "gpu"},
                "allow_fallback": {"type": "boolean", "default": False},
            },
            "additionalProperties": False,
        },
    },
    {
        "name": "lg_status",
        "description": "Get structured LG Duel client, server, map, camera, renderer, and capture status.",
        "inputSchema": {"type": "object", "properties": {}, "additionalProperties": False},
    },
    {
        "name": "lg_load_map",
        "description": "Request an authoritative map load and wait for the new active map revision.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "map": {"type": "string", "pattern": "^[A-Za-z0-9_-]+(?:\\.map)?$"},
                "allow_fallback": {"type": "boolean", "default": False},
            },
            "required": ["map"], "additionalProperties": False,
        },
    },
    {
        "name": "lg_reload_map",
        "description": "Reload the active authoritative map and wait for its revision to advance.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "map": {"type": "string", "description": "Optional expected current map safety check."},
                "allow_fallback": {"type": "boolean", "default": False},
            },
            "additionalProperties": False,
        },
    },
    {
        "name": "lg_get_camera",
        "description": "Get the effective development or player camera transform.",
        "inputSchema": {
            "type": "object",
            "properties": {"allow_fallback": {"type": "boolean", "default": False}},
            "additionalProperties": False,
        },
    },
    {
        "name": "lg_set_camera",
        "description": "Enable and place the presentation-only deterministic development camera.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "position": {"type": "array", "items": {"type": "number"}, "minItems": 3, "maxItems": 3},
                "yaw": {"type": "number"},
                "pitch": {"type": "number", "minimum": -89.9, "maximum": 89.9},
                "fov": {"type": "number", "minimum": 30, "maximum": 140},
                "allow_fallback": {"type": "boolean", "default": False},
            },
            "required": ["position", "yaw", "pitch"], "additionalProperties": False,
        },
    },
    {
        "name": "lg_capture_screenshot",
        "description": "Capture the actual rendered game view after camera/map state has completed a frame.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "name": {"type": "string", "pattern": "^[A-Za-z0-9_-]+$"},
                "hide_hud": {"type": "boolean", "default": True},
                "hide_overlays": {"type": "boolean", "default": True},
                "allow_fallback": {"type": "boolean", "default": False},
            },
            "additionalProperties": False,
        },
    },
    {
        "name": "lg_set_collision_debug",
        "description": "Set the renderer-only collision visualization mode without changing authoritative collision.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "mode": {"type": "integer", "minimum": 0, "maximum": 5},
                "allow_fallback": {"type": "boolean", "default": False},
            },
            "required": ["mode"], "additionalProperties": False,
        },
    },
    {
        "name": "lg_capture_map_views",
        "description": "Load a named camera preset and capture all of its deterministic views plus a manifest.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "map": {"type": "string", "pattern": "^[A-Za-z0-9_-]+(?:\\.map)?$"},
                "preset": {"type": "string", "pattern": "^[A-Za-z0-9_-]+$", "default": "standard"},
                "allow_fallback": {"type": "boolean", "default": False},
            },
            "required": ["map"], "additionalProperties": False,
        },
    },
    {
        "name": "lg_list_benchmarks",
        "description": "List validated offline benchmark scenarios and their canonical hashes.",
        "inputSchema": {"type": "object", "properties": {}, "additionalProperties": False},
    },
    {
        "name": "lg_run_benchmark",
        "description": "Run a named offline benchmark scenario and return its aggregate artifact reference.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "scenario": {"type": "string", "pattern": "^[A-Za-z0-9][A-Za-z0-9_.-]{0,79}$"},
                "repetitions": {"type": "integer", "minimum": 1, "maximum": 100, "default": 3},
                "port": {"type": "integer", "minimum": 1, "maximum": 65535, "default": 27961},
                "timeout": {"type": "number", "exclusiveMinimum": 0, "maximum": 3600, "default": 180},
                "build_mode": {"type": "string", "enum": ["release", "debug"], "default": "release"},
            },
            "required": ["scenario"], "additionalProperties": False,
        },
    },
    {
        "name": "lg_compare_benchmarks",
        "description": "Compare a named baseline with a result artifact using tail-aware noise thresholds.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "baseline": {"type": "string", "pattern": "^[A-Za-z0-9][A-Za-z0-9_.-]{0,79}$"},
                "result": {"type": "string", "minLength": 1},
                "threshold_percent": {"type": "number", "minimum": 0, "maximum": 100, "default": 3},
                "tail_threshold_percent": {"type": "number", "minimum": 0, "maximum": 100, "default": 3},
            },
            "required": ["baseline", "result"], "additionalProperties": False,
        },
    },
    {
        "name": "lg_get_benchmark_result",
        "description": "Read a benchmark aggregate by artifact path; detailed run data is opt-in.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "result": {"type": "string", "minLength": 1},
                "detailed": {"type": "boolean", "default": False},
            },
            "required": ["result"], "additionalProperties": False,
        },
    },
    {
        "name": "lg_create_benchmark_baseline",
        "description": "Run a scenario and create an immutable named benchmark baseline.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "scenario": {"type": "string", "pattern": "^[A-Za-z0-9][A-Za-z0-9_.-]{0,79}$"},
                "name": {"type": "string", "pattern": "^[A-Za-z0-9][A-Za-z0-9_.-]{0,79}$"},
                "repetitions": {"type": "integer", "minimum": 1, "maximum": 100, "default": 3},
                "port": {"type": "integer", "minimum": 1, "maximum": 65535, "default": 27961},
                "timeout": {"type": "number", "exclusiveMinimum": 0, "maximum": 3600, "default": 180},
                "build_mode": {"type": "string", "enum": ["release", "debug"], "default": "release"},
            },
            "required": ["scenario", "name"], "additionalProperties": False,
        },
    },
]


def invoke_tool(name: str, arguments: dict[str, Any]) -> dict[str, Any]:
    if name == "lg_start":
        renderer = str(arguments.get("renderer", "gpu"))
        allow_fallback = bool(arguments.get("allow_fallback", False))
        if renderer == "fallback" and not allow_fallback:
            raise LaunchError("renderer='fallback' requires allow_fallback=true")
        return ensure_client(renderer=renderer, allow_fallback=allow_fallback)
    if name == "lg_status":
        return status_with_state()
    if name in {
        "lg_load_map", "lg_reload_map", "lg_get_camera", "lg_set_camera",
        "lg_set_collision_debug", "lg_capture_screenshot", "lg_capture_map_views",
    }:
        allow_fallback = bool(arguments.get("allow_fallback", False))
        ensure_client(
            renderer="fallback" if allow_fallback else "gpu",
            allow_fallback=allow_fallback,
        )
    if name == "lg_load_map":
        return send_request("load_map", map=arguments["map"])
    if name == "lg_reload_map":
        expected = arguments.get("map")
        if expected:
            status = send_request("status")
            if status.get("map") != str(expected).removesuffix(".map"):
                raise ControlError(f"current map is '{status.get('map', '')}', not expected '{expected}'")
        return send_request("reload_map")
    if name == "lg_get_camera":
        return send_request("get_camera")
    if name == "lg_set_camera":
        return send_request(
            "set_camera", position=arguments["position"], yaw=arguments["yaw"],
            pitch=arguments["pitch"], fov=arguments.get("fov")
        )
    if name == "lg_set_collision_debug":
        mode = arguments["mode"]
        if isinstance(mode, bool) or not isinstance(mode, int) or not 0 <= mode <= 5:
            raise ControlError("mode must be an integer between 0 and 5")
        return send_request("set_collision_debug", mode=mode)
    if name == "lg_capture_screenshot":
        return send_request(
            "capture_screenshot", name=arguments.get("name"),
            hide_hud=arguments.get("hide_hud", True),
            hide_overlays=arguments.get("hide_overlays", True),
        )
    if name == "lg_capture_map_views":
        map_name = str(arguments["map"]).removesuffix(".map")
        preset = str(arguments.get("preset", "standard"))
        return send_request(
            "capture_map_views", map=map_name, preset=preset,
            views=load_preset(map_name, preset)
        )
    if name == "lg_list_benchmarks":
        return list_scenarios()
    if name == "lg_run_benchmark":
        result = run_benchmark(
            arguments["scenario"], repetitions=arguments.get("repetitions", 3),
            port=arguments.get("port", 27961), timeout=arguments.get("timeout", 180.0),
            build_mode=arguments.get("build_mode", "release"),
        )
        summary = {key: value for key, value in result.items() if key != "runs"}
        summary["screenshots"] = [shot for run in result.get("runs", []) for shot in run.get("screenshots", [])]
        return summary
    if name == "lg_compare_benchmarks":
        return compare_results(
            arguments["baseline"], arguments["result"],
            threshold_percent=arguments.get("threshold_percent", 3.0),
            tail_threshold_percent=arguments.get("tail_threshold_percent", 3.0),
        )
    if name == "lg_get_benchmark_result":
        return load_result(arguments["result"], detailed=arguments.get("detailed", False))
    if name == "lg_create_benchmark_baseline":
        result = create_baseline(
            arguments["scenario"], arguments["name"], repetitions=arguments.get("repetitions", 3),
            port=arguments.get("port", 27961), timeout=arguments.get("timeout", 180.0),
            build_mode=arguments.get("build_mode", "release"),
        )
        summary = {key: value for key, value in result.items() if key != "runs"}
        summary["screenshots"] = [shot for run in result.get("runs", []) for shot in run.get("screenshots", [])]
        return summary
    raise ControlError(f"unknown LG Duel tool '{name}'")


def image_content(path_text: str) -> dict[str, Any] | None:
    path = Path(path_text)
    try:
        data = path.read_bytes()
    except OSError:
        return None
    if not data.startswith(b"\x89PNG\r\n\x1a\n"):
        return None
    return {"type": "image", "data": base64.b64encode(data).decode("ascii"), "mimeType": "image/png"}


def tool_result(result: dict[str, Any]) -> dict[str, Any]:
    content: list[dict[str, Any]] = [
        {"type": "text", "text": json.dumps(result, indent=2, ensure_ascii=False)}
    ]
    if isinstance(result.get("path"), str):
        image = image_content(result["path"])
        if image:
            content.append(image)
    views = result.get("views")
    if isinstance(views, list):
        for view in views:
            if isinstance(view, dict) and isinstance(view.get("path"), str):
                image = image_content(view["path"])
                if image:
                    content.append(image)
    screenshots = result.get("screenshots")
    if isinstance(screenshots, list):
        for screenshot in screenshots:
            path = screenshot.get("path") if isinstance(screenshot, dict) else screenshot
            if isinstance(path, str):
                image = image_content(path)
                if image:
                    content.append(image)
    return {"content": content, "structuredContent": result, "isError": False}


def handle(request: dict[str, Any]) -> dict[str, Any] | None:
    request_id = request.get("id")
    method = request.get("method")
    if request_id is None:
        return None
    if method == "initialize":
        return {
            "jsonrpc": "2.0", "id": request_id,
            "result": {
                "protocolVersion": PROTOCOL_VERSION,
                "capabilities": {"tools": {"listChanged": False}},
                "serverInfo": SERVER_INFO,
                "instructions": (
                    "Visual tools start or attach to a verified SDL_GPU/vulkan session by default. "
                    "Fallback requires explicit allow_fallback=true."
                ),
            },
        }
    if method == "ping":
        return {"jsonrpc": "2.0", "id": request_id, "result": {}}
    if method == "tools/list":
        return {"jsonrpc": "2.0", "id": request_id, "result": {"tools": TOOLS}}
    if method == "tools/call":
        params = request.get("params", {})
        name = params.get("name", "") if isinstance(params, dict) else ""
        arguments = params.get("arguments", {}) if isinstance(params, dict) else {}
        if not isinstance(arguments, dict):
            arguments = {}
        try:
            result = invoke_tool(str(name), arguments)
            payload = tool_result(result)
        except (ControlError, LaunchError, BenchmarkError, KeyError, TypeError, ValueError) as error:
            payload = {
                "content": [{"type": "text", "text": f"LG Duel tool error: {error}"}],
                "isError": True,
            }
        return {"jsonrpc": "2.0", "id": request_id, "result": payload}
    return {
        "jsonrpc": "2.0", "id": request_id,
        "error": {"code": -32601, "message": f"Method not found: {method}"},
    }


def main() -> int:
    for line in sys.stdin.buffer:
        try:
            request = json.loads(line)
            if not isinstance(request, dict):
                raise ValueError("JSON-RPC message must be an object")
            response = handle(request)
            if response is not None:
                sys.stdout.write(json.dumps(response, separators=(",", ":")) + "\n")
                sys.stdout.flush()
        except (json.JSONDecodeError, ValueError) as error:
            response = {
                "jsonrpc": "2.0", "id": None,
                "error": {"code": -32700, "message": f"Parse error: {error}"},
            }
            sys.stdout.write(json.dumps(response, separators=(",", ":")) + "\n")
            sys.stdout.flush()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
