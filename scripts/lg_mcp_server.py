#!/usr/bin/env python3
"""Thin stdio MCP adapter for LG Duel's independent control socket."""

from __future__ import annotations

import base64
import copy
import io
import json
import math
import os
import signal
import subprocess
import sys
import tempfile
import threading
from pathlib import Path
from typing import Any, Callable

try:
    from PIL import Image
except ImportError:  # setup-lg-mcp.ps1 installs the runtime dependency.
    Image = None

from lg_control import ControlError, load_preset, send_request
from lg_launch import LaunchError, ensure_client, restart_owned, status_with_state, stop_owned
from lg_benchmark import (
    BenchmarkError, DEFAULT_CONTROL_PORT as DEFAULT_BENCHMARK_CONTROL_PORT,
    DEFAULT_SERVER_PORT as DEFAULT_BENCHMARK_SERVER_PORT,
    compare_results, create_baseline, list_scenarios, load_result, run_benchmark,
)
from lg_live_scenario import LiveScenarioError, run_live_scenario
from lg_map_edit import MapEditError, MapEditor


SERVER_INFO = {"name": "lg-duel-dev-control", "version": "1.6.0"}
PROTOCOL_VERSION = "2025-06-18"
MAP_EDITOR = MapEditor()
INLINE_IMAGE_BUDGET = 1024 * 1024
DEFAULT_INLINE_IMAGE_FORMAT = "webp"
DEFAULT_INLINE_IMAGE_MAX_PIXELS = 1280 * 720
DEFAULT_INLINE_IMAGE_QUALITY = 82
MIN_INLINE_IMAGE_PIXELS = 320 * 180
MAX_SOURCE_IMAGE_PIXELS = 7680 * 4320
STRUCTURED_CONTENT_BUDGET = 256 * 1024
MCP_RESULT_BUDGET = 2 * 1024 * 1024
WORKER_STDOUT_BUDGET = 4 * 1024 * 1024
WORKER_STDERR_BUDGET = 64 * 1024
ERROR_MESSAGE_BUDGET = 32 * 1024
TOOL_TIMEOUTS = {
    "lg_status": 5.0,
    "lg_stop": 10.0,
    "lg_start": 30.0,
    "lg_restart": 40.0,
    "lg_run_live_scenario": 240.0,
    "lg_run_benchmark": 240.0,
    "lg_create_benchmark_baseline": 240.0,
}
DEFAULT_TOOL_TIMEOUT = 90.0
INLINE_IMAGE_PROPERTIES = {
    "inline_image_mode": {
        "type": "string",
        "enum": ["compact", "full"],
        "default": "compact",
        "description": (
            "Compact sends a capped agent copy and keeps the saved PNG full size; "
            "full sends the saved PNG unchanged when it fits the MCP limit."
        ),
    },
    "inline_image_format": {
        "type": "string",
        "enum": ["webp", "jpeg", "png"],
        "default": DEFAULT_INLINE_IMAGE_FORMAT,
        "description": "Format for the compact agent copy.",
    },
    "inline_image_max_pixels": {
        "type": "integer",
        "minimum": MIN_INLINE_IMAGE_PIXELS,
        "maximum": 3840 * 2160,
        "default": DEFAULT_INLINE_IMAGE_MAX_PIXELS,
        "description": "Pixel cap for the compact agent copy.",
    },
    "inline_image_quality": {
        "type": "integer",
        "minimum": 50,
        "maximum": 95,
        "default": DEFAULT_INLINE_IMAGE_QUALITY,
        "description": "Lossy quality for compact WebP or JPEG copies.",
    },
}
READ_ONLY_TOOLS = {
    "lg_status",
    "lg_list_benchmarks",
    "lg_compare_benchmarks",
    "lg_get_benchmark_result",
    "lg_map_list",
    "lg_map_get",
    "lg_map_list_point_lights",
    "lg_map_list_teleports",
    "lg_map_get_world_lighting",
    "lg_map_validate",
}
DEFERRED_CANCELLATION_TOOLS = {
    "lg_start",
    "lg_restart",
    "lg_load_map",
    "lg_reload_map",
    "lg_get_camera",
    "lg_set_camera",
    "lg_set_collision_debug",
    "lg_capture_screenshot",
    "lg_capture_map_views",
    "lg_exec_console",
    "lg_get_cvar",
    "lg_set_cvar",
    "lg_send_input",
    "lg_wait_frames",
    "lg_set_player_view",
    "lg_set_player_weapon",
    "lg_map_validate_sync_reload",
    "lg_run_live_scenario",
    "lg_run_benchmark",
    "lg_create_benchmark_baseline",
}
SPAWN_CAPABLE_TOOLS = set(DEFERRED_CANCELLATION_TOOLS)


def tool_timeout(name: str, arguments: dict[str, Any]) -> float:
    if name == "lg_run_live_scenario":
        requested = arguments.get("timeout", 60.0)
        try:
            return min(3660.0, max(1.0, float(requested)) + 60.0)
        except (TypeError, ValueError):
            return 240.0
    if name in {"lg_run_benchmark", "lg_create_benchmark_baseline"}:
        try:
            repetitions = min(100, max(1, int(arguments.get("repetitions", 3))))
            per_run = min(3600.0, max(1.0, float(arguments.get("timeout", 180.0))))
        except (TypeError, ValueError):
            return 240.0
        # Benchmark startup gets the same caller deadline as each measured
        # run, so reserve one extra full interval before per-run cleanup.
        return min(
            365_000.0,
            60.0 + (repetitions + 1) * (per_run + 5.0),
        )
    return TOOL_TIMEOUTS.get(name, DEFAULT_TOOL_TIMEOUT)


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
                **INLINE_IMAGE_PROPERTIES,
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
                **INLINE_IMAGE_PROPERTIES,
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
                "server_port": {
                    "type": "integer", "minimum": 1, "maximum": 65535,
                    "default": DEFAULT_BENCHMARK_SERVER_PORT,
                },
                "control_port": {
                    "type": "integer", "minimum": 1, "maximum": 65535,
                    "default": DEFAULT_BENCHMARK_CONTROL_PORT,
                },
                "port": {
                    "type": "integer", "minimum": 1, "maximum": 65535,
                    "description": "Legacy alias for control_port; both must match when set.",
                },
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
                "server_port": {
                    "type": "integer", "minimum": 1, "maximum": 65535,
                    "default": DEFAULT_BENCHMARK_SERVER_PORT,
                },
                "control_port": {
                    "type": "integer", "minimum": 1, "maximum": 65535,
                    "default": DEFAULT_BENCHMARK_CONTROL_PORT,
                },
                "port": {
                    "type": "integer", "minimum": 1, "maximum": 65535,
                    "description": "Legacy alias for control_port; both must match when set.",
                },
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
COLOR_SCHEMA = {
    "type": "array",
    "items": {"type": "number", "minimum": 0, "maximum": 255},
    "minItems": 3,
    "maxItems": 3,
}
LIGHT_FIELDS_SCHEMA = {
    "origin": VEC3_SCHEMA,
    "color": COLOR_SCHEMA,
    "intensity": {"type": "number", "exclusiveMinimum": 0, "maximum": 16},
    "radius": {"type": "number", "exclusiveMinimum": 0, "maximum": 4096},
    "casts_shadows": {"type": "boolean", "default": False},
    "source_radius": {"type": "number", "minimum": 0, "maximum": 1024, "default": 0},
    "priority": {"type": "integer", "minimum": -1000, "maximum": 1000, "default": 0},
    "flicker_enabled": {"type": "boolean", "default": False},
    "flicker_seed": {
        "type": "integer", "minimum": 0, "maximum": 4294967295, "default": 0
    },
    "flicker_frequency": {
        "type": "number", "minimum": 0.1, "maximum": 30
    },
    "flicker_min": {"type": "number", "minimum": 0, "maximum": 4, "default": 1},
    "flicker_max": {"type": "number", "minimum": 0, "maximum": 4, "default": 1},
}
WORLD_LIGHT_FIELDS_SCHEMA = {
    "ambient_color": COLOR_SCHEMA,
    "ambient_intensity": {"type": "number", "minimum": 0},
    "sun_enabled": {"type": "boolean"},
    "sun_id": OBJECT_ID_SCHEMA,
    "sun_direction": VEC3_SCHEMA,
    "sun_color": COLOR_SCHEMA,
    "sun_intensity": {"type": "number", "minimum": 0},
}
TELEPORT_FIELDS_SCHEMA = {
    "min": VEC3_SCHEMA,
    "max": VEC3_SCHEMA,
    "destination": VEC3_SCHEMA,
    "exit_yaw": {"type": "number", "minimum": -40000, "maximum": 40000},
}


def _batch_operation_schema(
    op: str, properties: dict[str, Any], required: list[str]
) -> dict[str, Any]:
    return {
        "type": "object",
        "properties": {
            "op": {"type": "string", "const": op},
            **properties,
        },
        "required": ["op", *required],
        "additionalProperties": False,
    }


BATCH_OPERATION_SCHEMA = {
    "oneOf": [
        _batch_operation_schema(
            "add_point_light",
            {"id": OBJECT_ID_SCHEMA, **LIGHT_FIELDS_SCHEMA},
            ["id", "origin", "color", "intensity", "radius"],
        ),
        _batch_operation_schema(
            "update_point_light",
            {"id": OBJECT_ID_SCHEMA, **LIGHT_FIELDS_SCHEMA},
            ["id"],
        ),
        _batch_operation_schema(
            "remove_point_light", {"id": OBJECT_ID_SCHEMA}, ["id"]
        ),
        _batch_operation_schema(
            "set_world_lighting", WORLD_LIGHT_FIELDS_SCHEMA, []
        ),
        _batch_operation_schema(
            "add_teleport",
            {"id": OBJECT_ID_SCHEMA, **TELEPORT_FIELDS_SCHEMA},
            ["id", "min", "max", "destination", "exit_yaw"],
        ),
        _batch_operation_schema(
            "update_teleport",
            {"id": OBJECT_ID_SCHEMA, **TELEPORT_FIELDS_SCHEMA},
            ["id"],
        ),
        _batch_operation_schema(
            "remove_teleport", {"id": OBJECT_ID_SCHEMA}, ["id"]
        ),
        _batch_operation_schema(
            "add_cuboid",
            {
                "id": OBJECT_ID_SCHEMA, "min": VEC3_SCHEMA, "max": VEC3_SCHEMA,
                "material": {"type": "string", "minLength": 1, "maxLength": 192},
            },
            ["id", "min", "max", "material"],
        ),
        _batch_operation_schema(
            "copy_cuboid",
            {
                "source_id": OBJECT_ID_SCHEMA, "new_id": OBJECT_ID_SCHEMA,
                "offset": VEC3_SCHEMA,
            },
            ["source_id", "new_id", "offset"],
        ),
        _batch_operation_schema(
            "translate_cuboid",
            {"id": OBJECT_ID_SCHEMA, "offset": VEC3_SCHEMA},
            ["id", "offset"],
        ),
        _batch_operation_schema(
            "resize_cuboid",
            {"id": OBJECT_ID_SCHEMA, "min": VEC3_SCHEMA, "max": VEC3_SCHEMA},
            ["id", "min", "max"],
        ),
        _batch_operation_schema(
            "delete_cuboid", {"id": OBJECT_ID_SCHEMA}, ["id"]
        ),
        _batch_operation_schema(
            "set_material",
            {
                "id": OBJECT_ID_SCHEMA,
                "material": {"type": "string", "minLength": 1, "maxLength": 192},
            },
            ["id", "material"],
        ),
        _batch_operation_schema(
            "set_entity_properties",
            {
                "entity_id": OBJECT_ID_SCHEMA, "origin": VEC3_SCHEMA,
                "angle": {
                    "type": "number", "minimum": -40000, "maximum": 40000
                },
                "yaw": {
                    "type": "number", "minimum": -40000, "maximum": 40000
                },
                "bounds_min": VEC3_SCHEMA, "bounds_max": VEC3_SCHEMA,
            },
            ["entity_id"],
        ),
    ]
}

TOOLS.extend([
    {
        "name": "lg_map_list",
        "description": "List source maps, exact revisions, edit modes, and typed object IDs.",
        "inputSchema": {"type": "object", "properties": {}, "additionalProperties": False},
    },
    {
        "name": "lg_map_get",
        "description": "Get typed editable structure and the exact revision of one project map.",
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
        "name": "lg_map_list_point_lights",
        "description": "List API-owned typed point lights and the exact revision of one project map.",
        "inputSchema": {
            "type": "object",
            "properties": {"map": MAP_NAME_SCHEMA},
            "required": ["map"],
            "additionalProperties": False,
        },
    },
    {
        "name": "lg_map_add_point_light",
        "description": "Add one typed point light with a stable ID to a project map.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "map": MAP_NAME_SCHEMA,
                "id": OBJECT_ID_SCHEMA,
                **LIGHT_FIELDS_SCHEMA,
                "expected_revision": REVISION_SCHEMA,
                "dry_run": DRY_RUN_SCHEMA,
            },
            "required": [
                "map", "id", "origin", "color", "intensity", "radius",
                "expected_revision",
            ],
            "additionalProperties": False,
        },
    },
    {
        "name": "lg_map_update_point_light",
        "description": "Update selected typed fields on one API-owned point light.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "map": MAP_NAME_SCHEMA,
                "id": OBJECT_ID_SCHEMA,
                **LIGHT_FIELDS_SCHEMA,
                "expected_revision": REVISION_SCHEMA,
                "dry_run": DRY_RUN_SCHEMA,
            },
            "required": ["map", "id", "expected_revision"],
            "additionalProperties": False,
        },
    },
    {
        "name": "lg_map_remove_point_light",
        "description": "Remove one API-owned point light by stable ID.",
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
        "name": "lg_map_list_teleports",
        "description": "List API-owned typed teleports and the exact revision of one project map.",
        "inputSchema": {
            "type": "object",
            "properties": {"map": MAP_NAME_SCHEMA},
            "required": ["map"],
            "additionalProperties": False,
        },
    },
    {
        "name": "lg_map_add_teleport",
        "description": "Add one API-owned trigger volume and linked exit.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "map": MAP_NAME_SCHEMA,
                "id": OBJECT_ID_SCHEMA,
                **TELEPORT_FIELDS_SCHEMA,
                "expected_revision": REVISION_SCHEMA,
                "dry_run": DRY_RUN_SCHEMA,
            },
            "required": [
                "map", "id", "min", "max", "destination", "exit_yaw",
                "expected_revision",
            ],
            "additionalProperties": False,
        },
    },
    {
        "name": "lg_map_update_teleport",
        "description": "Update selected fields on one API-owned teleport.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "map": MAP_NAME_SCHEMA,
                "id": OBJECT_ID_SCHEMA,
                **TELEPORT_FIELDS_SCHEMA,
                "expected_revision": REVISION_SCHEMA,
                "dry_run": DRY_RUN_SCHEMA,
            },
            "required": ["map", "id", "expected_revision"],
            "additionalProperties": False,
        },
    },
    {
        "name": "lg_map_remove_teleport",
        "description": "Remove one API-owned teleport and its generated linked exit.",
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
        "name": "lg_map_get_world_lighting",
        "description": "Get ambient and optional sun settings for one project map.",
        "inputSchema": {
            "type": "object",
            "properties": {"map": MAP_NAME_SCHEMA},
            "required": ["map"],
            "additionalProperties": False,
        },
    },
    {
        "name": "lg_map_set_world_lighting",
        "description": "Set typed world ambient fields and optional sun settings.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "map": MAP_NAME_SCHEMA,
                **WORLD_LIGHT_FIELDS_SCHEMA,
                "expected_revision": REVISION_SCHEMA,
                "dry_run": DRY_RUN_SCHEMA,
            },
            "required": ["map", "expected_revision"],
            "additionalProperties": False,
        },
    },
    {
        "name": "lg_map_apply_batch",
        "description": "Apply up to 128 typed geometry, entity, and lighting operations as one atomic map write.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "map": MAP_NAME_SCHEMA,
                "operations": {
                    "type": "array",
                    "items": BATCH_OPERATION_SCHEMA,
                    "minItems": 1,
                    "maxItems": 128,
                },
                "expected_revision": REVISION_SCHEMA,
                "dry_run": DRY_RUN_SCHEMA,
            },
            "required": ["map", "operations", "expected_revision"],
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

KNOWN_TOOL_NAMES = {
    str(tool["name"]) for tool in TOOLS if isinstance(tool.get("name"), str)
}


def _state_change_possible(name: str) -> bool:
    return name in KNOWN_TOOL_NAMES and name not in READ_ONLY_TOOLS


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
    if name == "lg_map_list_point_lights":
        return MAP_EDITOR.list_point_lights(arguments["map"])
    if name == "lg_map_add_point_light":
        fields = tuple(LIGHT_FIELDS_SCHEMA)
        return MAP_EDITOR.add_point_light(
            arguments["map"], arguments["id"], arguments["origin"],
            arguments["color"], arguments["intensity"], arguments["radius"],
            arguments["expected_revision"],
            **{
                field: arguments[field] for field in fields
                if field not in {"origin", "color", "intensity", "radius"}
                and field in arguments
            },
            dry_run=bool(arguments.get("dry_run", False)),
        )
    if name == "lg_map_update_point_light":
        return MAP_EDITOR.update_point_light(
            arguments["map"], arguments["id"], arguments["expected_revision"],
            **{
                field: arguments[field] for field in LIGHT_FIELDS_SCHEMA
                if field in arguments
            },
            dry_run=bool(arguments.get("dry_run", False)),
        )
    if name == "lg_map_remove_point_light":
        return MAP_EDITOR.remove_point_light(
            arguments["map"], arguments["id"], arguments["expected_revision"],
            dry_run=bool(arguments.get("dry_run", False)),
        )
    if name == "lg_map_list_teleports":
        return MAP_EDITOR.list_teleports(arguments["map"])
    if name == "lg_map_add_teleport":
        return MAP_EDITOR.add_teleport(
            arguments["map"], arguments["id"], arguments["min"],
            arguments["max"], arguments["destination"], arguments["exit_yaw"],
            arguments["expected_revision"],
            dry_run=bool(arguments.get("dry_run", False)),
        )
    if name == "lg_map_update_teleport":
        return MAP_EDITOR.update_teleport(
            arguments["map"], arguments["id"], arguments["expected_revision"],
            **{
                field: arguments[field] for field in TELEPORT_FIELDS_SCHEMA
                if field in arguments
            },
            dry_run=bool(arguments.get("dry_run", False)),
        )
    if name == "lg_map_remove_teleport":
        return MAP_EDITOR.remove_teleport(
            arguments["map"], arguments["id"], arguments["expected_revision"],
            dry_run=bool(arguments.get("dry_run", False)),
        )
    if name == "lg_map_get_world_lighting":
        return MAP_EDITOR.get_world_lighting(arguments["map"])
    if name == "lg_map_set_world_lighting":
        return MAP_EDITOR.set_world_lighting(
            arguments["map"], arguments["expected_revision"],
            **{
                field: arguments[field] for field in WORLD_LIGHT_FIELDS_SCHEMA
                if field in arguments
            },
            dry_run=bool(arguments.get("dry_run", False)),
        )
    if name == "lg_map_apply_batch":
        return MAP_EDITOR.apply_batch(
            arguments["map"], arguments["operations"],
            arguments["expected_revision"],
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
            server_port=arguments.get(
                "server_port", DEFAULT_BENCHMARK_SERVER_PORT
            ),
            control_port=arguments.get("control_port"),
            port=arguments.get("port"),
            timeout=arguments.get("timeout", 180.0),
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
            server_port=arguments.get(
                "server_port", DEFAULT_BENCHMARK_SERVER_PORT
            ),
            control_port=arguments.get("control_port"),
            port=arguments.get("port"),
            timeout=arguments.get("timeout", 180.0),
            build_mode=arguments.get("build_mode", "release"),
        )
        summary = {key: value for key, value in result.items() if key != "runs"}
        summary["screenshots"] = [shot for run in result.get("runs", []) for shot in run.get("screenshots", [])]
        return summary
    raise ControlError(f"unknown LG Duel tool '{name}'")


def _inline_image_options(arguments: dict[str, Any] | None) -> dict[str, Any]:
    arguments = arguments or {}
    mode = arguments.get("inline_image_mode", "compact")
    if mode not in {"compact", "full"}:
        mode = "compact"
    image_format = arguments.get(
        "inline_image_format", DEFAULT_INLINE_IMAGE_FORMAT
    )
    if image_format not in {"webp", "jpeg", "png"}:
        image_format = DEFAULT_INLINE_IMAGE_FORMAT

    def bounded_int(key: str, default: int, minimum: int, maximum: int) -> int:
        value = arguments.get(key, default)
        if isinstance(value, bool):
            return default
        try:
            return min(maximum, max(minimum, int(value)))
        except (TypeError, ValueError):
            return default

    return {
        "mode": mode,
        "format": image_format,
        "max_pixels": bounded_int(
            "inline_image_max_pixels", DEFAULT_INLINE_IMAGE_MAX_PIXELS,
            MIN_INLINE_IMAGE_PIXELS, 3840 * 2160,
        ),
        "quality": bounded_int(
            "inline_image_quality", DEFAULT_INLINE_IMAGE_QUALITY, 50, 95
        ),
    }


def _full_image_content(
    path: Path, size: int, remaining_budget: int
) -> tuple[
    dict[str, Any] | None, dict[str, Any] | None,
    dict[str, Any] | None, int,
]:
    encoded_size = 4 * ((size + 2) // 3)
    if encoded_size > remaining_budget:
        return None, None, {
            "inline_image_omitted": "size_limit",
            "inline_image_size_bytes": size,
        }, 0
    try:
        with path.open("rb") as source:
            data = source.read(size + 1)
    except OSError:
        return None, None, None, 0
    actual_encoded_size = 4 * ((len(data) + 2) // 3)
    if len(data) > size or actual_encoded_size > remaining_budget:
        return None, None, {
            "inline_image_omitted": "size_limit",
            "inline_image_size_bytes": max(size, len(data)),
        }, 0
    if not data.startswith(b"\x89PNG\r\n\x1a\n"):
        return None, None, None, 0
    return (
        {
            "type": "image",
            "data": base64.b64encode(data).decode("ascii"),
            "mimeType": "image/png",
        },
        {
            "inline_image_mode": "full",
            "inline_image_format": "png",
            "inline_image_size_bytes": len(data),
            "inline_image_source_size_bytes": size,
        },
        None,
        actual_encoded_size,
    )


def _scaled_size(
    width: int, height: int, max_pixels: int
) -> tuple[int, int]:
    pixels = width * height
    if pixels <= max_pixels:
        return width, height
    scale = math.sqrt(max_pixels / pixels)
    return max(1, int(width * scale)), max(1, int(height * scale))


def _encode_compact_image(
    image: Any, image_format: str, quality: int
) -> bytes:
    output = io.BytesIO()
    if image_format == "webp":
        image.save(output, format="WEBP", quality=quality, method=5)
    elif image_format == "jpeg":
        image.save(
            output, format="JPEG", quality=quality, optimize=True,
            progressive=True, subsampling="4:2:0",
        )
    else:
        image.save(output, format="PNG", optimize=True, compress_level=9)
    return output.getvalue()


def _compact_image_content(
    path: Path,
    source_size: int,
    remaining_budget: int,
    options: dict[str, Any],
) -> tuple[
    dict[str, Any] | None, dict[str, Any] | None,
    dict[str, Any] | None, int,
]:
    if Image is None:
        return None, None, {
            "inline_image_omitted": "encoder_unavailable",
            "inline_image_size_bytes": source_size,
        }, 0
    image_format = options["format"]
    mime_type = {
        "webp": "image/webp", "jpeg": "image/jpeg", "png": "image/png",
    }[image_format]
    try:
        with Image.open(path) as source:
            width, height = source.size
            if width * height > MAX_SOURCE_IMAGE_PIXELS:
                return None, None, {
                    "inline_image_omitted": "source_pixel_limit",
                    "inline_image_size_bytes": source_size,
                    "inline_image_source_width": width,
                    "inline_image_source_height": height,
                }, 0
            source.load()
            image = source.convert("RGB")
    except (OSError, ValueError, Image.DecompressionBombError):
        return None, None, {
            "inline_image_omitted": "decode_failed",
            "inline_image_size_bytes": source_size,
        }, 0

    source_width, source_height = image.size
    max_pixels = options["max_pixels"]
    quality = options["quality"]
    encoded = b""
    while max_pixels >= MIN_INLINE_IMAGE_PIXELS:
        width, height = _scaled_size(
            source_width, source_height, max_pixels
        )
        candidate = image
        if candidate.size != (width, height):
            candidate = image.resize(
                (width, height), Image.Resampling.LANCZOS
            )
        try:
            encoded = _encode_compact_image(
                candidate, image_format, quality
            )
        except (KeyError, OSError, ValueError):
            return None, None, {
                "inline_image_omitted": "encode_failed",
                "inline_image_size_bytes": source_size,
            }, 0
        encoded_size = 4 * ((len(encoded) + 2) // 3)
        if encoded_size <= remaining_budget:
            return (
                {
                    "type": "image",
                    "data": base64.b64encode(encoded).decode("ascii"),
                    "mimeType": mime_type,
                },
                {
                    "inline_image_mode": "compact",
                    "inline_image_format": image_format,
                    "inline_image_width": width,
                    "inline_image_height": height,
                    "inline_image_pixels": width * height,
                    "inline_image_quality": quality,
                    "inline_image_size_bytes": len(encoded),
                    "inline_image_source_width": source_width,
                    "inline_image_source_height": source_height,
                    "inline_image_source_size_bytes": source_size,
                },
                None,
                encoded_size,
            )
        max_pixels = int(max_pixels * 0.75)
        if image_format != "png":
            quality = max(60, quality - 4)
    return None, None, {
        "inline_image_omitted": "size_limit",
        "inline_image_size_bytes": len(encoded) or source_size,
        "inline_image_source_size_bytes": source_size,
    }, 0


def image_content(
    path_text: str,
    remaining_budget: int,
    options: dict[str, Any] | None = None,
) -> tuple[
    dict[str, Any] | None, dict[str, Any] | None,
    dict[str, Any] | None, int,
]:
    path = Path(path_text)
    if path.suffix.lower() != ".png":
        return None, None, None, 0
    try:
        size = path.stat().st_size
    except OSError:
        return None, None, None, 0
    options = options or _inline_image_options(None)
    if options["mode"] == "full":
        return _full_image_content(path, size, remaining_budget)
    return _compact_image_content(path, size, remaining_budget, options)


def _json_size(value: Any) -> int:
    return len(
        json.dumps(
            value, separators=(",", ":"), ensure_ascii=False
        ).encode("utf-8")
    )


def _collect_result_paths(
    value: Any,
    *,
    prefix: str = "",
    found: list[dict[str, str]] | None = None,
) -> list[dict[str, str]]:
    found = found if found is not None else []
    if len(found) >= 256:
        return found
    if isinstance(value, dict):
        for key, child in value.items():
            field = f"{prefix}.{key}" if prefix else str(key)
            key_text = str(key).lower()
            if (
                isinstance(child, str)
                and (
                    key_text == "path"
                    or key_text.endswith("_path")
                    or key_text.endswith("_directory")
                    or key_text.endswith("_dir")
                )
            ):
                found.append({"field": field, "value": child})
            else:
                _collect_result_paths(child, prefix=field, found=found)
            if len(found) >= 256:
                break
    elif isinstance(value, list):
        for index, child in enumerate(value):
            _collect_result_paths(
                child, prefix=f"{prefix}[{index}]", found=found
            )
            if len(found) >= 256:
                break
    return found


def _reduced_summary(value: Any, *, depth: int = 0) -> Any:
    if value is None or isinstance(value, (bool, int, float)):
        return value
    if isinstance(value, str):
        encoded = value.encode("utf-8")
        if len(encoded) <= 8192:
            return value
        return (
            encoded[:8192].decode("utf-8", errors="ignore")
            + "\n[text omitted: size_limit]"
        )
    if isinstance(value, dict) and depth < 2:
        reduced: dict[str, Any] = {}
        for key, child in list(value.items())[:100]:
            if isinstance(child, list):
                continue
            reduced[str(key)[:256]] = _reduced_summary(
                child, depth=depth + 1
            )
        return reduced
    if isinstance(value, list) and len(value) <= 20:
        return [
            _reduced_summary(child, depth=depth + 1)
            for child in value
            if child is None or isinstance(child, (bool, int, float, str))
        ]
    return None


def _compact_structured_result(
    result: dict[str, Any],
    *,
    budget: int = STRUCTURED_CONTENT_BUDGET,
) -> dict[str, Any]:
    original_size = _json_size(result)
    if original_size <= budget:
        return copy.deepcopy(result)

    omitted_fields = [str(key)[:256] for key in result]
    compact: dict[str, Any] = {
        "structured_output_omitted": "size_limit",
        "structured_output_size_bytes": original_size,
        "structured_output_omitted_fields": omitted_fields[:100],
    }
    if len(omitted_fields) > 100:
        compact["structured_output_omitted_field_count"] = len(
            omitted_fields
        )

    priority = {
        "status",
        "classification",
        "valid",
        "scenario",
        "summary",
        "aggregate",
        "artifact",
        "artifact_path",
        "result_directory",
        "source_result_directory",
        "settings",
        "environment",
        "screenshots",
        "views",
    }
    ordered_keys = sorted(
        result,
        key=lambda key: (
            0
            if (
                str(key) in priority
                or str(key) == "path"
                or str(key).endswith("_path")
                or str(key).endswith("_directory")
            )
            else 1,
            str(key),
        ),
    )
    for key in ordered_keys:
        value = result[key]
        key_text = str(key)
        if key_text not in priority and not (
            value is None or isinstance(value, (bool, int, float, str))
        ):
            continue
        candidate = {**compact, key: copy.deepcopy(value)}
        if _json_size(candidate) <= budget:
            compact[key] = candidate[key]
            continue
        if key_text in priority:
            reduced = _reduced_summary(value)
            candidate = {**compact, key: reduced}
            if reduced is not None and _json_size(candidate) <= budget:
                compact[key] = reduced

    paths = _collect_result_paths(result)
    if paths:
        candidate = {
            **compact,
            "structured_output_preserved_paths": paths,
        }
        if _json_size(candidate) <= budget:
            compact["structured_output_preserved_paths"] = paths
    return compact


def tool_result(
    result: dict[str, Any],
    *,
    image_arguments: dict[str, Any] | None = None,
) -> dict[str, Any]:
    structured = _compact_structured_result(result)
    images: list[dict[str, Any]] = []
    omissions: list[dict[str, Any]] = []
    remaining = INLINE_IMAGE_BUDGET
    image_options = _inline_image_options(image_arguments)

    def add_image(owner: dict[str, Any], path_text: str) -> None:
        nonlocal remaining
        image, details, omission, used = image_content(
            path_text, remaining, image_options
        )
        if image is not None:
            images.append(image)
            remaining -= used
        if details is not None:
            owner.update(details)
        if omission is not None:
            owner.update(omission)
            omissions.append({"path": path_text, **omission})

    if isinstance(structured.get("path"), str):
        add_image(structured, structured["path"])
    views = structured.get("views")
    if isinstance(views, list):
        for view in views:
            if isinstance(view, dict) and isinstance(view.get("path"), str):
                add_image(view, view["path"])
    screenshots = structured.get("screenshots")
    if isinstance(screenshots, list):
        for screenshot in screenshots:
            path = screenshot.get("path") if isinstance(screenshot, dict) else screenshot
            if isinstance(path, str):
                if isinstance(screenshot, dict):
                    add_image(screenshot, path)
                else:
                    holder: dict[str, Any] = {}
                    add_image(holder, path)
    if omissions:
        structured["inline_image_omissions"] = omissions
    def make_payload() -> dict[str, Any]:
        content: list[dict[str, Any]] = [{
            "type": "text",
            "text": json.dumps(structured, indent=2, ensure_ascii=False),
        }]
        content.extend(images)
        return {
            "content": content,
            "structuredContent": structured,
            "isError": False,
        }

    payload = make_payload()
    removed_images = 0
    while images and _json_size(payload) > MCP_RESULT_BUDGET:
        images.pop()
        removed_images += 1
        structured["inline_images_omitted_result_limit"] = removed_images
        payload = make_payload()
    if _json_size(payload) > MCP_RESULT_BUDGET:
        structured = _compact_structured_result(
            structured, budget=64 * 1024
        )
        structured["mcp_result_omitted"] = "size_limit"
        payload = make_payload()
    return payload


def _error_payload(
    name: str,
    *,
    code: str,
    message: str,
    timeout: float | None = None,
    error_type: str | None = None,
) -> dict[str, Any]:
    state_change_possible = _state_change_possible(name)
    definitely_not_started = {
        "worker_start_failed",
        "worker_tree_unavailable",
        "server_busy",
    }
    encoded_message = message.encode("utf-8")
    message_omitted = len(encoded_message) > ERROR_MESSAGE_BUDGET
    if message_omitted:
        message = encoded_message[:ERROR_MESSAGE_BUDGET].decode(
            "utf-8", errors="ignore"
        )
        message += "\n[error text omitted: size_limit]"
    error: dict[str, Any] = {"code": code, "message": message}
    if message_omitted:
        error["message_omitted"] = "size_limit"
        error["message_size_bytes"] = len(encoded_message)
    if error_type:
        error["type"] = error_type
    structured: dict[str, Any] = {
        "ok": False,
        "tool": name,
        "error": error,
        "outcome": (
            "unknown"
            if state_change_possible and code not in definitely_not_started
            else "not_completed"
        ),
        "state_change_possible": state_change_possible,
    }
    if timeout is not None:
        structured["timeout_seconds"] = timeout
    return {
        "content": [{
            "type": "text",
            "text": json.dumps(structured, indent=2, ensure_ascii=False),
        }],
        "structuredContent": structured,
        "isError": True,
    }


def _worker_main() -> int:
    try:
        request = json.loads(sys.stdin.buffer.readline())
        if not isinstance(request, dict):
            raise ValueError("worker request must be an object")
        name = request.get("name")
        arguments = request.get("arguments")
        if not isinstance(name, str) or not isinstance(arguments, dict):
            raise ValueError("worker request requires a tool name and object arguments")
        try:
            result = invoke_tool(name, arguments)
            response = {
                "ok": True,
                "result": _compact_structured_result(result),
            }
        except (
            ControlError, LaunchError, BenchmarkError, LiveScenarioError,
            MapEditError,
            KeyError, OSError, TypeError, ValueError,
        ) as error:
            response = {
                "ok": False,
                "error": {
                    "code": "tool_error",
                    "type": type(error).__name__,
                    "message": str(error),
                },
            }
    except (json.JSONDecodeError, OSError, TypeError, ValueError) as error:
        response = {
            "ok": False,
            "error": {
                "code": "worker_protocol_error",
                "type": type(error).__name__,
                "message": str(error),
            },
        }
    sys.stdout.write(json.dumps(response, separators=(",", ":")) + "\n")
    sys.stdout.flush()
    return 0


class WorkerTree:
    """A worker and only the descendants that worker starts."""

    def __init__(
        self,
        worker: subprocess.Popen[str],
        *,
        job_handle: int | None = None,
        process_group: bool = False,
        contained: bool = True,
    ) -> None:
        self.worker = worker
        self.job_handle = job_handle
        self.process_group = process_group
        self.contained = contained
        self._lock = threading.Lock()
        self._terminated = False

    def terminate(self) -> None:
        with self._lock:
            if self._terminated:
                return
            self._terminated = True
            job_handle = self.job_handle
        try:
            if job_handle is not None:
                import ctypes

                kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
                kernel32.TerminateJobObject(
                    ctypes.c_void_p(job_handle), ctypes.c_uint(1)
                )
            elif self.process_group:
                os.killpg(
                    self.worker.pid,
                    getattr(signal, "SIGKILL", signal.SIGTERM),
                )
            else:
                self.worker.kill()
        except (OSError, ProcessLookupError):
            pass

    def close(self) -> None:
        with self._lock:
            job_handle = self.job_handle
            self.job_handle = None
        if job_handle is not None:
            import ctypes

            try:
                ctypes.WinDLL(
                    "kernel32", use_last_error=True
                ).CloseHandle(ctypes.c_void_p(job_handle))
            except OSError:
                pass


def _create_worker_tree(worker: subprocess.Popen[str]) -> WorkerTree:
    if os.name != "nt":
        return WorkerTree(worker, process_group=True)

    import ctypes

    handle = getattr(worker, "_handle", None)
    if handle is None:
        return WorkerTree(worker, contained=False)
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.CreateJobObjectW.restype = ctypes.c_void_p
    job_handle = kernel32.CreateJobObjectW(None, None)
    if not job_handle:
        raise OSError(ctypes.get_last_error(), "CreateJobObjectW failed")
    if not kernel32.AssignProcessToJobObject(
        ctypes.c_void_p(job_handle), ctypes.c_void_p(int(handle))
    ):
        error = ctypes.get_last_error()
        kernel32.CloseHandle(ctypes.c_void_p(job_handle))
        raise OSError(error, "AssignProcessToJobObject failed")
    return WorkerTree(worker, job_handle=int(job_handle))


class WorkerCancellation:
    def __init__(self, *, terminate_worker: bool = True) -> None:
        self._lock = threading.Lock()
        self._worker_tree: WorkerTree | None = None
        self._cancelled = False
        self._reason: str | None = None
        self._terminate_worker = terminate_worker

    @property
    def cancelled(self) -> bool:
        with self._lock:
            return self._cancelled

    @property
    def reason(self) -> str | None:
        with self._lock:
            return self._reason

    def attach(self, worker_tree: WorkerTree) -> bool:
        with self._lock:
            self._worker_tree = worker_tree
            cancelled = self._cancelled
        if cancelled and self._terminate_worker:
            worker_tree.terminate()
        return not cancelled

    def cancel(self, reason: str | None = None) -> None:
        with self._lock:
            self._cancelled = True
            if reason:
                self._reason = reason
            worker_tree = self._worker_tree
        if worker_tree is not None and self._terminate_worker:
            worker_tree.terminate()


def run_tool_supervised(
    name: str,
    arguments: dict[str, Any],
    cancellation: WorkerCancellation | None = None,
) -> tuple[dict[str, Any] | None, dict[str, Any] | None]:
    timeout = tool_timeout(name, arguments)
    cancellation = cancellation or WorkerCancellation()
    if cancellation.cancelled:
        return None, _error_payload(
            name,
            code="request_cancelled",
            message="tool call was cancelled before its worker started",
            timeout=timeout,
        )
    creationflags = getattr(subprocess, "CREATE_NO_WINDOW", 0)
    stdout_file = tempfile.TemporaryFile()
    stderr_file = tempfile.TemporaryFile()
    try:
        popen_arguments: dict[str, Any] = {
            "cwd": Path(__file__).resolve().parents[1],
            "stdin": subprocess.PIPE,
            "stdout": stdout_file,
            "stderr": stderr_file,
            "text": True,
            "encoding": "utf-8",
            "creationflags": creationflags,
        }
        if os.name != "nt":
            popen_arguments["start_new_session"] = True
        try:
            worker = subprocess.Popen(
                [sys.executable, str(Path(__file__).resolve()), "--worker"],
                **popen_arguments,
            )
        except OSError as error:
            return None, _error_payload(
                name,
                code="worker_start_failed",
                message=f"could not start tool worker: {error}",
                timeout=timeout,
                error_type=type(error).__name__,
            )
        try:
            worker_tree = _create_worker_tree(worker)
        except OSError as error:
            try:
                worker.kill()
            except OSError:
                pass
            return None, _error_payload(
                name,
                code="worker_tree_unavailable",
                message=f"could not contain tool worker: {error}",
                timeout=timeout,
                error_type=type(error).__name__,
            )
        if name in SPAWN_CAPABLE_TOOLS and not worker_tree.contained:
            worker_tree.terminate()
            worker_tree.close()
            return None, _error_payload(
                name,
                code="worker_tree_unavailable",
                message=(
                    "tool worker containment is unavailable; the call did not "
                    "start"
                ),
                timeout=timeout,
            )
        if not cancellation.attach(worker_tree):
            worker_tree.terminate()
            try:
                worker.communicate(timeout=5.0)
            except subprocess.TimeoutExpired:
                pass
            worker_tree.close()
            return None, _error_payload(
                name,
                code="request_cancelled",
                message="tool call was cancelled before its worker request",
                timeout=timeout,
            )
        request = json.dumps(
            {"name": name, "arguments": arguments}, separators=(",", ":")
        ) + "\n"
        monitor_stop = threading.Event()
        output_exceeded = threading.Event()

        def monitor_output() -> None:
            while not monitor_stop.wait(0.005):
                try:
                    stdout_size = os.fstat(stdout_file.fileno()).st_size
                    stderr_size = os.fstat(stderr_file.fileno()).st_size
                except OSError:
                    return
                if (
                    stdout_size > WORKER_STDOUT_BUDGET
                    or stderr_size > WORKER_STDERR_BUDGET
                ):
                    output_exceeded.set()
                    worker_tree.terminate()
                    return

        output_monitor = threading.Thread(
            target=monitor_output,
            name=f"lg-mcp-output-{name}",
            daemon=True,
        )
        output_monitor.start()
        try:
            try:
                returned_stdout, returned_stderr = worker.communicate(
                    request, timeout=timeout
                )
            except subprocess.TimeoutExpired:
                worker_tree.terminate()
                try:
                    worker.communicate(timeout=5.0)
                except subprocess.TimeoutExpired:
                    pass
                if cancellation.cancelled:
                    return None, _error_payload(
                        name,
                        code="request_cancelled",
                        message=(
                            "tool worker tree was stopped after its request "
                            "was cancelled"
                        ),
                        timeout=timeout,
                    )
                return None, _error_payload(
                    name,
                    code="worker_timeout",
                    message=(
                        f"tool worker exceeded its {timeout:g} second deadline"
                    ),
                    timeout=timeout,
                )
            finally:
                monitor_stop.set()
                output_monitor.join(timeout=0.1)
            if cancellation.cancelled:
                return None, _error_payload(
                    name,
                    code="request_cancelled",
                    message=(
                        "tool worker tree was stopped after its request was "
                        "cancelled"
                    ),
                    timeout=timeout,
                )
            stdout_file.seek(0)
            file_stdout = stdout_file.read(WORKER_STDOUT_BUDGET + 1)
            stderr_file.seek(0)
            file_stderr = stderr_file.read(WORKER_STDERR_BUDGET + 1)
            stdout_data = (
                returned_stdout.encode("utf-8")
                if isinstance(returned_stdout, str)
                else file_stdout
            )
            stderr_data = (
                returned_stderr.encode("utf-8")
                if isinstance(returned_stderr, str)
                else file_stderr
            )
            if (
                output_exceeded.is_set()
                or len(stdout_data) > WORKER_STDOUT_BUDGET
                or len(stderr_data) > WORKER_STDERR_BUDGET
            ):
                return None, _error_payload(
                    name,
                    code="worker_output_limit",
                    message=(
                        "tool worker output exceeded its stdout or stderr "
                        "byte limit"
                    ),
                    timeout=timeout,
                )
            stderr = stderr_data[:WORKER_STDERR_BUDGET].decode(
                "utf-8", errors="replace"
            )
            try:
                stdout = stdout_data.decode("utf-8", errors="strict")
            except UnicodeDecodeError as error:
                return None, _error_payload(
                    name,
                    code="worker_protocol_error",
                    message=f"worker returned invalid UTF-8: {error}",
                    timeout=timeout,
                )
            if worker.returncode != 0:
                detail = (
                    stderr.strip()[-4096:]
                    or f"worker exited with code {worker.returncode}"
                )
                return None, _error_payload(
                    name,
                    code="worker_exit",
                    message=detail,
                    timeout=timeout,
                )
            try:
                response = json.loads(stdout)
            except (UnicodeDecodeError, json.JSONDecodeError) as error:
                return None, _error_payload(
                    name,
                    code="worker_protocol_error",
                    message=f"worker returned invalid JSON: {error}",
                    timeout=timeout,
                )
            if not isinstance(response, dict):
                return None, _error_payload(
                    name,
                    code="worker_protocol_error",
                    message="worker returned a non-object response",
                    timeout=timeout,
                )
            if (
                response.get("ok") is True
                and isinstance(response.get("result"), dict)
            ):
                return response["result"], None
            detail = response.get("error")
            if not isinstance(detail, dict):
                detail = {
                    "code": "worker_protocol_error",
                    "message": (
                        "worker response did not contain an error object"
                    ),
                }
            return None, _error_payload(
                name,
                code=str(detail.get("code", "worker_error")),
                message=str(detail.get("message", "tool worker failed")),
                timeout=timeout,
                error_type=(
                    str(detail["type"])
                    if isinstance(detail.get("type"), str)
                    else None
                ),
            )
        finally:
            worker_tree.close()
    finally:
        stdout_file.close()
        stderr_file.close()


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
        result, error_payload = run_tool_supervised(str(name), arguments)
        payload = (
            error_payload
            if error_payload is not None
            else tool_result(result or {}, image_arguments=arguments)
        )
        return {"jsonrpc": "2.0", "id": request_id, "result": payload}
    return {
        "jsonrpc": "2.0", "id": request_id,
        "error": {"code": -32601, "message": f"Method not found: {method}"},
    }


class McpStdioDispatcher:
    """Keep stdio responsive while one supervised live call is running."""

    def __init__(self, writer: Callable[[dict[str, Any]], None]) -> None:
        self._writer = writer
        self._lock = threading.Lock()
        self._write_lock = threading.Lock()
        self._active: dict[str, WorkerCancellation] = {}
        self._active_threads: dict[str, threading.Thread] = {}
        self._normal_key: str | None = None
        self._stop_key: str | None = None
        self._threads: list[threading.Thread] = []

    @staticmethod
    def _key(request_id: Any) -> str:
        return (
            f"{type(request_id).__name__}:"
            f"{json.dumps(request_id, sort_keys=True, separators=(',', ':'))}"
        )

    def emit(self, response: dict[str, Any]) -> None:
        with self._write_lock:
            self._writer(response)

    def dispatch(self, request: dict[str, Any]) -> None:
        method = request.get("method")
        if method == "notifications/cancelled":
            self._cancel(request)
            return
        if method != "tools/call":
            response = handle(request)
            if response is not None:
                self.emit(response)
            return

        request_id = request.get("id")
        if request_id is None:
            return
        params = request.get("params", {})
        name = params.get("name", "") if isinstance(params, dict) else ""
        arguments = params.get("arguments", {}) if isinstance(params, dict) else {}
        if not isinstance(arguments, dict):
            arguments = {}
        name = str(name)
        key = self._key(request_id)
        cancellation = WorkerCancellation()
        busy = False
        duplicate = False
        predecessor_cancellation: WorkerCancellation | None = None
        predecessor_thread: threading.Thread | None = None
        thread: threading.Thread | None = None
        with self._lock:
            duplicate = key in self._active
            if not duplicate:
                if name == "lg_stop":
                    busy = self._stop_key is not None
                else:
                    busy = (
                        self._normal_key is not None
                        or self._stop_key is not None
                    )
            if not duplicate and not busy:
                self._active[key] = cancellation
                if name == "lg_stop":
                    self._stop_key = key
                    if self._normal_key is not None:
                        predecessor_cancellation = self._active.get(
                            self._normal_key
                        )
                        predecessor_thread = self._active_threads.get(
                            self._normal_key
                        )
                else:
                    self._normal_key = key
                thread = threading.Thread(
                    target=self._run_call,
                    args=(
                        key, request_id, name, arguments, cancellation,
                        predecessor_thread,
                    ),
                    name=f"lg-mcp-{name}",
                )
                self._threads.append(thread)
                self._active_threads[key] = thread
        if thread is not None:
            if predecessor_cancellation is not None:
                predecessor_cancellation.cancel("superseded by lg_stop")
            thread.start()
            return
        if duplicate:
            self.emit({
                "jsonrpc": "2.0",
                "id": request_id,
                "error": {
                    "code": -32600,
                    "message": "Duplicate active request id",
                },
            })
            return
        payload = _error_payload(
            name,
            code="server_busy",
            message=(
                "another live tool call is active; cancel it or wait for it "
                "to finish"
            ),
        )
        self.emit({
            "jsonrpc": "2.0", "id": request_id, "result": payload,
        })

    def _cancel(self, request: dict[str, Any]) -> None:
        params = request.get("params", {})
        if not isinstance(params, dict) or "requestId" not in params:
            return
        key = self._key(params["requestId"])
        reason = params.get("reason")
        with self._lock:
            cancellation = self._active.get(key)
        if cancellation is not None:
            cancellation.cancel(reason if isinstance(reason, str) else None)

    def _run_call(
        self,
        key: str,
        request_id: Any,
        name: str,
        arguments: dict[str, Any],
        cancellation: WorkerCancellation,
        predecessor: threading.Thread | None,
    ) -> None:
        try:
            if predecessor is not None:
                predecessor.join()
            try:
                result, error_payload = run_tool_supervised(
                    name, arguments, cancellation=cancellation
                )
            except Exception as error:
                result = None
                error_payload = _error_payload(
                    name,
                    code="worker_dispatch_error",
                    message=str(error),
                    error_type=type(error).__name__,
                )
            if cancellation.cancelled:
                state_change_possible = _state_change_possible(name)
                data: dict[str, Any] = {
                    "tool": name,
                    "state_change_possible": state_change_possible,
                    "outcome": (
                        "unknown"
                        if state_change_possible
                        else "not_completed"
                    ),
                }
                if cancellation.reason:
                    data["reason"] = cancellation.reason
                response = {
                    "jsonrpc": "2.0",
                    "id": request_id,
                    "error": {
                        "code": -32800,
                        "message": "Request cancelled",
                        "data": data,
                    },
                }
            else:
                payload = (
                    error_payload
                    if error_payload is not None
                    else tool_result(
                        result or {}, image_arguments=arguments
                    )
                )
                response = {
                    "jsonrpc": "2.0",
                    "id": request_id,
                    "result": payload,
                }
            self.emit(response)
        finally:
            with self._lock:
                self._active.pop(key, None)
                self._active_threads.pop(key, None)
                if self._normal_key == key:
                    self._normal_key = None
                if self._stop_key == key:
                    self._stop_key = None

    def finish(self) -> None:
        with self._lock:
            threads = list(self._threads)
        for thread in threads:
            thread.join()


def main() -> int:
    if len(sys.argv) == 2 and sys.argv[1] == "--worker":
        return _worker_main()
    def write_response(response: dict[str, Any]) -> None:
        sys.stdout.write(
            json.dumps(
                response,
                separators=(",", ":"),
                ensure_ascii=False,
            )
            + "\n"
        )
        sys.stdout.flush()

    dispatcher = McpStdioDispatcher(write_response)
    for line in sys.stdin.buffer:
        try:
            request = json.loads(line)
            if not isinstance(request, dict):
                raise ValueError("JSON-RPC message must be an object")
            dispatcher.dispatch(request)
        except (json.JSONDecodeError, ValueError) as error:
            response = {
                "jsonrpc": "2.0", "id": None,
                "error": {"code": -32700, "message": f"Parse error: {error}"},
            }
            dispatcher.emit(response)
    dispatcher.finish()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
