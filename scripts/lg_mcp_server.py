#!/usr/bin/env python3
"""Thin stdio MCP adapter for LG Duel's independent control socket."""

from __future__ import annotations

import base64
import json
import sys
from pathlib import Path
from typing import Any

from lg_control import ControlError, load_preset, send_request
from lg_launch import LaunchError, ensure_client, restart_owned, status_with_state, stop_owned
from lg_benchmark import (
    BenchmarkError, compare_results, create_baseline, list_scenarios, load_result, run_benchmark,
)
from lg_live_scenario import LiveScenarioError, run_live_scenario
from lg_map_edit import MapEditError, MapEditor


SERVER_INFO = {"name": "lg-duel-dev-control", "version": "1.1.0"}
PROTOCOL_VERSION = "2025-06-18"
MAP_EDITOR = MapEditor()


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
        "name": "lg_stop",
        "description": "Stop only LG Duel processes owned by the verified development launcher.",
        "inputSchema": {"type": "object", "properties": {}, "additionalProperties": False},
    },
    {
        "name": "lg_restart",
        "description": "Restart the launcher-owned LG Duel session and verify its renderer.",
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
        "name": "lg_exec_console",
        "description": "Run one bounded game-console command in the opt-in local development client.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "command": {"type": "string", "minLength": 1, "maxLength": 1024},
                "allow_fallback": {"type": "boolean", "default": False},
            },
            "required": ["command"], "additionalProperties": False,
        },
    },
    {
        "name": "lg_get_cvar",
        "description": "Read one game console variable by name.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "name": {"type": "string", "pattern": "^[A-Za-z0-9_]{1,64}$"},
                "allow_fallback": {"type": "boolean", "default": False},
            },
            "required": ["name"], "additionalProperties": False,
        },
    },
    {
        "name": "lg_set_cvar",
        "description": "Set one game console variable through its normal validation path.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "name": {"type": "string", "pattern": "^[A-Za-z0-9_]{1,64}$"},
                "value": {"type": "string", "minLength": 1, "maxLength": 256},
                "allow_fallback": {"type": "boolean", "default": False},
            },
            "required": ["name", "value"], "additionalProperties": False,
        },
    },
    {
        "name": "lg_send_input",
        "description": "Send bounded player input through the normal client command path for a fixed tick count.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "ticks": {"type": "integer", "minimum": 1, "maximum": 1250},
                "forward": {"type": "number", "minimum": -1, "maximum": 1},
                "right": {"type": "number", "minimum": -1, "maximum": 1},
                "up": {"type": "number", "minimum": -1, "maximum": 1},
                "yaw": {"type": "number", "minimum": -1000000, "maximum": 1000000},
                "pitch": {"type": "number", "minimum": -89.9, "maximum": 89.9},
                "attack": {"type": "boolean"},
                "jump": {"type": "boolean"},
                "dash": {"type": "boolean"},
                "crouch": {"type": "boolean"},
                "sneak": {"type": "boolean"},
                "zoom": {"type": "boolean"},
                "weapon": {"type": "string", "minLength": 1, "maxLength": 32},
                "allow_fallback": {"type": "boolean", "default": False},
            },
            "required": ["ticks"],
            "dependentRequired": {"yaw": ["pitch"], "pitch": ["yaw"]},
            "additionalProperties": False,
        },
    },
    {
        "name": "lg_wait_frames",
        "description": "Wait for a fixed number of rendered client frames.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "frames": {"type": "integer", "minimum": 1, "maximum": 600},
                "allow_fallback": {"type": "boolean", "default": False},
            },
            "required": ["frames"], "additionalProperties": False,
        },
    },
    {
        "name": "lg_set_player_view",
        "description": "Set the local player's view angles without moving the development camera.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "yaw": {"type": "number", "minimum": -1000000, "maximum": 1000000},
                "pitch": {"type": "number", "minimum": -89.9, "maximum": 89.9},
                "allow_fallback": {"type": "boolean", "default": False},
            },
            "required": ["yaw", "pitch"], "additionalProperties": False,
        },
    },
    {
        "name": "lg_set_player_weapon",
        "description": "Select the local player's weapon through the normal command path.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "weapon": {"type": "string", "minLength": 1, "maxLength": 32},
                "allow_fallback": {"type": "boolean", "default": False},
            },
            "required": ["weapon"], "additionalProperties": False,
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
        "name": "lg_run_live_scenario",
        "description": "Run one C++-validated client/server scenario and return its evidence directory.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "scenario": {"type": "string", "minLength": 1, "maxLength": 1024},
                "renderer": {"type": "string", "enum": ["gpu", "fallback"], "default": "gpu"},
                "allow_fallback": {"type": "boolean", "default": False},
                "timeout": {"type": "number", "exclusiveMinimum": 0, "maximum": 3600, "default": 60},
            },
            "required": ["scenario"], "additionalProperties": False,
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

MAP_NAME_SCHEMA = {
    "type": "string",
    "pattern": "^[A-Za-z0-9][A-Za-z0-9_-]{0,63}(?:\\.map)?$",
}
OBJECT_ID_SCHEMA = {
    "type": "string",
    "pattern": "^[a-z][a-z0-9_-]{0,63}$",
}
REVISION_SCHEMA = {"type": "string", "pattern": "^[0-9a-f]{64}$"}
ROLLBACK_SCHEMA = {"type": "string", "pattern": "^[0-9a-f]{32}$"}
VEC3_SCHEMA = {
    "type": "array",
    "items": {"type": "number", "minimum": -40000, "maximum": 40000},
    "minItems": 3,
    "maxItems": 3,
}
DRY_RUN_SCHEMA = {"type": "boolean", "default": False}

TOOLS.extend([
    {
        "name": "lg_map_list",
        "description": "List source maps, exact revisions, and MCP-managed object IDs.",
        "inputSchema": {"type": "object", "properties": {}, "additionalProperties": False},
    },
    {
        "name": "lg_map_get",
        "description": "Get the typed structure and exact revision of one MCP-managed map.",
        "inputSchema": {
            "type": "object",
            "properties": {"map": MAP_NAME_SCHEMA},
            "required": ["map"],
            "additionalProperties": False,
        },
    },
    {
        "name": "lg_map_create",
        "description": "Create a canonical MCP-managed map from a known template; existing maps are never replaced.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "map": MAP_NAME_SCHEMA,
                "template": {"type": "string", "enum": ["initial"], "default": "initial"},
                "dry_run": DRY_RUN_SCHEMA,
            },
            "required": ["map"],
            "additionalProperties": False,
        },
    },
    {
        "name": "lg_map_add_cuboid",
        "description": "Add one axis-aligned six-face cuboid to an MCP-managed map.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "map": MAP_NAME_SCHEMA,
                "id": OBJECT_ID_SCHEMA,
                "min": VEC3_SCHEMA,
                "max": VEC3_SCHEMA,
                "material": {"type": "string", "minLength": 1, "maxLength": 192},
                "expected_revision": REVISION_SCHEMA,
                "dry_run": DRY_RUN_SCHEMA,
            },
            "required": ["map", "id", "min", "max", "material", "expected_revision"],
            "additionalProperties": False,
        },
    },
    {
        "name": "lg_map_copy_cuboid",
        "description": "Copy an MCP-managed cuboid to a new stable ID and translate it.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "map": MAP_NAME_SCHEMA,
                "source_id": OBJECT_ID_SCHEMA,
                "new_id": OBJECT_ID_SCHEMA,
                "offset": VEC3_SCHEMA,
                "expected_revision": REVISION_SCHEMA,
                "dry_run": DRY_RUN_SCHEMA,
            },
            "required": ["map", "source_id", "new_id", "offset", "expected_revision"],
            "additionalProperties": False,
        },
    },
    {
        "name": "lg_map_translate_cuboid",
        "description": "Translate one MCP-managed cuboid by a bounded three-number offset.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "map": MAP_NAME_SCHEMA,
                "id": OBJECT_ID_SCHEMA,
                "offset": VEC3_SCHEMA,
                "expected_revision": REVISION_SCHEMA,
                "dry_run": DRY_RUN_SCHEMA,
            },
            "required": ["map", "id", "offset", "expected_revision"],
            "additionalProperties": False,
        },
    },
    {
        "name": "lg_map_resize_cuboid",
        "description": "Set the absolute min and max bounds of one MCP-managed cuboid.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "map": MAP_NAME_SCHEMA,
                "id": OBJECT_ID_SCHEMA,
                "min": VEC3_SCHEMA,
                "max": VEC3_SCHEMA,
                "expected_revision": REVISION_SCHEMA,
                "dry_run": DRY_RUN_SCHEMA,
            },
            "required": ["map", "id", "min", "max", "expected_revision"],
            "additionalProperties": False,
        },
    },
    {
        "name": "lg_map_delete_cuboid",
        "description": "Delete one MCP-managed cuboid by stable ID.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "map": MAP_NAME_SCHEMA,
                "id": OBJECT_ID_SCHEMA,
                "expected_revision": REVISION_SCHEMA,
                "dry_run": DRY_RUN_SCHEMA,
            },
            "required": ["map", "id", "expected_revision"],
            "additionalProperties": False,
        },
    },
    {
        "name": "lg_map_set_material",
        "description": "Set one existing texture or clip material on all faces of a managed cuboid.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "map": MAP_NAME_SCHEMA,
                "id": OBJECT_ID_SCHEMA,
                "material": {"type": "string", "minLength": 1, "maxLength": 192},
                "expected_revision": REVISION_SCHEMA,
                "dry_run": DRY_RUN_SCHEMA,
            },
            "required": ["map", "id", "material", "expected_revision"],
            "additionalProperties": False,
        },
    },
    {
        "name": "lg_map_set_entity_properties",
        "description": "Set typed world bounds or spawn transform properties on a managed template entity.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "map": MAP_NAME_SCHEMA,
                "entity_id": OBJECT_ID_SCHEMA,
                "origin": VEC3_SCHEMA,
                "angle": {"type": "number", "minimum": -40000, "maximum": 40000},
                "yaw": {"type": "number", "minimum": -40000, "maximum": 40000},
                "bounds_min": VEC3_SCHEMA,
                "bounds_max": VEC3_SCHEMA,
                "expected_revision": REVISION_SCHEMA,
                "dry_run": DRY_RUN_SCHEMA,
            },
            "required": ["map", "entity_id", "expected_revision"],
            "additionalProperties": False,
        },
    },
    {
        "name": "lg_map_validate",
        "description": "Run structural checks and the built LG Duel map validator on one managed source map.",
        "inputSchema": {
            "type": "object",
            "properties": {"map": MAP_NAME_SCHEMA},
            "required": ["map"],
            "additionalProperties": False,
        },
    },
    {
        "name": "lg_map_rollback",
        "description": "Restore the exact prior source bytes for one API transaction when its revision still matches.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "rollback_token": ROLLBACK_SCHEMA,
                "expected_revision": REVISION_SCHEMA,
                "dry_run": DRY_RUN_SCHEMA,
            },
            "required": ["rollback_token", "expected_revision"],
            "additionalProperties": False,
        },
    },
    {
        "name": "lg_map_validate_sync_reload",
        "description": "Validate, atomically sync to build/default/maps, then load or reload and return the authoritative revision.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "map": MAP_NAME_SCHEMA,
                "expected_revision": REVISION_SCHEMA,
                "allow_fallback": {"type": "boolean", "default": False},
            },
            "required": ["map", "expected_revision"],
            "additionalProperties": False,
        },
    },
])


def invoke_tool(name: str, arguments: dict[str, Any]) -> dict[str, Any]:
    if name == "lg_map_list":
        return MAP_EDITOR.list_maps()
    if name == "lg_map_get":
        return MAP_EDITOR.inspect(arguments["map"])
    if name == "lg_map_create":
        return MAP_EDITOR.create(
            arguments["map"], arguments.get("template", "initial"),
            dry_run=bool(arguments.get("dry_run", False)),
        )
    if name == "lg_map_add_cuboid":
        return MAP_EDITOR.add_cuboid(
            arguments["map"], arguments["id"], arguments["min"], arguments["max"],
            arguments["material"], arguments["expected_revision"],
            dry_run=bool(arguments.get("dry_run", False)),
        )
    if name == "lg_map_copy_cuboid":
        return MAP_EDITOR.copy_cuboid(
            arguments["map"], arguments["source_id"], arguments["new_id"],
            arguments["offset"], arguments["expected_revision"],
            dry_run=bool(arguments.get("dry_run", False)),
        )
    if name == "lg_map_translate_cuboid":
        return MAP_EDITOR.translate_cuboid(
            arguments["map"], arguments["id"], arguments["offset"],
            arguments["expected_revision"],
            dry_run=bool(arguments.get("dry_run", False)),
        )
    if name == "lg_map_resize_cuboid":
        return MAP_EDITOR.resize_cuboid(
            arguments["map"], arguments["id"], arguments["min"], arguments["max"],
            arguments["expected_revision"],
            dry_run=bool(arguments.get("dry_run", False)),
        )
    if name == "lg_map_delete_cuboid":
        return MAP_EDITOR.delete_cuboid(
            arguments["map"], arguments["id"], arguments["expected_revision"],
            dry_run=bool(arguments.get("dry_run", False)),
        )
    if name == "lg_map_set_material":
        return MAP_EDITOR.set_material(
            arguments["map"], arguments["id"], arguments["material"],
            arguments["expected_revision"],
            dry_run=bool(arguments.get("dry_run", False)),
        )
    if name == "lg_map_set_entity_properties":
        fields = ("origin", "angle", "yaw", "bounds_min", "bounds_max")
        return MAP_EDITOR.set_entity_properties(
            arguments["map"], arguments["entity_id"], arguments["expected_revision"],
            **{field: arguments[field] for field in fields if field in arguments},
            dry_run=bool(arguments.get("dry_run", False)),
        )
    if name == "lg_map_validate":
        return MAP_EDITOR.validate(arguments["map"])
    if name == "lg_map_rollback":
        return MAP_EDITOR.rollback(
            arguments["rollback_token"], arguments["expected_revision"],
            dry_run=bool(arguments.get("dry_run", False)),
        )
    if name == "lg_map_validate_sync_reload":
        allow_fallback = bool(arguments.get("allow_fallback", False))
        return MAP_EDITOR.validate_sync_reload(
            arguments["map"],
            arguments["expected_revision"],
            ensure_runtime=lambda: ensure_client(
                renderer="fallback" if allow_fallback else "gpu",
                allow_fallback=allow_fallback,
            ),
            status=lambda: send_request("status"),
            load=lambda map_name: send_request("load_map", map=map_name),
            reload_current=lambda: send_request("reload_map"),
        )
    if name == "lg_start":
        renderer = str(arguments.get("renderer", "gpu"))
        allow_fallback = bool(arguments.get("allow_fallback", False))
        if renderer == "fallback" and not allow_fallback:
            raise LaunchError("renderer='fallback' requires allow_fallback=true")
        return ensure_client(renderer=renderer, allow_fallback=allow_fallback)
    if name == "lg_status":
        return status_with_state()
    if name == "lg_stop":
        return stop_owned()
    if name == "lg_restart":
        renderer = str(arguments.get("renderer", "gpu"))
        allow_fallback = bool(arguments.get("allow_fallback", False))
        if renderer == "fallback" and not allow_fallback:
            raise LaunchError("renderer='fallback' requires allow_fallback=true")
        return restart_owned(renderer=renderer, allow_fallback=allow_fallback)
    if name in {
        "lg_load_map", "lg_reload_map", "lg_get_camera", "lg_set_camera",
        "lg_set_collision_debug", "lg_capture_screenshot", "lg_capture_map_views",
        "lg_exec_console", "lg_get_cvar", "lg_set_cvar", "lg_send_input",
        "lg_wait_frames", "lg_set_player_view", "lg_set_player_weapon",
    }:
        allow_fallback = bool(arguments.get("allow_fallback", False))
        ensure_client(
            renderer="fallback" if allow_fallback else "gpu",
            allow_fallback=allow_fallback,
        )
    if name == "lg_load_map":
        return send_request("load_map", map=arguments["map"])
    if name == "lg_exec_console":
        return send_request("exec_console", command=arguments["command"])
    if name == "lg_get_cvar":
        return send_request("get_cvar", name=arguments["name"])
    if name == "lg_set_cvar":
        return send_request("set_cvar", name=arguments["name"], value=arguments["value"])
    if name == "lg_send_input":
        fields = (
            "ticks", "forward", "right", "up", "yaw", "pitch", "attack", "jump",
            "dash", "crouch", "sneak", "zoom", "weapon",
        )
        return send_request("send_input", **{field: arguments[field] for field in fields if field in arguments})
    if name == "lg_wait_frames":
        return send_request("wait_frames", frames=arguments["frames"])
    if name == "lg_set_player_view":
        return send_request("set_player_view", yaw=arguments["yaw"], pitch=arguments["pitch"])
    if name == "lg_set_player_weapon":
        return send_request("set_player_weapon", weapon=arguments["weapon"])
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
    if name == "lg_run_live_scenario":
        renderer = str(arguments.get("renderer", "gpu"))
        allow_fallback = bool(arguments.get("allow_fallback", False))
        if renderer == "fallback" and not allow_fallback:
            raise ControlError("renderer='fallback' requires allow_fallback=true")
        scenario = Path(arguments["scenario"])
        if not scenario.suffix:
            scenario = Path(__file__).resolve().parents[1] / "scenarios" / "live" / f"{scenario}.json"
        return run_live_scenario(
            scenario, renderer=renderer, allow_fallback=allow_fallback,
            timeout=float(arguments.get("timeout", 60.0)),
        )
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
        except (
            ControlError, LaunchError, BenchmarkError, MapEditError,
            KeyError, OSError, TypeError, ValueError,
        ) as error:
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
