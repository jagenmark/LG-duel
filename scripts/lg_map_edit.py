#!/usr/bin/env python3
"""Safe, typed editing for LG Duel maps owned by the local MCP API."""

from __future__ import annotations

import base64
import copy
import difflib
import hashlib
import json
import math
import os
import re
import secrets
import shutil
import subprocess
import tempfile
from contextlib import contextmanager
from pathlib import Path
from typing import Any, BinaryIO, Callable, Iterator


MAP_NAME_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_-]{0,63}$")
OBJECT_ID_RE = re.compile(r"^[a-z][a-z0-9_-]{0,63}$")
AGENT_ID_RE = re.compile(r"^[a-z][a-z0-9_-]{0,191}$")
REVISION_RE = re.compile(r"^[0-9a-f]{64}$")
MATERIAL_RE = re.compile(r"^[A-Za-z0-9_][A-Za-z0-9_./-]{0,191}$")
ROLLBACK_RE = re.compile(r"^[0-9a-f]{32}$")
MAP_NUMBER_PATTERN = r"[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?"
MAP_POINT_RE = re.compile(
    rf"\(\s*({MAP_NUMBER_PATTERN})\s+({MAP_NUMBER_PATTERN})\s+"
    rf"({MAP_NUMBER_PATTERN})\s*\)"
)
STATE_PREFIX_V1 = "// lg-map-api-state-v1 "
STATE_PREFIX = "// lg-map-api-state-v2 "
FORMAT_VERSION = 2
MAX_COORDINATE = 40_000.0
MIN_CUBOID_SIZE = 0.01
MAX_CUBOIDS = 2048
MAX_SPAWNS = 32
MAX_POINT_LIGHTS = 96
MAX_TELEPORTS = 16
MAX_POINT_LIGHT_INTENSITY = 16.0
MAX_POINT_LIGHT_RADIUS = 4096.0
MAX_LIGHT_PRIORITY = 1000
MAX_LIGHT_SOURCE_RADIUS = 1024.0
MAX_FLICKER_FREQUENCY = 30.0
MAX_LIGHT_FACTOR = 4.0
MAX_MANAGED_MAP_BYTES = 8 * 1024 * 1024
COLLISION_MATERIALS = {"common/clip", "common/playerclip", "common/weapclip"}
DEFAULT_SUN_DIRECTION = [0.25916052, -0.43193421, -0.86386842]


class MapEditError(RuntimeError):
    pass


def _sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _canonical_json(value: Any) -> bytes:
    return json.dumps(
        value, ensure_ascii=True, sort_keys=True, separators=(",", ":")
    ).encode("ascii")


def _format_number(value: float) -> str:
    if value == 0:
        return "0"
    if float(value).is_integer():
        return str(int(value))
    return format(value, ".9g")


def _vec3(value: Any, label: str) -> list[float]:
    if not isinstance(value, list) or len(value) != 3:
        raise MapEditError(f"{label} must be an array of three numbers")
    result: list[float] = []
    for item in value:
        if isinstance(item, bool) or not isinstance(item, (int, float)):
            raise MapEditError(f"{label} must contain only numbers")
        number = float(item)
        if not math.isfinite(number) or abs(number) > MAX_COORDINATE:
            raise MapEditError(
                f"{label} values must be finite and within +/-{int(MAX_COORDINATE)}"
            )
        result.append(0.0 if number == 0 else number)
    return result


def _number(
    value: Any, label: str, *, minimum: float | None = None,
    maximum: float | None = None,
) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise MapEditError(f"{label} must be a number")
    result = float(value)
    if not math.isfinite(result):
        raise MapEditError(f"{label} must be finite")
    if minimum is not None and result < minimum:
        raise MapEditError(f"{label} must be at least {_format_number(minimum)}")
    if maximum is not None and result > maximum:
        raise MapEditError(f"{label} must be at most {_format_number(maximum)}")
    return 0.0 if result == 0 else result


def _integer(
    value: Any, label: str, *, minimum: int, maximum: int,
) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        raise MapEditError(f"{label} must be an integer")
    if value < minimum or value > maximum:
        raise MapEditError(f"{label} must be between {minimum} and {maximum}")
    return value


def _color(value: Any, label: str) -> list[float]:
    result = _vec3(value, label)
    if any(channel < 0.0 or channel > 255.0 for channel in result):
        raise MapEditError(f"{label} channels must be between 0 and 255")
    return result


def _validate_id(value: Any, label: str) -> str:
    if not isinstance(value, str) or not OBJECT_ID_RE.fullmatch(value):
        raise MapEditError(
            f"{label} must start with a lower-case letter and use only lower-case "
            "letters, numbers, _ and -"
        )
    return value


def _validate_map_name(value: Any) -> str:
    if not isinstance(value, str):
        raise MapEditError("map must be a string")
    name = value.removesuffix(".map")
    if not MAP_NAME_RE.fullmatch(name):
        raise MapEditError("map must be a safe map stem of at most 64 characters")
    return name


def _validate_revision(value: Any) -> str:
    if not isinstance(value, str) or not REVISION_RE.fullmatch(value):
        raise MapEditError("expected_revision must be a lower-case SHA-256 value")
    return value


def _material_token(value: Any) -> str:
    if not isinstance(value, str):
        raise MapEditError("material must be a string")
    material = value.replace("\\", "/").removeprefix("textures/")
    if not MATERIAL_RE.fullmatch(material) or ".." in material.split("/"):
        raise MapEditError("material must be a safe texture id")
    return material


def _bounds(minimum: Any, maximum: Any) -> tuple[list[float], list[float]]:
    low = _vec3(minimum, "min")
    high = _vec3(maximum, "max")
    for axis in range(3):
        if high[axis] - low[axis] < MIN_CUBOID_SIZE:
            raise MapEditError(
                f"cuboid max must exceed min by at least {MIN_CUBOID_SIZE} on every axis"
            )
    return low, high


def _string_vec3(value: list[float]) -> str:
    return " ".join(_format_number(item) for item in value)


def _string_color_map(value: list[float]) -> str:
    return " ".join(_format_number(channel / 255.0) for channel in value)


def _quote(value: str) -> str:
    if any(character in value for character in ('"', "\\", "\r", "\n")):
        raise MapEditError("map property text contains a forbidden character")
    return f'"{value}"'


def _cross(first: list[float], second: list[float]) -> list[float]:
    return [
        first[1] * second[2] - first[2] * second[1],
        first[2] * second[0] - first[0] * second[2],
        first[0] * second[1] - first[1] * second[0],
    ]


def _face(
    first: tuple[float, float, float],
    second: tuple[float, float, float],
    third: tuple[float, float, float],
    material: str,
    expected_normal: tuple[int, int, int],
) -> str:
    edge_a = [second[index] - first[index] for index in range(3)]
    edge_b = [third[index] - first[index] for index in range(3)]
    normal = _cross(edge_a, edge_b)
    dot = sum(normal[index] * expected_normal[index] for index in range(3))
    if not math.isfinite(dot) or dot <= 0:
        raise MapEditError("internal cuboid face winding is degenerate or inverted")

    def point_text(point: tuple[float, float, float]) -> str:
        return "( " + " ".join(_format_number(item) for item in point) + " )"

    return (
        f"{point_text(first)} {point_text(second)} {point_text(third)} "
        f"{material} 0 0 0 1 1"
    )


def _cuboid_faces(cuboid: dict[str, Any]) -> list[str]:
    low, high = _bounds(cuboid["min"], cuboid["max"])
    x0, y0, z0 = low
    x1, y1, z1 = high
    material = str(cuboid["material"])
    return [
        _face((x0, y0, z0), (x0, y0, z1), (x0, y1, z0), material, (-1, 0, 0)),
        _face((x1, y0, z0), (x1, y1, z0), (x1, y0, z1), material, (1, 0, 0)),
        _face((x0, y0, z0), (x1, y0, z0), (x0, y0, z1), material, (0, -1, 0)),
        _face((x0, y1, z0), (x0, y1, z1), (x1, y1, z0), material, (0, 1, 0)),
        _face((x0, y0, z0), (x0, y1, z0), (x1, y0, z0), material, (0, 0, -1)),
        _face((x0, y0, z1), (x1, y0, z1), (x0, y1, z1), material, (0, 0, 1)),
    ]


def _entity_lines(entity: dict[str, Any]) -> list[str]:
    properties = {
        "classname": entity["classname"],
        "lg_agent_id": entity["id"],
        **entity.get("properties", {}),
    }
    keys = ["classname", "lg_agent_id"] + sorted(
        key for key in properties if key not in {"classname", "lg_agent_id"}
    )
    lines = ["{"]
    lines.extend(f"{_quote(key)} {_quote(str(properties[key]))}" for key in keys)
    lines.append("}")
    return lines


def _light_properties(light: dict[str, Any]) -> dict[str, str]:
    flicker = light["flicker"]
    return {
        "origin": _string_vec3(light["origin"]),
        "_color": _string_color_map(light["color"]),
        "intensity": _format_number(light["intensity"]),
        "radius": _format_number(light["radius"]),
        "casts_shadows": "1" if light["casts_shadows"] else "0",
        "source_radius": _format_number(light["source_radius"]),
        "priority": str(light["priority"]),
        "flicker": "1" if flicker["enabled"] else "0",
        "flicker_seed": str(flicker["seed"]),
        "flicker_frequency": _format_number(flicker["frequency"]),
        "flicker_min": _format_number(flicker["min"]),
        "flicker_max": _format_number(flicker["max"]),
    }


def _teleport_target_name(object_id: str) -> str:
    return f"lg_agent_teleport_target_{object_id}"


def _teleport_entity_lines(teleport: dict[str, Any]) -> list[str]:
    object_id = teleport["id"]
    target_name = _teleport_target_name(object_id)
    trigger = {
        "id": f"lg-internal-teleport-trigger-{object_id}",
        "classname": "trigger_teleport",
        "properties": {"target": target_name},
    }
    target = {
        "id": f"lg-internal-teleport-target-{object_id}",
        "classname": "target_position",
        "properties": {
            "targetname": target_name,
            "origin": _string_vec3(teleport["destination"]),
            "angle": _format_number(teleport["exit_yaw"]),
        },
    }
    brush = {
        "min": teleport["min"],
        "max": teleport["max"],
        "material": "common/trigger",
    }
    lines = _entity_lines(trigger)
    lines.insert(-1, "{")
    lines[-1:-1] = [*_cuboid_faces(brush), "}"]
    return [*lines, *_entity_lines(target)]


def _render_version(state: dict[str, Any], *, version: int) -> bytes:
    if version == FORMAT_VERSION:
        _validate_state(state)
        prefix = STATE_PREFIX
    else:
        _validate_v1_state(state)
        prefix = STATE_PREFIX_V1
    encoded = base64.urlsafe_b64encode(_canonical_json(state)).decode("ascii")
    lines = [
        f"{prefix}{encoded}",
        "// Game: LG Duel",
        "// Format: Standard",
    ]
    render_entities = copy.deepcopy(state["entities"])
    if version == FORMAT_VERSION:
        world = next(
            entity for entity in render_entities
            if entity["classname"] == "worldspawn"
        )
        lighting = state["world_lighting"]
        world["properties"]["lg_ambient_intensity"] = _format_number(
            lighting["ambient_intensity"]
        )
        world["properties"]["lg_ambient_color"] = _string_color_map(
            lighting["ambient_color"]
        )
        render_entities.extend(
            {
                "id": light["id"],
                "classname": "light_point",
                "properties": _light_properties(light),
            }
            for light in state["point_lights"]
        )
        if lighting["sun"] is not None:
            sun = lighting["sun"]
            render_entities.append({
                "id": sun["id"],
                "classname": "light_sun",
                "properties": {
                    "direction": _string_vec3(sun["direction"]),
                    "color": _string_color_map(sun["color"]),
                    "intensity": _format_number(sun["intensity"]),
                },
            })
    entities = sorted(
        render_entities,
        key=lambda entity: (entity["classname"] != "worldspawn", entity["id"]),
    )
    for entity in entities:
        lines.extend(_entity_lines(entity))
    for cuboid in sorted(state["cuboids"], key=lambda item: item["id"]):
        lines.extend(
            [
                "{",
                '"classname" "func_group"',
                f'"lg_agent_id" "{cuboid["id"]}"',
                "{",
                *_cuboid_faces(cuboid),
                "}",
                "}",
            ]
        )
    if version == FORMAT_VERSION:
        for teleport in sorted(state["teleports"], key=lambda item: item["id"]):
            lines.extend(_teleport_entity_lines(teleport))
    return ("\n".join(lines) + "\n").encode("utf-8")


def _render(state: dict[str, Any]) -> bytes:
    return _render_version(state, version=FORMAT_VERSION)


def _initial_state() -> dict[str, Any]:
    return {
        "format": FORMAT_VERSION,
        "template": "initial",
        "world_lighting": {
            "ambient_color": [255.0, 255.0, 255.0],
            "ambient_intensity": 0.3,
            "sun": None,
        },
        "entities": [
            {
                "id": "worldspawn",
                "classname": "worldspawn",
                "properties": {
                    "lg_bounds_max": "512 512 256",
                    "lg_bounds_min": "-512 -512 -128",
                    "lg_map_api_version": "2",
                },
            },
            {
                "id": "spawn-a",
                "classname": "lg_spawn",
                "properties": {"angle": "0", "origin": "-128 0 0"},
            },
            {
                "id": "spawn-b",
                "classname": "lg_spawn",
                "properties": {"angle": "180", "origin": "128 0 0"},
            },
        ],
        "cuboids": [],
        "point_lights": [],
        "teleports": [],
    }


def _validate_properties(entity: dict[str, Any], *, version: int = FORMAT_VERSION) -> None:
    classname = entity["classname"]
    properties = entity.get("properties")
    if not isinstance(properties, dict):
        raise MapEditError("managed entity properties must be an object")
    allowed = {
        "worldspawn": {"lg_bounds_min", "lg_bounds_max", "lg_map_api_version"},
        "lg_spawn": {"origin", "angle", "yaw"},
    }
    if classname not in allowed:
        raise MapEditError(f"entity class '{classname}' is not allowed in managed maps")
    if not set(properties).issubset(allowed[classname]):
        raise MapEditError(f"entity '{entity['id']}' has unsupported properties")
    if classname == "worldspawn":
        expected_version = str(version)
        if properties.get("lg_map_api_version") != expected_version:
            raise MapEditError(
                f"worldspawn must keep lg_map_api_version={expected_version}"
            )
        for key in ("lg_bounds_min", "lg_bounds_max"):
            parts = str(properties.get(key, "")).split()
            if len(parts) != 3:
                raise MapEditError(f"worldspawn {key} must contain three numbers")
            try:
                values = [float(part) for part in parts]
            except ValueError as error:
                raise MapEditError(f"worldspawn {key} must contain three numbers") from error
            _vec3(values, key)
    else:
        parts = str(properties.get("origin", "")).split()
        if len(parts) != 3:
            raise MapEditError(f"spawn '{entity['id']}' must have an origin")
        try:
            values = [float(part) for part in parts]
        except ValueError as error:
            raise MapEditError(f"spawn '{entity['id']}' must have a numeric origin") from error
        _vec3(values, "origin")
        if "angle" in properties and "yaw" in properties:
            raise MapEditError("spawn may define angle or yaw, not both")
        for key in ("angle", "yaw"):
            if key in properties:
                try:
                    value = float(properties[key])
                except (TypeError, ValueError) as error:
                    raise MapEditError(f"spawn {key} must be a number") from error
                if not math.isfinite(value) or abs(value) > MAX_COORDINATE:
                    raise MapEditError(f"spawn {key} is outside the allowed numeric range")


def _validate_common_state(
    state: Any, *, version: int, expected_keys: set[str],
) -> tuple[set[str], list[float], list[float]]:
    if not isinstance(state, dict) or set(state) != expected_keys:
        raise MapEditError("managed map state has an invalid shape")
    if state["format"] != version or state["template"] != "initial":
        raise MapEditError("managed map format or template is not supported")
    if not isinstance(state["entities"], list) or not isinstance(state["cuboids"], list):
        raise MapEditError("managed map entities and cuboids must be arrays")
    if len(state["cuboids"]) > MAX_CUBOIDS:
        raise MapEditError(f"managed maps may contain at most {MAX_CUBOIDS} cuboids")
    ids: set[str] = set()
    worldspawns = 0
    spawns = 0
    world: dict[str, Any] | None = None
    for entity in state["entities"]:
        if not isinstance(entity, dict) or set(entity) != {
            "id", "classname", "properties"
        }:
            raise MapEditError("managed entity has an invalid shape")
        object_id = _validate_id(entity["id"], "entity id")
        if object_id in ids:
            raise MapEditError(f"duplicate managed object id '{object_id}'")
        ids.add(object_id)
        _validate_properties(entity, version=version)
        if entity["classname"] == "worldspawn":
            worldspawns += 1
            world = entity
        elif entity["classname"] == "lg_spawn":
            spawns += 1
    if worldspawns != 1:
        raise MapEditError("managed maps require exactly one worldspawn")
    if spawns > MAX_SPAWNS:
        raise MapEditError(f"managed maps may contain at most {MAX_SPAWNS} spawns")
    assert world is not None
    world_min = [float(item) for item in world["properties"]["lg_bounds_min"].split()]
    world_max = [float(item) for item in world["properties"]["lg_bounds_max"].split()]
    if any(world_min[axis] >= world_max[axis] for axis in range(3)):
        raise MapEditError("worldspawn bounds_min must be less than bounds_max")
    for entity in state["entities"]:
        if entity["classname"] != "lg_spawn":
            continue
        origin = [float(item) for item in entity["properties"]["origin"].split()]
        if any(
            origin[axis] < world_min[axis] or origin[axis] > world_max[axis]
            for axis in range(3)
        ):
            raise MapEditError(f"spawn '{entity['id']}' is outside worldspawn bounds")
    for cuboid in state["cuboids"]:
        if not isinstance(cuboid, dict) or set(cuboid) != {
            "id", "min", "max", "material"
        }:
            raise MapEditError("managed cuboid has an invalid shape")
        object_id = _validate_id(cuboid["id"], "cuboid id")
        if object_id in ids:
            raise MapEditError(f"duplicate managed object id '{object_id}'")
        ids.add(object_id)
        _bounds(cuboid["min"], cuboid["max"])
        if _material_token(cuboid["material"]) != cuboid["material"]:
            raise MapEditError("managed cuboid material is not canonical")
        low, high = _bounds(cuboid["min"], cuboid["max"])
        if any(
            low[axis] < world_min[axis] or high[axis] > world_max[axis]
            for axis in range(3)
        ):
            raise MapEditError(f"cuboid '{object_id}' is outside worldspawn bounds")
        _cuboid_faces(cuboid)
    return ids, world_min, world_max


def _validate_v1_state(state: Any) -> None:
    _validate_common_state(
        state,
        version=1,
        expected_keys={"format", "template", "entities", "cuboids"},
    )


def _validate_flicker(value: Any) -> None:
    if not isinstance(value, dict) or set(value) != {
        "enabled", "seed", "frequency", "min", "max"
    }:
        raise MapEditError("point light flicker has an invalid shape")
    if not isinstance(value["enabled"], bool):
        raise MapEditError("flicker enabled must be a boolean")
    _integer(value["seed"], "flicker seed", minimum=0, maximum=0xFFFFFFFF)
    minimum = _number(
        value["min"], "flicker min", minimum=0.0, maximum=MAX_LIGHT_FACTOR
    )
    maximum = _number(
        value["max"], "flicker max", minimum=0.0, maximum=MAX_LIGHT_FACTOR
    )
    if minimum > maximum:
        raise MapEditError("flicker min must not exceed flicker max")
    frequency = _number(
        value["frequency"], "flicker frequency",
        minimum=0.0, maximum=MAX_FLICKER_FREQUENCY,
    )
    if value["enabled"] and frequency < 0.1:
        raise MapEditError("enabled flicker frequency must be between 0.1 and 30")
    if not value["enabled"] and frequency != 0.0:
        raise MapEditError("disabled flicker frequency must be zero")


def _validate_sun(value: Any) -> None:
    if value is None:
        return
    if not isinstance(value, dict) or set(value) != {
        "id", "direction", "color", "intensity"
    }:
        raise MapEditError("sun light has an invalid shape")
    _validate_id(value["id"], "sun id")
    direction = _vec3(value["direction"], "sun direction")
    if math.sqrt(sum(channel * channel for channel in direction)) <= 0.0001:
        raise MapEditError("sun direction must be non-zero")
    _color(value["color"], "sun color")
    _number(value["intensity"], "sun intensity", minimum=0.0)


def _validate_state(state: Any) -> None:
    ids, world_min, world_max = _validate_common_state(
        state,
        version=FORMAT_VERSION,
        expected_keys={
            "format", "template", "world_lighting",
            "entities", "cuboids", "point_lights", "teleports",
        },
    )
    lighting = state["world_lighting"]
    if not isinstance(lighting, dict) or set(lighting) != {
        "ambient_color", "ambient_intensity", "sun"
    }:
        raise MapEditError("world lighting has an invalid shape")
    _color(lighting["ambient_color"], "ambient color")
    _number(lighting["ambient_intensity"], "ambient intensity", minimum=0.0)
    _validate_sun(lighting["sun"])
    if lighting["sun"] is not None:
        sun_id = lighting["sun"]["id"]
        if sun_id in ids:
            raise MapEditError(f"duplicate managed object id '{sun_id}'")
        ids.add(sun_id)
    if not isinstance(state["point_lights"], list):
        raise MapEditError("managed point lights must be an array")
    if len(state["point_lights"]) > MAX_POINT_LIGHTS:
        raise MapEditError(
            f"managed maps may contain at most {MAX_POINT_LIGHTS} point lights"
        )
    for light in state["point_lights"]:
        if not isinstance(light, dict) or set(light) != {
            "id", "origin", "color", "intensity", "radius",
            "casts_shadows", "source_radius", "priority", "flicker",
        }:
            raise MapEditError("managed point light has an invalid shape")
        light_id = _validate_id(light["id"], "point light id")
        if light_id in ids:
            raise MapEditError(f"duplicate managed object id '{light_id}'")
        ids.add(light_id)
        origin = _vec3(light["origin"], "point light origin")
        if any(
            origin[axis] < world_min[axis] or origin[axis] > world_max[axis]
            for axis in range(3)
        ):
            raise MapEditError(f"point light '{light_id}' is outside worldspawn bounds")
        _color(light["color"], "point light color")
        _number(
            light["intensity"], "point light intensity",
            minimum=0.000000001, maximum=MAX_POINT_LIGHT_INTENSITY,
        )
        radius = _number(
            light["radius"], "point light radius",
            minimum=0.000000001, maximum=MAX_POINT_LIGHT_RADIUS,
        )
        if not isinstance(light["casts_shadows"], bool):
            raise MapEditError("casts_shadows must be a boolean")
        source_radius = _number(
            light["source_radius"], "source radius",
            minimum=0.0, maximum=MAX_LIGHT_SOURCE_RADIUS,
        )
        if source_radius > radius:
            raise MapEditError("source radius must not exceed point light radius")
        _integer(
            light["priority"], "point light priority",
            minimum=-MAX_LIGHT_PRIORITY, maximum=MAX_LIGHT_PRIORITY,
        )
        _validate_flicker(light["flicker"])
    if not isinstance(state["teleports"], list):
        raise MapEditError("managed teleports must be an array")
    if len(state["teleports"]) > MAX_TELEPORTS:
        raise MapEditError(
            f"managed maps may contain at most {MAX_TELEPORTS} teleports"
        )
    for teleport in state["teleports"]:
        if not isinstance(teleport, dict) or set(teleport) != {
            "id", "min", "max", "destination", "exit_yaw"
        }:
            raise MapEditError("managed teleport has an invalid shape")
        object_id = _validate_id(teleport["id"], "teleport id")
        if object_id in ids:
            raise MapEditError(f"duplicate managed object id '{object_id}'")
        ids.add(object_id)
        for internal_id in (
            f"lg-internal-teleport-trigger-{object_id}",
            f"lg-internal-teleport-target-{object_id}",
        ):
            if internal_id in ids:
                raise MapEditError(
                    f"teleport '{object_id}' generated duplicate agent id "
                    f"'{internal_id}'"
                )
            ids.add(internal_id)
        low, high = _bounds(teleport["min"], teleport["max"])
        if any(
            low[axis] < world_min[axis] or high[axis] > world_max[axis]
            for axis in range(3)
        ):
            raise MapEditError(f"teleport '{object_id}' is outside worldspawn bounds")
        destination = _vec3(teleport["destination"], "teleport destination")
        if any(
            destination[axis] < world_min[axis]
            or destination[axis] > world_max[axis]
            for axis in range(3)
        ):
            raise MapEditError(
                f"teleport '{object_id}' destination is outside worldspawn bounds"
            )
        _number(
            teleport["exit_yaw"], "teleport exit_yaw",
            minimum=-MAX_COORDINATE, maximum=MAX_COORDINATE,
        )


def _decode_state(data: bytes) -> dict[str, Any]:
    if len(data) > MAX_MANAGED_MAP_BYTES:
        raise MapEditError("managed map exceeds the 8 MiB API limit")
    try:
        first_line = data.decode("utf-8").splitlines()[0]
    except (UnicodeDecodeError, IndexError) as error:
        raise MapEditError("map is not a UTF-8 MCP-managed map") from error
    if first_line.startswith(STATE_PREFIX):
        prefix = STATE_PREFIX
        version = FORMAT_VERSION
    elif first_line.startswith(STATE_PREFIX_V1):
        prefix = STATE_PREFIX_V1
        version = 1
    else:
        raise MapEditError("map is not managed by the MCP map API")
    try:
        raw = base64.urlsafe_b64decode(first_line[len(prefix):].encode("ascii"))
        state = json.loads(raw)
    except (ValueError, UnicodeError, json.JSONDecodeError) as error:
        raise MapEditError("managed map state marker is invalid") from error
    if version == FORMAT_VERSION:
        _validate_state(state)
    else:
        _validate_v1_state(state)
    if _render_version(state, version=version) != data:
        raise MapEditError(
            "managed map text changed outside the API; use TrenchBroom or recreate the managed map"
        )
    if version == 1:
        state = copy.deepcopy(state)
        state["format"] = FORMAT_VERSION
        state["world_lighting"] = {
            "ambient_color": [255.0, 255.0, 255.0],
            "ambient_intensity": 0.3,
            "sun": None,
        }
        state["point_lights"] = []
        state["teleports"] = []
        world = next(
            entity for entity in state["entities"]
            if entity["classname"] == "worldspawn"
        )
        world["properties"]["lg_map_api_version"] = str(FORMAT_VERSION)
        _validate_state(state)
    return state


def _default_flicker() -> dict[str, Any]:
    return {
        "enabled": False,
        "seed": 0,
        "frequency": 0.0,
        "min": 1.0,
        "max": 1.0,
    }


def _make_point_light(
    object_id: Any, origin: Any, color: Any, intensity: Any, radius: Any,
    *, casts_shadows: Any = False, source_radius: Any = 0.0,
    priority: Any = 0, flicker_enabled: Any = False,
    flicker_seed: Any = 0, flicker_frequency: Any = None,
    flicker_min: Any = 1.0, flicker_max: Any = 1.0,
) -> dict[str, Any]:
    if not isinstance(casts_shadows, bool):
        raise MapEditError("casts_shadows must be a boolean")
    if not isinstance(flicker_enabled, bool):
        raise MapEditError("flicker_enabled must be a boolean")
    if flicker_frequency is None:
        flicker_frequency = 8.0 if flicker_enabled else 0.0
    light = {
        "id": _validate_id(object_id, "point light id"),
        "origin": _vec3(origin, "point light origin"),
        "color": _color(color, "point light color"),
        "intensity": _number(
            intensity, "point light intensity",
            minimum=0.000000001, maximum=MAX_POINT_LIGHT_INTENSITY,
        ),
        "radius": _number(
            radius, "point light radius",
            minimum=0.000000001, maximum=MAX_POINT_LIGHT_RADIUS,
        ),
        "casts_shadows": casts_shadows,
        "source_radius": _number(
            source_radius, "source radius",
            minimum=0.0, maximum=MAX_LIGHT_SOURCE_RADIUS,
        ),
        "priority": _integer(
            priority, "point light priority",
            minimum=-MAX_LIGHT_PRIORITY, maximum=MAX_LIGHT_PRIORITY,
        ),
        "flicker": {
            "enabled": flicker_enabled,
            "seed": _integer(
                flicker_seed, "flicker seed", minimum=0, maximum=0xFFFFFFFF
            ),
            "frequency": _number(
                flicker_frequency, "flicker frequency",
                minimum=0.0, maximum=MAX_FLICKER_FREQUENCY,
            ),
            "min": _number(
                flicker_min, "flicker min",
                minimum=0.0, maximum=MAX_LIGHT_FACTOR,
            ),
            "max": _number(
                flicker_max, "flicker max",
                minimum=0.0, maximum=MAX_LIGHT_FACTOR,
            ),
        },
    }
    if light["source_radius"] > light["radius"]:
        raise MapEditError("source radius must not exceed point light radius")
    _validate_flicker(light["flicker"])
    return light


def _scan_quoted_token(
    text: str, start: int, end: int
) -> tuple[str, int, int, int]:
    if start >= end or text[start] != '"':
        raise MapEditError("map property token must start with a quote")
    index = start + 1
    value_start = index
    escaped = False
    while index < end:
        character = text[index]
        if escaped:
            escaped = False
        elif character == "\\":
            escaped = True
        elif character == '"':
            return text[value_start:index], value_start, index, index + 1
        index += 1
    raise MapEditError("map has an unterminated quoted property token")


def _skip_map_space_and_comments(text: str, start: int, end: int) -> int:
    index = start
    while index < end:
        if text[index].isspace():
            index += 1
            continue
        if text.startswith("//", index):
            newline = text.find("\n", index + 2, end)
            if newline < 0:
                return end
            index = newline + 1
            continue
        break
    return index


def _parse_raw_entity_layout(
    text: str, entity_start: int, entity_end: int
) -> tuple[dict[str, dict[str, Any]], list[tuple[int, int]]]:
    properties: dict[str, dict[str, Any]] = {}
    brushes: list[tuple[int, int]] = []
    brush_start: int | None = None
    depth = 0
    index = entity_start
    while index < entity_end:
        index = _skip_map_space_and_comments(text, index, entity_end)
        if index >= entity_end:
            break
        character = text[index]
        if character == "{":
            if depth == 1:
                brush_start = index
            depth += 1
            index += 1
            continue
        if character == "}":
            if depth == 2 and brush_start is not None:
                brushes.append((brush_start, index + 1))
                brush_start = None
            depth -= 1
            if depth < 0:
                raise MapEditError("map entity has an unmatched closing brace")
            index += 1
            continue
        if character != '"':
            index += 1
            continue
        token, _token_start, _token_end, after_token = _scan_quoted_token(
            text, index, entity_end
        )
        if depth != 1:
            index = after_token
            continue
        value_quote = _skip_map_space_and_comments(
            text, after_token, entity_end
        )
        if value_quote >= entity_end or text[value_quote] != '"':
            raise MapEditError(f"map property '{token}' is missing a quoted value")
        value, value_start, value_end, after_value = _scan_quoted_token(
            text, value_quote, entity_end
        )
        line_start = text.rfind("\n", entity_start, index) + 1
        newline = text.find("\n", after_value, entity_end)
        line_end = entity_end if newline < 0 else newline + 1
        properties.setdefault(token, {
            "value": value,
            "value_start": value_start,
            "value_end": value_end,
            "line_start": line_start,
            "line_end": line_end,
        })
        index = after_value
    if depth != 0:
        raise MapEditError("map entity braces are not balanced")
    return properties, brushes


def _parse_raw_map(data: bytes) -> dict[str, Any]:
    if len(data) > MAX_MANAGED_MAP_BYTES:
        raise MapEditError("map exceeds the 8 MiB API limit")
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError as error:
        raise MapEditError("map must be UTF-8 for typed non-lossy edits") from error
    entities: list[dict[str, Any]] = []
    depth = 0
    entity_start: int | None = None
    quoted = False
    escaped = False
    index = 0
    while index < len(text):
        character = text[index]
        if escaped:
            escaped = False
        elif quoted and character == "\\":
            escaped = True
        elif character == '"':
            quoted = not quoted
        elif not quoted and character == "/" and index + 1 < len(text) and text[index + 1] == "/":
            newline = text.find("\n", index + 2)
            index = len(text) if newline < 0 else newline
            continue
        elif not quoted and character == "{":
            if depth == 0:
                entity_start = index
            depth += 1
        elif not quoted and character == "}":
            depth -= 1
            if depth < 0:
                raise MapEditError("map has an unmatched closing brace")
            if depth == 0:
                assert entity_start is not None
                entity_end = index + 1
                properties, brushes = _parse_raw_entity_layout(
                    text, entity_start, entity_end
                )
                entities.append({
                    "start": entity_start,
                    "end": entity_end,
                    "properties": properties,
                    "classname": properties.get("classname", {}).get("value"),
                    "brush_count": len(brushes),
                    "brushes": brushes,
                })
                entity_start = None
        index += 1
    if quoted:
        raise MapEditError("map has an unterminated quoted string")
    if depth != 0:
        raise MapEditError("map braces are not balanced")
    worlds = [entity for entity in entities if entity["classname"] == "worldspawn"]
    if len(worlds) != 1:
        raise MapEditError("maps require exactly one worldspawn")
    agent_ids: set[str] = set()
    for entity in entities:
        item = entity["properties"].get("lg_agent_id")
        if item is None:
            continue
        object_id = item["value"]
        if not AGENT_ID_RE.fullmatch(object_id):
            raise MapEditError("lg_agent_id must use safe lower-case id text")
        if object_id in agent_ids:
            raise MapEditError(f"duplicate lg_agent_id '{object_id}'")
        agent_ids.add(object_id)
    if sum(entity["classname"] in {"light", "light_point"} for entity in entities) > MAX_POINT_LIGHTS:
        raise MapEditError(f"maps may contain at most {MAX_POINT_LIGHTS} point lights")
    if sum(entity["classname"] == "light_sun" for entity in entities) > 1:
        raise MapEditError("maps may contain at most one light_sun")
    teleport_entities = [
        entity for entity in entities
        if entity["classname"] == "trigger_teleport"
    ]
    if any(
        entity["brush_count"] == 0
        and not (
            _raw_property(entity, "lg_agent_id") or ""
        ).startswith("lg-internal-teleport-trigger-")
        for entity in teleport_entities
    ):
        raise MapEditError("trigger_teleport requires at least one top-level brush")
    teleport_volume_count = sum(
        entity["brush_count"] for entity in teleport_entities
    )
    if teleport_volume_count > MAX_TELEPORTS:
        raise MapEditError(f"maps may contain at most {MAX_TELEPORTS} teleports")
    return {"text": text, "entities": entities, "world": worlds[0], "agent_ids": agent_ids}


def _raw_property(entity: dict[str, Any], key: str) -> str | None:
    item = entity["properties"].get(key)
    return str(item["value"]) if item is not None else None


def _raw_vec(value: str | None, label: str) -> list[float]:
    parts = value.split() if value is not None else []
    if len(parts) != 3:
        raise MapEditError(f"{label} must contain three numbers")
    try:
        return _vec3([float(part) for part in parts], label)
    except ValueError as error:
        raise MapEditError(f"{label} must contain three numbers") from error


def _raw_color(value: str | None, label: str) -> list[float]:
    color = _raw_vec(value, label)
    if all(channel <= 1.0 for channel in color):
        color = [channel * 255.0 for channel in color]
        color = [
            float(round(channel))
            if abs(channel - round(channel)) <= 0.000001
            else channel
            for channel in color
        ]
    return color


def _normalized_direction(value: list[float], label: str) -> list[float]:
    length = math.sqrt(sum(channel * channel for channel in value))
    if not math.isfinite(length) or length <= 0.0001:
        raise MapEditError(f"{label} must be non-zero")
    return [channel / length for channel in value]


def _sun_direction_from_raw(entity: dict[str, Any] | None) -> list[float]:
    if entity is None:
        return list(DEFAULT_SUN_DIRECTION)
    direction = _raw_property(entity, "direction")
    if direction is not None:
        parsed = _raw_vec(direction, "sun direction")
        _normalized_direction(parsed, "sun direction")
        return parsed
    angle_text = _raw_property(entity, "angle")
    pitch_text = _raw_property(entity, "pitch")
    if angle_text is None and pitch_text is None:
        return list(DEFAULT_SUN_DIRECTION)
    angle = _raw_float(angle_text, "sun angle", 0.0)
    pitch = _raw_float(pitch_text, "sun pitch", -45.0)
    yaw_radians = math.radians(angle)
    pitch_radians = math.radians(pitch)
    pitch_cos = math.cos(pitch_radians)
    return _normalized_direction([
        math.cos(yaw_radians) * pitch_cos,
        math.sin(yaw_radians) * pitch_cos,
        math.sin(pitch_radians),
    ], "sun direction")


def _raw_float(value: str | None, label: str, default: float) -> float:
    if value is None:
        return default
    try:
        return _number(float(value), label)
    except ValueError as error:
        raise MapEditError(f"{label} must be a number") from error


def _raw_bool(value: str | None, label: str, default: bool = False) -> bool:
    if value is None:
        return default
    normalized = value.strip().lower()
    if normalized in {"1", "true", "yes", "on"}:
        return True
    if normalized in {"0", "false", "no", "off"}:
        return False
    raise MapEditError(f"{label} must be a boolean")


def _raw_world_bounds(raw: dict[str, Any]) -> tuple[list[float], list[float]]:
    world = raw["world"]
    minimum = _raw_property(world, "lg_bounds_min")
    maximum = _raw_property(world, "lg_bounds_max")
    if minimum is not None and maximum is not None:
        low = _raw_vec(minimum, "lg_bounds_min")
        high = _raw_vec(maximum, "lg_bounds_max")
        if any(low[axis] >= high[axis] for axis in range(3)):
            raise MapEditError("worldspawn bounds_min must be less than bounds_max")
        return low, high
    points = MAP_POINT_RE.findall(raw["text"])
    if not points:
        raise MapEditError(
            "hand-authored maps need world bounds or brush points for typed edits"
        )
    values = [[float(channel) for channel in point] for point in points]
    return (
        [min(point[axis] for point in values) for axis in range(3)],
        [max(point[axis] for point in values) for axis in range(3)],
    )


def _raw_brush_bounds(
    raw: dict[str, Any], entity: dict[str, Any]
) -> tuple[list[float], list[float]]:
    if len(entity["brushes"]) != 1:
        raise MapEditError("owned teleport trigger must contain exactly one brush")
    start, end = entity["brushes"][0]
    points = MAP_POINT_RE.findall(raw["text"][start:end])
    if not points:
        raise MapEditError("owned teleport trigger brush has no face points")
    values = [[float(channel) for channel in point] for point in points]
    return (
        [min(point[axis] for point in values) for axis in range(3)],
        [max(point[axis] for point in values) for axis in range(3)],
    )


def _vectors_match(
    left: list[float], right: list[float], tolerance: float = 0.00001
) -> bool:
    return all(abs(left[index] - right[index]) <= tolerance for index in range(3))


def _raw_materials(raw: dict[str, Any]) -> list[str]:
    return sorted(set(re.findall(
        r"(?m)^[ \t]*(?:\([^)\r\n]*\)[ \t]*){3}([^ \t\r\n]+)",
        raw["text"],
    )))


def _replace_text_spans(
    text: str, replacements: list[tuple[int, int, str]]
) -> str:
    for start, end, replacement in sorted(replacements, reverse=True):
        if start < 0 or end < start or end > len(text):
            raise MapEditError("internal map patch span is invalid")
        text = text[:start] + replacement + text[end:]
    return text


def _preferred_newline(text: str) -> str:
    crlf_count = text.count("\r\n")
    lone_lf_count = text.count("\n") - crlf_count
    return "\r\n" if crlf_count > lone_lf_count else "\n"


def _match_newlines(rendered: str, template: str) -> str:
    newline = _preferred_newline(template)
    return rendered.replace("\r\n", "\n").replace("\n", newline)


def _append_raw_entities(text: str, rendered: list[str]) -> str:
    if not rendered:
        return text
    newline = _preferred_newline(text)
    separator = "" if text.endswith(("\n", "\r")) else newline
    body = newline.join(_match_newlines(item, text) for item in rendered)
    return text + separator + body + newline


def _render_raw_entity(
    classname: str, object_id: str, properties: dict[str, str]
) -> str:
    return "\n".join(_entity_lines({
        "id": object_id,
        "classname": classname,
        "properties": properties,
    }))


def _raw_entity_by_agent_id(
    raw: dict[str, Any], object_id: str, classes: set[str]
) -> dict[str, Any] | None:
    return next(
        (
            entity for entity in raw["entities"]
            if entity["classname"] in classes
            and _raw_property(entity, "lg_agent_id") == object_id
        ),
        None,
    )


def _point_light_from_raw(entity: dict[str, Any], object_id: str) -> dict[str, Any]:
    color_text = _raw_property(entity, "_color")
    if color_text is None:
        color_text = _raw_property(entity, "color")
    color = (
        _raw_color(color_text, "point light color")
        if color_text is not None else [255.0, 255.0, 255.0]
    )
    intensity = 1.0
    quake_light = _raw_property(entity, "_light")
    if quake_light is not None:
        try:
            values = [float(part) for part in quake_light.split()]
        except ValueError as error:
            raise MapEditError(
                "point light _light must contain finite numbers"
            ) from error
        if not all(math.isfinite(value) for value in values):
            raise MapEditError("point light _light must contain finite numbers")
        if len(values) == 1 and values[0] > 0.0:
            intensity = values[0] / 300.0
        elif (
            len(values) == 4
            and all(0.0 <= value <= 1.0 for value in values[:3])
            and values[3] > 0.0
        ):
            color = [value * 255.0 for value in values[:3]]
            intensity = values[3] / 300.0
        else:
            raise MapEditError(
                "point light _light must be intensity or 'r g b intensity'"
            )
    light_text = _raw_property(entity, "light")
    if light_text is not None:
        intensity = _raw_float(light_text, "point light light", 300.0) / 300.0
    intensity_text = _raw_property(entity, "intensity")
    if intensity_text is not None:
        intensity = _raw_float(intensity_text, "point light intensity", 1.0)
    flicker_enabled = _raw_bool(
        _raw_property(entity, "flicker"), "point light flicker"
    )
    frequency = _raw_float(
        _raw_property(entity, "flicker_frequency"),
        "point light flicker_frequency",
        8.0 if flicker_enabled else 0.0,
    )
    try:
        seed = int(_raw_property(entity, "flicker_seed") or "0")
        priority = int(_raw_property(entity, "priority") or "0")
    except ValueError as error:
        raise MapEditError("point light seed and priority must be integers") from error
    return _make_point_light(
        object_id,
        _raw_vec(_raw_property(entity, "origin"), "point light origin"),
        color,
        intensity,
        _raw_float(_raw_property(entity, "radius"), "point light radius", 320.0),
        casts_shadows=_raw_bool(
            _raw_property(entity, "casts_shadows"), "casts_shadows"
        ),
        source_radius=_raw_float(
            _raw_property(entity, "source_radius"), "source_radius", 0.0
        ),
        priority=priority,
        flicker_enabled=flicker_enabled,
        flicker_seed=seed,
        flicker_frequency=frequency,
        flicker_min=_raw_float(
            _raw_property(entity, "flicker_min"), "flicker_min", 1.0
        ),
        flicker_max=_raw_float(
            _raw_property(entity, "flicker_max"), "flicker_max", 1.0
        ),
    )


class MapEditor:
    """Owns safe paths, revisions, transactions, and canonical map writes."""

    def __init__(self, repo_root: Path | None = None):
        self.repo_root = (
            Path(repo_root) if repo_root is not None else Path(__file__).resolve().parents[1]
        ).resolve()
        self.maps_root = (self.repo_root / "maps").resolve()
        self.runtime_maps_root = (self.repo_root / "build" / "default" / "maps").resolve()
        self.transaction_root = self.maps_root / ".lg-map-api" / "transactions"
        if not self.maps_root.is_dir():
            raise MapEditError(f"maps directory does not exist: {self.maps_root}")

    @contextmanager
    def _map_lock(self, name: str) -> Iterator[None]:
        with self._named_lock(f"map-{name}"):
            yield

    @contextmanager
    def _runtime_lock(self) -> Iterator[None]:
        with self._named_lock("runtime-load"):
            yield

    @contextmanager
    def _named_lock(self, name: str) -> Iterator[None]:
        lock_root = self.maps_root / ".lg-map-api" / "locks"
        lock_root.mkdir(parents=True, exist_ok=True)
        path = lock_root / f"{name}.lock"
        with path.open("a+b") as handle:
            handle.seek(0, os.SEEK_END)
            if handle.tell() == 0:
                handle.write(b"\0")
                handle.flush()
            handle.seek(0)
            self._lock_file(handle)
            try:
                yield
            finally:
                self._unlock_file(handle)

    @staticmethod
    def _lock_file(handle: BinaryIO) -> None:
        if os.name == "nt":
            import msvcrt
            msvcrt.locking(handle.fileno(), msvcrt.LK_LOCK, 1)
        else:
            import fcntl
            fcntl.flock(handle.fileno(), fcntl.LOCK_EX)

    @staticmethod
    def _unlock_file(handle: BinaryIO) -> None:
        handle.seek(0)
        if os.name == "nt":
            import msvcrt
            msvcrt.locking(handle.fileno(), msvcrt.LK_UNLCK, 1)
        else:
            import fcntl
            fcntl.flock(handle.fileno(), fcntl.LOCK_UN)

    def _map_path(self, map_name: Any) -> tuple[str, Path]:
        name = _validate_map_name(map_name)
        path = (self.maps_root / f"{name}.map").resolve()
        if path.parent != self.maps_root:
            raise MapEditError("map path escaped the maps directory")
        return name, path

    def _read(self, map_name: Any) -> tuple[str, Path, bytes, dict[str, Any]]:
        name, path = self._map_path(map_name)
        try:
            data = path.read_bytes()
        except FileNotFoundError as error:
            raise MapEditError(f"map '{name}' does not exist") from error
        state = self._validate_managed_data(data)
        return name, path, data, state

    def _material(self, value: Any) -> str:
        material = _material_token(value)
        normalized = material.lower()
        if normalized in COLLISION_MATERIALS:
            return normalized
        texture = self._material_path(material)
        textures_root = (self.repo_root / "textures").resolve()
        relative = texture.relative_to(textures_root).as_posix()
        return relative[:-4] if relative.lower().endswith(".png") else relative

    def _material_path(self, material: str) -> Path:
        textures_root = (self.repo_root / "textures").resolve()
        texture = (textures_root / material).resolve()
        if texture.suffix.lower() not in {".png", ".bmp", ".jpg"}:
            texture = Path(f"{texture}.png")
        if texture == textures_root or textures_root not in texture.parents:
            raise MapEditError("material path escaped the textures directory")
        if not texture.is_file():
            raise MapEditError(f"material texture does not exist: {material}")
        return texture

    def _validate_managed_data(self, data: bytes) -> dict[str, Any]:
        state = _decode_state(data)
        for cuboid in state["cuboids"]:
            if self._material(cuboid["material"]) != cuboid["material"]:
                raise MapEditError(
                    f"managed cuboid '{cuboid['id']}' material is not canonical"
                )
        return state

    def _validate_editable_data(self, data: bytes) -> dict[str, Any]:
        if data.startswith((STATE_PREFIX.encode("ascii"), STATE_PREFIX_V1.encode("ascii"))):
            return {"mode": "managed", "state": self._validate_managed_data(data)}
        raw = _parse_raw_map(data)
        for entity in raw["entities"]:
            internal_id = _raw_property(entity, "lg_agent_id") or ""
            prefix = "lg-internal-teleport-trigger-"
            if (
                entity["classname"] == "trigger_teleport"
                and internal_id.startswith(prefix)
            ):
                self._direct_teleport_from_raw(raw, internal_id[len(prefix):])
        return {"mode": "direct", "raw": raw}

    def _read_editable(
        self, map_name: Any
    ) -> tuple[str, Path, bytes, dict[str, Any]]:
        name, path = self._map_path(map_name)
        try:
            data = path.read_bytes()
        except FileNotFoundError as error:
            raise MapEditError(f"map '{name}' does not exist") from error
        return name, path, data, self._validate_editable_data(data)

    @staticmethod
    def _diff(
        before: bytes,
        after: bytes,
        summary: str,
        added: list[str] | None = None,
        changed: list[str] | None = None,
        deleted: list[str] | None = None,
    ) -> dict[str, Any]:
        def diff_lines(data: bytes) -> list[str]:
            return [
                (
                    f"{STATE_PREFIX}<canonical-state>"
                    if line.startswith((STATE_PREFIX, STATE_PREFIX_V1))
                    else line
                )
                for line in data.decode("utf-8").splitlines()
            ]

        lines = list(
            difflib.unified_diff(
                diff_lines(before),
                diff_lines(after),
                fromfile="before.map",
                tofile="after.map",
                lineterm="",
                n=2,
            )
        )
        truncated = len(lines) > 80
        return {
            "summary": summary,
            "objects_added": added or [],
            "objects_changed": changed or [],
            "objects_deleted": deleted or [],
            "unified": "\n".join(lines[:80]),
            "truncated": truncated,
        }

    @staticmethod
    def _atomic_write(path: Path, data: bytes) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        temporary: Path | None = None
        try:
            with tempfile.NamedTemporaryFile(
                mode="wb", dir=path.parent, prefix=f".{path.name}.", suffix=".tmp", delete=False
            ) as output:
                temporary = Path(output.name)
                output.write(data)
                output.flush()
                os.fsync(output.fileno())
            os.replace(temporary, path)
            temporary = None
        finally:
            if temporary is not None:
                try:
                    temporary.unlink()
                except FileNotFoundError:
                    pass

    @staticmethod
    def _atomic_create(path: Path, data: bytes) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        temporary: Path | None = None
        try:
            with tempfile.NamedTemporaryFile(
                mode="wb", dir=path.parent, prefix=f".{path.name}.", suffix=".tmp", delete=False
            ) as output:
                temporary = Path(output.name)
                output.write(data)
                output.flush()
                os.fsync(output.fileno())
            os.link(temporary, path)
        finally:
            if temporary is not None:
                try:
                    temporary.unlink()
                except FileNotFoundError:
                    pass

    def _discard_transaction(self, token: str) -> None:
        try:
            (self.transaction_root / f"{token}.json").unlink()
        except FileNotFoundError:
            pass

    def _record_transaction(
        self, name: str, before: bytes | None, after: bytes
    ) -> str:
        token = secrets.token_hex(16)
        record = {
            "format": 1,
            "map": name,
            "before_exists": before is not None,
            "before": (
                base64.b64encode(before).decode("ascii") if before is not None else ""
            ),
            "before_revision": _sha256(before) if before is not None else None,
            "after_revision": _sha256(after),
        }
        self._atomic_write(
            self.transaction_root / f"{token}.json",
            json.dumps(record, sort_keys=True, separators=(",", ":")).encode("utf-8"),
        )
        return token

    def _apply(
        self,
        name: str,
        path: Path,
        before: bytes,
        after: bytes,
        expected_revision: Any,
        dry_run: bool,
        diff: dict[str, Any],
    ) -> dict[str, Any]:
        expected = _validate_revision(expected_revision)
        self._validate_managed_data(after)
        after_revision = _sha256(after)
        token = None
        with self._map_lock(name):
            current = path.read_bytes()
            before_revision = _sha256(current)
            if expected != before_revision or current != before:
                raise MapEditError(
                    f"stale map revision: expected {expected}, current revision is {before_revision}"
                )
            if not dry_run and after != before:
                token = self._record_transaction(name, before, after)
                try:
                    self._atomic_write(path, after)
                except Exception:
                    self._discard_transaction(token)
                    raise
        return {
            "map": name,
            "applied": not dry_run and after != before,
            "dry_run": dry_run,
            "revision_before": before_revision,
            "revision_after": after_revision,
            "rollback_token": token,
            "diff": diff,
        }

    def create(self, map_name: Any, template: Any = "initial", dry_run: bool = False) -> dict[str, Any]:
        name, path = self._map_path(map_name)
        if template != "initial":
            raise MapEditError("template must be 'initial'")
        after = _render(_initial_state())
        diff = self._diff(b"", after, f"create map from template '{template}'")
        token = None
        with self._map_lock(name):
            if path.exists():
                raise MapEditError(f"map '{name}' already exists")
            if not dry_run:
                token = self._record_transaction(name, None, after)
                try:
                    self._atomic_create(path, after)
                except Exception:
                    self._discard_transaction(token)
                    raise
        return {
            "map": name,
            "applied": not dry_run,
            "dry_run": dry_run,
            "revision_before": None,
            "revision_after": _sha256(after),
            "rollback_token": token,
            "diff": diff,
            "objects": {
                "entities": ["worldspawn", "spawn-a", "spawn-b"],
                "cuboids": [],
                "point_lights": [],
                "teleports": [],
            },
        }

    def list_maps(self) -> dict[str, Any]:
        maps = []
        for path in sorted(self.maps_root.glob("*.map"), key=lambda item: item.name.lower()):
            data = path.read_bytes()
            managed = True
            objects: dict[str, Any] | None = None
            error: str | None = None
            try:
                state = self._validate_managed_data(data)
                objects = {
                    "entities": [item["id"] for item in state["entities"]],
                    "cuboids": [item["id"] for item in state["cuboids"]],
                    "point_lights": [
                        item["id"] for item in state["point_lights"]
                    ],
                    "teleports": [item["id"] for item in state["teleports"]],
                }
            except MapEditError as caught:
                if data.startswith((
                    STATE_PREFIX.encode("ascii"),
                    STATE_PREFIX_V1.encode("ascii"),
                )):
                    error = str(caught)
                else:
                    managed = False
                    try:
                        raw = _parse_raw_map(data)
                        direct_teleport_ids = sorted(
                            item["value"].removeprefix(
                                "lg-internal-teleport-trigger-"
                            )
                            for entity in raw["entities"]
                            if entity["classname"] == "trigger_teleport"
                            for key, item in entity["properties"].items()
                            if key == "lg_agent_id"
                            and item["value"].startswith(
                                "lg-internal-teleport-trigger-"
                            )
                        )
                        for object_id in direct_teleport_ids:
                            self._direct_teleport_from_raw(raw, object_id)
                        objects = {
                            "entities": [],
                            "cuboids": [],
                            "point_lights": sorted(
                                item["value"]
                                for entity in raw["entities"]
                                if entity["classname"] in {"light", "light_point"}
                                for key, item in entity["properties"].items()
                                if key == "lg_agent_id"
                            ),
                            "teleports": direct_teleport_ids,
                        }
                    except MapEditError as direct_error:
                        error = str(direct_error)
            entry = {
                "map": path.stem,
                "revision": _sha256(data),
                "size_bytes": len(data),
                "managed": managed,
                "editable": error is None,
            }
            if objects is not None:
                entry["objects"] = objects
            if error is not None:
                entry["edit_status"] = error
            maps.append(entry)
        return {"maps": maps, "count": len(maps)}

    def inspect(self, map_name: Any) -> dict[str, Any]:
        name, _path, data, editable = self._read_editable(map_name)
        if editable["mode"] == "direct":
            raw = editable["raw"]
            point_lights = []
            for entity in raw["entities"]:
                object_id = _raw_property(entity, "lg_agent_id")
                if (
                    object_id is not None
                    and entity["classname"] in {"light", "light_point"}
                ):
                    point_lights.append(_point_light_from_raw(entity, object_id))
            teleports = []
            for entity in raw["entities"]:
                internal_id = _raw_property(entity, "lg_agent_id") or ""
                prefix = "lg-internal-teleport-trigger-"
                if entity["classname"] != "trigger_teleport" or not internal_id.startswith(prefix):
                    continue
                object_id = internal_id[len(prefix):]
                teleport, _trigger, _target = self._direct_teleport_from_raw(
                    raw, object_id
                )
                teleports.append(teleport)
            world = raw["world"]
            ambient_color = _raw_color(
                _raw_property(world, "lg_ambient_color") or "255 255 255",
                "ambient color",
            )
            suns = [
                entity for entity in raw["entities"]
                if entity["classname"] == "light_sun"
            ]
            sun = None
            if suns:
                sun_entity = suns[0]
                sun_color = _raw_color(
                    _raw_property(sun_entity, "color")
                    or _raw_property(sun_entity, "_color")
                    or "255 240 200",
                    "sun color",
                )
                sun = {
                    "id": _raw_property(sun_entity, "lg_agent_id") or "sun",
                    "direction": _sun_direction_from_raw(sun_entity),
                    "color": sun_color,
                    "intensity": _raw_float(
                        _raw_property(sun_entity, "intensity"),
                        "sun intensity", 0.7,
                    ),
                }
            return {
                "map": name,
                "revision": _sha256(data),
                "size_bytes": len(data),
                "managed": False,
                "editing_mode": "direct_non_lossy",
                "world_lighting": {
                    "ambient_color": ambient_color,
                    "ambient_intensity": _raw_float(
                        _raw_property(world, "lg_ambient_intensity"),
                        "ambient intensity", 0.3,
                    ),
                    "sun": sun,
                },
                "point_lights": point_lights,
                "teleports": teleports,
                "unowned": {
                    "point_lights": sum(
                        entity["classname"] in {"light", "light_point"}
                        and _raw_property(entity, "lg_agent_id") is None
                        for entity in raw["entities"]
                    ),
                    "teleports": sum(
                        entity["brush_count"]
                        for entity in raw["entities"]
                        if entity["classname"] == "trigger_teleport"
                        and not (
                            _raw_property(entity, "lg_agent_id") or ""
                        ).startswith("lg-internal-teleport-trigger-")
                    ),
                },
                "entities": [],
                "cuboids": [],
            }
        state = editable["state"]
        return {
            "map": name,
            "revision": _sha256(data),
            "size_bytes": len(data),
            "managed": True,
            "template": state["template"],
            "format": state["format"],
            "world_lighting": copy.deepcopy(state["world_lighting"]),
            "entities": copy.deepcopy(state["entities"]),
            "cuboids": copy.deepcopy(state["cuboids"]),
            "point_lights": copy.deepcopy(state["point_lights"]),
            "teleports": copy.deepcopy(state["teleports"]),
        }

    def add_cuboid(
        self, map_name: Any, object_id: Any, minimum: Any, maximum: Any,
        material: Any, expected_revision: Any, dry_run: bool = False,
    ) -> dict[str, Any]:
        name, path, before, state = self._read(map_name)
        object_id = _validate_id(object_id, "id")
        low, high = _bounds(minimum, maximum)
        if any(item["id"] == object_id for item in state["entities"] + state["cuboids"]):
            raise MapEditError(f"managed object id '{object_id}' already exists")
        state["cuboids"].append(
            {"id": object_id, "min": low, "max": high, "material": self._material(material)}
        )
        after = _render(state)
        return self._apply(
            name, path, before, after, expected_revision, dry_run,
            self._diff(before, after, f"add cuboid '{object_id}'", added=[object_id]),
        )

    def copy_cuboid(
        self, map_name: Any, source_id: Any, new_id: Any, offset: Any,
        expected_revision: Any, dry_run: bool = False,
    ) -> dict[str, Any]:
        name, path, before, state = self._read(map_name)
        source_id = _validate_id(source_id, "source_id")
        new_id = _validate_id(new_id, "new_id")
        delta = _vec3(offset, "offset")
        if any(item["id"] == new_id for item in state["entities"] + state["cuboids"]):
            raise MapEditError(f"managed object id '{new_id}' already exists")
        source = next((item for item in state["cuboids"] if item["id"] == source_id), None)
        if source is None:
            raise MapEditError(f"managed cuboid '{source_id}' does not exist")
        duplicate = copy.deepcopy(source)
        duplicate["id"] = new_id
        duplicate["min"] = [source["min"][axis] + delta[axis] for axis in range(3)]
        duplicate["max"] = [source["max"][axis] + delta[axis] for axis in range(3)]
        _bounds(duplicate["min"], duplicate["max"])
        state["cuboids"].append(duplicate)
        after = _render(state)
        return self._apply(
            name, path, before, after, expected_revision, dry_run,
            self._diff(before, after, f"copy cuboid '{source_id}' to '{new_id}'", added=[new_id]),
        )

    def translate_cuboid(
        self, map_name: Any, object_id: Any, offset: Any,
        expected_revision: Any, dry_run: bool = False,
    ) -> dict[str, Any]:
        name, path, before, state = self._read(map_name)
        object_id = _validate_id(object_id, "id")
        delta = _vec3(offset, "offset")
        cuboid = next((item for item in state["cuboids"] if item["id"] == object_id), None)
        if cuboid is None:
            raise MapEditError(f"managed cuboid '{object_id}' does not exist")
        cuboid["min"] = [cuboid["min"][axis] + delta[axis] for axis in range(3)]
        cuboid["max"] = [cuboid["max"][axis] + delta[axis] for axis in range(3)]
        _bounds(cuboid["min"], cuboid["max"])
        after = _render(state)
        return self._apply(
            name, path, before, after, expected_revision, dry_run,
            self._diff(before, after, f"translate cuboid '{object_id}'", changed=[object_id]),
        )

    def resize_cuboid(
        self, map_name: Any, object_id: Any, minimum: Any, maximum: Any,
        expected_revision: Any, dry_run: bool = False,
    ) -> dict[str, Any]:
        name, path, before, state = self._read(map_name)
        object_id = _validate_id(object_id, "id")
        low, high = _bounds(minimum, maximum)
        cuboid = next((item for item in state["cuboids"] if item["id"] == object_id), None)
        if cuboid is None:
            raise MapEditError(f"managed cuboid '{object_id}' does not exist")
        cuboid["min"], cuboid["max"] = low, high
        after = _render(state)
        return self._apply(
            name, path, before, after, expected_revision, dry_run,
            self._diff(before, after, f"resize cuboid '{object_id}'", changed=[object_id]),
        )

    def delete_cuboid(
        self, map_name: Any, object_id: Any, expected_revision: Any,
        dry_run: bool = False,
    ) -> dict[str, Any]:
        name, path, before, state = self._read(map_name)
        object_id = _validate_id(object_id, "id")
        kept = [item for item in state["cuboids"] if item["id"] != object_id]
        if len(kept) == len(state["cuboids"]):
            raise MapEditError(f"managed cuboid '{object_id}' does not exist")
        state["cuboids"] = kept
        after = _render(state)
        return self._apply(
            name, path, before, after, expected_revision, dry_run,
            self._diff(before, after, f"delete cuboid '{object_id}'", deleted=[object_id]),
        )

    def set_material(
        self, map_name: Any, object_id: Any, material: Any, expected_revision: Any,
        dry_run: bool = False,
    ) -> dict[str, Any]:
        name, path, before, state = self._read(map_name)
        object_id = _validate_id(object_id, "id")
        cuboid = next((item for item in state["cuboids"] if item["id"] == object_id), None)
        if cuboid is None:
            raise MapEditError(f"managed cuboid '{object_id}' does not exist")
        cuboid["material"] = self._material(material)
        after = _render(state)
        return self._apply(
            name, path, before, after, expected_revision, dry_run,
            self._diff(before, after, f"set material on '{object_id}'", changed=[object_id]),
        )

    def set_entity_properties(
        self, map_name: Any, entity_id: Any, expected_revision: Any,
        *, origin: Any = None, angle: Any = None, yaw: Any = None,
        bounds_min: Any = None, bounds_max: Any = None, dry_run: bool = False,
    ) -> dict[str, Any]:
        name, path, before, state = self._read(map_name)
        entity_id = _validate_id(entity_id, "entity_id")
        entity = next((item for item in state["entities"] if item["id"] == entity_id), None)
        if entity is None:
            raise MapEditError(f"managed entity '{entity_id}' does not exist")
        changed_fields = [value is not None for value in (origin, angle, yaw, bounds_min, bounds_max)]
        if not any(changed_fields):
            raise MapEditError("at least one supported property must be supplied")
        if entity["classname"] == "worldspawn":
            if origin is not None or angle is not None or yaw is not None:
                raise MapEditError("worldspawn supports only bounds_min and bounds_max")
            current_min = [float(item) for item in entity["properties"]["lg_bounds_min"].split()]
            current_max = [float(item) for item in entity["properties"]["lg_bounds_max"].split()]
            low = _vec3(bounds_min, "bounds_min") if bounds_min is not None else current_min
            high = _vec3(bounds_max, "bounds_max") if bounds_max is not None else current_max
            for axis in range(3):
                if low[axis] >= high[axis]:
                    raise MapEditError("worldspawn bounds_min must be less than bounds_max")
            entity["properties"]["lg_bounds_min"] = _string_vec3(low)
            entity["properties"]["lg_bounds_max"] = _string_vec3(high)
        elif entity["classname"] == "lg_spawn":
            if bounds_min is not None or bounds_max is not None:
                raise MapEditError("lg_spawn does not support world bounds")
            if origin is not None:
                entity["properties"]["origin"] = _string_vec3(_vec3(origin, "origin"))
            for key, value in (("angle", angle), ("yaw", yaw)):
                if value is not None and (
                    isinstance(value, bool) or not isinstance(value, (int, float))
                ):
                    raise MapEditError(f"{key} must be a number")
            if angle is not None and yaw is not None:
                raise MapEditError("set either angle or yaw, not both")
            heading = angle if angle is not None else yaw
            if heading is not None:
                number = float(heading)
                if not math.isfinite(number) or abs(number) > MAX_COORDINATE:
                    raise MapEditError("spawn heading is outside the allowed numeric range")
                entity["properties"]["angle"] = _format_number(number)
                entity["properties"].pop("yaw", None)
        after = _render(state)
        return self._apply(
            name, path, before, after, expected_revision, dry_run,
            self._diff(before, after, f"set properties on entity '{entity_id}'", changed=[entity_id]),
        )

    def _mutate_lighting_operation(
        self, state: dict[str, Any], operation: dict[str, Any]
    ) -> tuple[list[str], list[str], list[str]]:
        if not isinstance(operation, dict) or not isinstance(operation.get("op"), str):
            raise MapEditError("batch operation must be an object with an op")
        op = operation["op"]
        common_point_fields = {
            "origin", "color", "intensity", "radius", "casts_shadows",
            "source_radius", "priority", "flicker_enabled", "flicker_seed",
            "flicker_frequency", "flicker_min", "flicker_max",
        }
        if op == "add_point_light":
            allowed = {"op", "id", *common_point_fields}
            required = {"op", "id", "origin", "color", "intensity", "radius"}
            if not set(operation).issubset(allowed) or not required.issubset(operation):
                raise MapEditError("add_point_light has missing or unsupported fields")
            light_id = _validate_id(operation["id"], "point light id")
            all_ids = {
                item["id"] for item in
                state["entities"] + state["cuboids"] + state["point_lights"]
                + state["teleports"]
            }
            sun = state["world_lighting"]["sun"]
            if sun is not None:
                all_ids.add(sun["id"])
            if light_id in all_ids:
                raise MapEditError(f"managed object id '{light_id}' already exists")
            if len(state["point_lights"]) >= MAX_POINT_LIGHTS:
                raise MapEditError(
                    f"managed maps may contain at most {MAX_POINT_LIGHTS} point lights"
                )
            kwargs = {
                key: operation[key] for key in common_point_fields
                if key in operation
                and key not in {"origin", "color", "intensity", "radius"}
            }
            state["point_lights"].append(_make_point_light(
                light_id, operation["origin"], operation["color"],
                operation["intensity"], operation["radius"], **kwargs,
            ))
            return [light_id], [], []
        if op == "update_point_light":
            allowed = {"op", "id", *common_point_fields}
            if not set(operation).issubset(allowed) or "id" not in operation:
                raise MapEditError("update_point_light has missing or unsupported fields")
            supplied = common_point_fields.intersection(operation)
            if not supplied:
                raise MapEditError("update_point_light needs at least one changed field")
            light_id = _validate_id(operation["id"], "point light id")
            light = next(
                (item for item in state["point_lights"] if item["id"] == light_id),
                None,
            )
            if light is None:
                raise MapEditError(f"managed point light '{light_id}' does not exist")
            flicker = light["flicker"]
            flat = {
                "origin": light["origin"],
                "color": light["color"],
                "intensity": light["intensity"],
                "radius": light["radius"],
                "casts_shadows": light["casts_shadows"],
                "source_radius": light["source_radius"],
                "priority": light["priority"],
                "flicker_enabled": flicker["enabled"],
                "flicker_seed": flicker["seed"],
                "flicker_frequency": flicker["frequency"],
                "flicker_min": flicker["min"],
                "flicker_max": flicker["max"],
            }
            flat.update({field: operation[field] for field in supplied})
            if flat["flicker_enabled"] is False:
                flat["flicker_frequency"] = 0.0
            elif (
                "flicker_enabled" in supplied
                and "flicker_frequency" not in supplied
                and not flicker["enabled"]
            ):
                flat["flicker_frequency"] = None
            replacement = _make_point_light(
                light_id, flat.pop("origin"), flat.pop("color"),
                flat.pop("intensity"), flat.pop("radius"), **flat,
            )
            light.clear()
            light.update(replacement)
            return [], [light_id], []
        if op == "remove_point_light":
            if set(operation) != {"op", "id"}:
                raise MapEditError("remove_point_light accepts only op and id")
            light_id = _validate_id(operation["id"], "point light id")
            kept = [
                item for item in state["point_lights"]
                if item["id"] != light_id
            ]
            if len(kept) == len(state["point_lights"]):
                raise MapEditError(f"managed point light '{light_id}' does not exist")
            state["point_lights"] = kept
            return [], [], [light_id]
        if op == "add_teleport":
            if set(operation) != {
                "op", "id", "min", "max", "destination", "exit_yaw"
            }:
                raise MapEditError("add_teleport has missing or unsupported fields")
            object_id = _validate_id(operation["id"], "teleport id")
            all_ids = {
                item["id"] for item in
                state["entities"] + state["cuboids"] + state["point_lights"]
                + state["teleports"]
            }
            sun = state["world_lighting"]["sun"]
            if sun is not None:
                all_ids.add(sun["id"])
            if object_id in all_ids:
                raise MapEditError(f"managed object id '{object_id}' already exists")
            if len(state["teleports"]) >= MAX_TELEPORTS:
                raise MapEditError(
                    f"managed maps may contain at most {MAX_TELEPORTS} teleports"
                )
            low, high = _bounds(operation["min"], operation["max"])
            state["teleports"].append({
                "id": object_id,
                "min": low,
                "max": high,
                "destination": _vec3(
                    operation["destination"], "teleport destination"
                ),
                "exit_yaw": _number(
                    operation["exit_yaw"], "teleport exit_yaw",
                    minimum=-MAX_COORDINATE, maximum=MAX_COORDINATE,
                ),
            })
            return [object_id], [], []
        if op == "update_teleport":
            allowed = {
                "op", "id", "min", "max", "destination", "exit_yaw"
            }
            if not set(operation).issubset(allowed) or "id" not in operation:
                raise MapEditError("update_teleport has missing or unsupported fields")
            supplied = set(operation).intersection(
                {"min", "max", "destination", "exit_yaw"}
            )
            if not supplied:
                raise MapEditError("update_teleport needs at least one changed field")
            object_id = _validate_id(operation["id"], "teleport id")
            teleport = next(
                (item for item in state["teleports"] if item["id"] == object_id),
                None,
            )
            if teleport is None:
                raise MapEditError(f"managed teleport '{object_id}' does not exist")
            low, high = _bounds(
                operation.get("min", teleport["min"]),
                operation.get("max", teleport["max"]),
            )
            teleport["min"], teleport["max"] = low, high
            if "destination" in operation:
                teleport["destination"] = _vec3(
                    operation["destination"], "teleport destination"
                )
            if "exit_yaw" in operation:
                teleport["exit_yaw"] = _number(
                    operation["exit_yaw"], "teleport exit_yaw",
                    minimum=-MAX_COORDINATE, maximum=MAX_COORDINATE,
                )
            return [], [object_id], []
        if op == "remove_teleport":
            if set(operation) != {"op", "id"}:
                raise MapEditError("remove_teleport accepts only op and id")
            object_id = _validate_id(operation["id"], "teleport id")
            kept = [
                item for item in state["teleports"] if item["id"] != object_id
            ]
            if len(kept) == len(state["teleports"]):
                raise MapEditError(f"managed teleport '{object_id}' does not exist")
            state["teleports"] = kept
            return [], [], [object_id]
        if op == "set_world_lighting":
            allowed = {
                "op", "ambient_color", "ambient_intensity", "sun_enabled",
                "sun_id", "sun_direction", "sun_color", "sun_intensity",
            }
            if not set(operation).issubset(allowed):
                raise MapEditError("set_world_lighting has unsupported fields")
            if len(operation) == 1:
                raise MapEditError("set_world_lighting needs at least one changed field")
            lighting = state["world_lighting"]
            if "ambient_color" in operation:
                lighting["ambient_color"] = _color(
                    operation["ambient_color"], "ambient color"
                )
            if "ambient_intensity" in operation:
                lighting["ambient_intensity"] = _number(
                    operation["ambient_intensity"], "ambient intensity", minimum=0.0
                )
            sun_fields = {
                "sun_id", "sun_direction", "sun_color", "sun_intensity"
            }
            if operation.get("sun_enabled") is False:
                if sun_fields.intersection(operation):
                    raise MapEditError("disabled sun cannot include sun settings")
                lighting["sun"] = None
            elif operation.get("sun_enabled") is True:
                current = lighting["sun"] or {
                    "id": "sun",
                    "direction": [0.3, -0.5, -1.0],
                    "color": [255.0, 240.0, 200.0],
                    "intensity": 0.7,
                }
                lighting["sun"] = {
                    "id": (
                        _validate_id(operation["sun_id"], "sun id")
                        if "sun_id" in operation else current["id"]
                    ),
                    "direction": (
                        _vec3(operation["sun_direction"], "sun direction")
                        if "sun_direction" in operation else current["direction"]
                    ),
                    "color": (
                        _color(operation["sun_color"], "sun color")
                        if "sun_color" in operation else current["color"]
                    ),
                    "intensity": (
                        _number(
                            operation["sun_intensity"], "sun intensity", minimum=0.0
                        )
                        if "sun_intensity" in operation else current["intensity"]
                    ),
                }
            elif sun_fields.intersection(operation):
                if lighting["sun"] is None:
                    raise MapEditError("enable the sun before changing sun settings")
                current = lighting["sun"]
                if "sun_id" in operation:
                    current["id"] = _validate_id(operation["sun_id"], "sun id")
                if "sun_direction" in operation:
                    current["direction"] = _vec3(
                        operation["sun_direction"], "sun direction"
                    )
                if "sun_color" in operation:
                    current["color"] = _color(operation["sun_color"], "sun color")
                if "sun_intensity" in operation:
                    current["intensity"] = _number(
                        operation["sun_intensity"], "sun intensity", minimum=0.0
                    )
            if "sun_enabled" in operation and not isinstance(
                operation["sun_enabled"], bool
            ):
                raise MapEditError("sun_enabled must be a boolean")
            return [], ["worldspawn"], []
        if op == "add_cuboid":
            if set(operation) != {"op", "id", "min", "max", "material"}:
                raise MapEditError("add_cuboid has missing or unsupported fields")
            object_id = _validate_id(operation["id"], "cuboid id")
            all_ids = {
                item["id"] for item in
                state["entities"] + state["cuboids"] + state["point_lights"]
                + state["teleports"]
            }
            sun = state["world_lighting"]["sun"]
            if sun is not None:
                all_ids.add(sun["id"])
            if object_id in all_ids:
                raise MapEditError(f"managed object id '{object_id}' already exists")
            low, high = _bounds(operation["min"], operation["max"])
            state["cuboids"].append({
                "id": object_id, "min": low, "max": high,
                "material": self._material(operation["material"]),
            })
            return [object_id], [], []
        if op == "copy_cuboid":
            if set(operation) != {"op", "source_id", "new_id", "offset"}:
                raise MapEditError("copy_cuboid has missing or unsupported fields")
            source_id = _validate_id(operation["source_id"], "source_id")
            new_id = _validate_id(operation["new_id"], "new_id")
            if any(
                item["id"] == new_id for item in
                state["entities"] + state["cuboids"] + state["point_lights"]
                + state["teleports"]
            ):
                raise MapEditError(f"managed object id '{new_id}' already exists")
            source = next(
                (item for item in state["cuboids"] if item["id"] == source_id),
                None,
            )
            if source is None:
                raise MapEditError(f"managed cuboid '{source_id}' does not exist")
            offset = _vec3(operation["offset"], "offset")
            duplicate = copy.deepcopy(source)
            duplicate["id"] = new_id
            duplicate["min"] = [
                source["min"][axis] + offset[axis] for axis in range(3)
            ]
            duplicate["max"] = [
                source["max"][axis] + offset[axis] for axis in range(3)
            ]
            state["cuboids"].append(duplicate)
            return [new_id], [], []
        if op in {"translate_cuboid", "resize_cuboid", "set_material"}:
            allowed_by_op = {
                "translate_cuboid": {"op", "id", "offset"},
                "resize_cuboid": {"op", "id", "min", "max"},
                "set_material": {"op", "id", "material"},
            }
            if set(operation) != allowed_by_op[op]:
                raise MapEditError(f"{op} has missing or unsupported fields")
            object_id = _validate_id(operation["id"], "cuboid id")
            cuboid = next(
                (item for item in state["cuboids"] if item["id"] == object_id),
                None,
            )
            if cuboid is None:
                raise MapEditError(f"managed cuboid '{object_id}' does not exist")
            if op == "translate_cuboid":
                offset = _vec3(operation["offset"], "offset")
                cuboid["min"] = [
                    cuboid["min"][axis] + offset[axis] for axis in range(3)
                ]
                cuboid["max"] = [
                    cuboid["max"][axis] + offset[axis] for axis in range(3)
                ]
            elif op == "resize_cuboid":
                cuboid["min"], cuboid["max"] = _bounds(
                    operation["min"], operation["max"]
                )
            else:
                cuboid["material"] = self._material(operation["material"])
            return [], [object_id], []
        if op == "delete_cuboid":
            if set(operation) != {"op", "id"}:
                raise MapEditError("delete_cuboid accepts only op and id")
            object_id = _validate_id(operation["id"], "cuboid id")
            kept = [
                item for item in state["cuboids"] if item["id"] != object_id
            ]
            if len(kept) == len(state["cuboids"]):
                raise MapEditError(f"managed cuboid '{object_id}' does not exist")
            state["cuboids"] = kept
            return [], [], [object_id]
        if op == "set_entity_properties":
            allowed = {
                "op", "entity_id", "origin", "angle", "yaw",
                "bounds_min", "bounds_max",
            }
            if not set(operation).issubset(allowed) or "entity_id" not in operation:
                raise MapEditError("set_entity_properties has missing or unsupported fields")
            supplied = set(operation).intersection(allowed - {"op", "entity_id"})
            if not supplied:
                raise MapEditError("set_entity_properties needs a changed field")
            entity_id = _validate_id(operation["entity_id"], "entity_id")
            entity = next(
                (item for item in state["entities"] if item["id"] == entity_id),
                None,
            )
            if entity is None:
                raise MapEditError(f"managed entity '{entity_id}' does not exist")
            if entity["classname"] == "worldspawn":
                if supplied.intersection({"origin", "angle", "yaw"}):
                    raise MapEditError("worldspawn supports only bounds_min and bounds_max")
                current_min = [
                    float(item)
                    for item in entity["properties"]["lg_bounds_min"].split()
                ]
                current_max = [
                    float(item)
                    for item in entity["properties"]["lg_bounds_max"].split()
                ]
                low = (
                    _vec3(operation["bounds_min"], "bounds_min")
                    if "bounds_min" in operation else current_min
                )
                high = (
                    _vec3(operation["bounds_max"], "bounds_max")
                    if "bounds_max" in operation else current_max
                )
                if any(low[axis] >= high[axis] for axis in range(3)):
                    raise MapEditError(
                        "worldspawn bounds_min must be less than bounds_max"
                    )
                entity["properties"]["lg_bounds_min"] = _string_vec3(low)
                entity["properties"]["lg_bounds_max"] = _string_vec3(high)
            else:
                if supplied.intersection({"bounds_min", "bounds_max"}):
                    raise MapEditError("lg_spawn does not support world bounds")
                if "origin" in operation:
                    entity["properties"]["origin"] = _string_vec3(
                        _vec3(operation["origin"], "origin")
                    )
                if "angle" in operation and "yaw" in operation:
                    raise MapEditError("set either angle or yaw, not both")
                heading = operation.get("angle", operation.get("yaw"))
                if heading is not None:
                    entity["properties"]["angle"] = _format_number(
                        _number(
                            heading, "spawn heading",
                            minimum=-MAX_COORDINATE, maximum=MAX_COORDINATE,
                        )
                    )
                    entity["properties"].pop("yaw", None)
            return [], [entity_id], []
        raise MapEditError(f"unsupported batch operation '{op}'")

    def _apply_lighting_operations(
        self, map_name: Any, operations: Any, expected_revision: Any,
        dry_run: bool, summary: str,
    ) -> dict[str, Any]:
        if not isinstance(operations, list) or not operations:
            raise MapEditError("operations must be a non-empty array")
        if len(operations) > 128:
            raise MapEditError("a batch may contain at most 128 operations")
        name, path, before, editable = self._read_editable(map_name)
        if editable["mode"] == "direct":
            return self._apply_direct_operations(
                name, path, before, operations, expected_revision, dry_run,
                summary,
            )
        state = editable["state"]
        working = copy.deepcopy(state)
        added: list[str] = []
        changed: list[str] = []
        deleted: list[str] = []
        for index, operation in enumerate(operations):
            try:
                operation_added, operation_changed, operation_deleted = (
                    self._mutate_lighting_operation(working, operation)
                )
                _validate_state(working)
            except MapEditError as error:
                raise MapEditError(f"operation {index}: {error}") from error
            added.extend(operation_added)
            changed.extend(operation_changed)
            deleted.extend(operation_deleted)
        after = _render(working)
        return self._apply(
            name, path, before, after, expected_revision, dry_run,
            self._diff(
                before, after, summary,
                added=added, changed=changed, deleted=deleted,
            ),
        )

    @staticmethod
    def _direct_check_position(
        raw: dict[str, Any], position: list[float], label: str
    ) -> None:
        low, high = _raw_world_bounds(raw)
        if any(
            position[axis] < low[axis] or position[axis] > high[axis]
            for axis in range(3)
        ):
            raise MapEditError(f"{label} is outside worldspawn bounds")

    @staticmethod
    def _direct_set_world_property(
        text: str, raw: dict[str, Any], key: str, value: str
    ) -> str:
        item = raw["world"]["properties"].get(key)
        if item is not None:
            return _replace_text_spans(
                text, [(item["value_start"], item["value_end"], value)]
            )
        classname = raw["world"]["properties"]["classname"]
        if classname["line_end"] >= raw["world"]["end"]:
            insertion_at = raw["world"]["end"] - 1
            insertion = f' "{key}" "{value}" '
            return text[:insertion_at] + insertion + text[insertion_at:]
        insertion = f'"{key}" "{value}"{_preferred_newline(text)}'
        return text[:classname["line_end"]] + insertion + text[classname["line_end"]:]

    @staticmethod
    def _direct_teleport_text(teleport: dict[str, Any]) -> str:
        object_id = teleport["id"]
        target_name = _teleport_target_name(object_id)
        trigger_id = f"lg-internal-teleport-trigger-{object_id}"
        target_id = f"lg-internal-teleport-target-{object_id}"
        trigger_lines = _entity_lines({
            "id": trigger_id,
            "classname": "trigger_teleport",
            "properties": {
                "target": target_name,
                "lg_api_id": object_id,
                "lg_api_min": _string_vec3(teleport["min"]),
                "lg_api_max": _string_vec3(teleport["max"]),
                "lg_api_destination": _string_vec3(teleport["destination"]),
                "lg_api_exit_yaw": _format_number(teleport["exit_yaw"]),
            },
        })
        trigger_lines.insert(-1, "{")
        trigger_lines[-1:-1] = [
            *_cuboid_faces({
                "min": teleport["min"], "max": teleport["max"],
                "material": "common/trigger",
            }),
            "}",
        ]
        target_text = _render_raw_entity(
            "target_position",
            target_id,
            {
                "targetname": target_name,
                "origin": _string_vec3(teleport["destination"]),
                "angle": _format_number(teleport["exit_yaw"]),
            },
        )
        return "\n".join(trigger_lines) + "\n" + target_text

    @staticmethod
    def _direct_teleport_from_raw(
        raw: dict[str, Any], object_id: str
    ) -> tuple[dict[str, Any], dict[str, Any], dict[str, Any]]:
        object_id = _validate_id(object_id, "teleport id")

        def invalid(detail: str) -> None:
            raise MapEditError(
                f"direct-map teleport '{object_id}' owned data is inconsistent: "
                f"{detail}"
            )

        trigger_id = f"lg-internal-teleport-trigger-{object_id}"
        target_id = f"lg-internal-teleport-target-{object_id}"
        trigger = _raw_entity_by_agent_id(
            raw, trigger_id, {"trigger_teleport"}
        )
        target = _raw_entity_by_agent_id(raw, target_id, {"target_position"})
        if trigger is None or target is None:
            raise MapEditError(
                f"direct-map teleport '{object_id}' is not owned by the typed API"
            )
        target_name = _teleport_target_name(object_id)
        if (
            _raw_property(trigger, "target") != target_name
            or _raw_property(target, "targetname") != target_name
        ):
            invalid("trigger link or target name changed")
        if _raw_property(trigger, "lg_api_id") != object_id:
            invalid("public ID metadata changed")
        matching_triggers = [
            entity for entity in raw["entities"]
            if entity["classname"] == "trigger_teleport"
            and _raw_property(entity, "target") == target_name
        ]
        matching_targets = [
            entity for entity in raw["entities"]
            if entity["classname"] == "target_position"
            and _raw_property(entity, "targetname") == target_name
        ]
        if len(matching_triggers) != 1 or matching_triggers[0] is not trigger:
            invalid("expected one owned trigger for its target")
        if len(matching_targets) != 1 or matching_targets[0] is not target:
            invalid("expected one owned target")
        if trigger["brush_count"] != 1:
            invalid("trigger must contain exactly one top-level brush")
        if target["brush_count"] != 0:
            invalid("target_position must not contain brushes")
        low, high = _bounds(
            _raw_vec(_raw_property(trigger, "lg_api_min"), "teleport min"),
            _raw_vec(_raw_property(trigger, "lg_api_max"), "teleport max"),
        )
        brush_low, brush_high = _raw_brush_bounds(raw, trigger)
        if not _vectors_match(low, brush_low) or not _vectors_match(high, brush_high):
            invalid("trigger brush bounds do not match API metadata")
        destination = _raw_vec(
            _raw_property(trigger, "lg_api_destination"),
            "teleport destination",
        )
        target_origin = _raw_vec(
            _raw_property(target, "origin"), "teleport target origin"
        )
        if not _vectors_match(destination, target_origin):
            invalid("target origin does not match API metadata")
        exit_yaw = _raw_float(
            _raw_property(trigger, "lg_api_exit_yaw"),
            "teleport exit_yaw",
            0.0,
        )
        target_angle_text = _raw_property(target, "angle")
        if target_angle_text is None:
            invalid("target angle is missing")
        target_angle = _raw_float(
            target_angle_text, "teleport target angle", 0.0
        )
        if abs(exit_yaw - target_angle) > 0.00001:
            invalid("target angle does not match API metadata")
        teleport = {
            "id": object_id,
            "min": low,
            "max": high,
            "destination": destination,
            "exit_yaw": exit_yaw,
        }
        return teleport, trigger, target

    def _apply_direct_operations(
        self, name: str, path: Path, before: bytes, operations: Any,
        expected_revision: Any, dry_run: bool, summary: str,
    ) -> dict[str, Any]:
        if not isinstance(operations, list) or not operations:
            raise MapEditError("operations must be a non-empty array")
        if len(operations) > 128:
            raise MapEditError("a batch may contain at most 128 operations")
        text = before.decode("utf-8")
        added: list[str] = []
        changed: list[str] = []
        deleted: list[str] = []
        for index, operation in enumerate(operations):
            try:
                raw = _parse_raw_map(text.encode("utf-8"))
                op = operation.get("op") if isinstance(operation, dict) else None
                if op == "add_point_light":
                    allowed = {
                        "op", "id", "origin", "color", "intensity", "radius",
                        "casts_shadows", "source_radius", "priority",
                        "flicker_enabled", "flicker_seed", "flicker_frequency",
                        "flicker_min", "flicker_max",
                    }
                    required = {"op", "id", "origin", "color", "intensity", "radius"}
                    if not set(operation).issubset(allowed) or not required.issubset(operation):
                        raise MapEditError("add_point_light has missing or unsupported fields")
                    object_id = _validate_id(operation["id"], "point light id")
                    if object_id in raw["agent_ids"]:
                        raise MapEditError(f"managed object id '{object_id}' already exists")
                    kwargs = {
                        key: operation[key] for key in allowed
                        if key in operation and key not in required
                        and key != "op"
                    }
                    light = _make_point_light(
                        object_id, operation["origin"], operation["color"],
                        operation["intensity"], operation["radius"], **kwargs,
                    )
                    self._direct_check_position(
                        raw, light["origin"], f"point light '{object_id}'"
                    )
                    text = _append_raw_entities(text, [
                        _render_raw_entity(
                            "light_point", object_id, _light_properties(light)
                        )
                    ])
                    added.append(object_id)
                elif op == "update_point_light":
                    object_id = _validate_id(operation.get("id"), "point light id")
                    entity = _raw_entity_by_agent_id(
                        raw, object_id, {"light", "light_point"}
                    )
                    if entity is None:
                        raise MapEditError(
                            f"direct-map point light '{object_id}' is not owned "
                            "by the typed API"
                        )
                    current = _point_light_from_raw(entity, object_id)
                    flicker = current["flicker"]
                    flat = {
                        "origin": current["origin"], "color": current["color"],
                        "intensity": current["intensity"], "radius": current["radius"],
                        "casts_shadows": current["casts_shadows"],
                        "source_radius": current["source_radius"],
                        "priority": current["priority"],
                        "flicker_enabled": flicker["enabled"],
                        "flicker_seed": flicker["seed"],
                        "flicker_frequency": flicker["frequency"],
                        "flicker_min": flicker["min"], "flicker_max": flicker["max"],
                    }
                    allowed = {"op", "id", *flat}
                    if not set(operation).issubset(allowed) or len(operation) <= 2:
                        raise MapEditError("update_point_light has missing or unsupported fields")
                    for key in flat:
                        if key in operation:
                            flat[key] = operation[key]
                    if flat["flicker_enabled"] is False:
                        flat["flicker_frequency"] = 0.0
                    elif (
                        "flicker_enabled" in operation
                        and "flicker_frequency" not in operation
                        and not flicker["enabled"]
                    ):
                        flat["flicker_frequency"] = None
                    light = _make_point_light(
                        object_id, flat.pop("origin"), flat.pop("color"),
                        flat.pop("intensity"), flat.pop("radius"), **flat,
                    )
                    self._direct_check_position(
                        raw, light["origin"], f"point light '{object_id}'"
                    )
                    replacement = _render_raw_entity(
                        "light_point", object_id, _light_properties(light)
                    )
                    replacement = _match_newlines(
                        replacement, text[entity["start"]:entity["end"]]
                    )
                    text = _replace_text_spans(
                        text, [(entity["start"], entity["end"], replacement)]
                    )
                    changed.append(object_id)
                elif op == "remove_point_light":
                    if set(operation) != {"op", "id"}:
                        raise MapEditError("remove_point_light accepts only op and id")
                    object_id = _validate_id(operation["id"], "point light id")
                    entity = _raw_entity_by_agent_id(
                        raw, object_id, {"light", "light_point"}
                    )
                    if entity is None:
                        raise MapEditError(
                            f"direct-map point light '{object_id}' is not owned "
                            "by the typed API"
                        )
                    text = _replace_text_spans(
                        text, [(entity["start"], entity["end"], "")]
                    )
                    deleted.append(object_id)
                elif op == "set_world_lighting":
                    allowed = {
                        "op", "ambient_color", "ambient_intensity", "sun_enabled",
                        "sun_id", "sun_direction", "sun_color", "sun_intensity",
                    }
                    if not set(operation).issubset(allowed) or len(operation) == 1:
                        raise MapEditError("set_world_lighting has unsupported fields")
                    if "ambient_color" in operation:
                        color = _color(operation["ambient_color"], "ambient color")
                        text = self._direct_set_world_property(
                            text, raw, "lg_ambient_color", _string_color_map(color)
                        )
                        raw = _parse_raw_map(text.encode("utf-8"))
                    if "ambient_intensity" in operation:
                        intensity = _number(
                            operation["ambient_intensity"],
                            "ambient intensity", minimum=0.0,
                        )
                        text = self._direct_set_world_property(
                            text, raw, "lg_ambient_intensity",
                            _format_number(intensity),
                        )
                    sun_fields = {
                        "sun_id", "sun_direction", "sun_color", "sun_intensity"
                    }
                    if "sun_enabled" in operation or sun_fields.intersection(operation):
                        raw = _parse_raw_map(text.encode("utf-8"))
                        suns = [
                            entity for entity in raw["entities"]
                            if entity["classname"] == "light_sun"
                        ]
                        current = suns[0] if suns else None
                        if operation.get("sun_enabled") is False:
                            if sun_fields.intersection(operation):
                                raise MapEditError("disabled sun cannot include sun settings")
                            if current is not None:
                                text = _replace_text_spans(
                                    text, [(current["start"], current["end"], "")]
                                )
                        else:
                            sun_id = _validate_id(
                                operation.get(
                                    "sun_id",
                                    (
                                        _raw_property(current, "lg_agent_id")
                                        if current is not None else None
                                    )
                                    or "sun",
                                ),
                                "sun id",
                            )
                            direction = (
                                _vec3(
                                    operation["sun_direction"],
                                    "sun direction",
                                )
                                if "sun_direction" in operation
                                else _sun_direction_from_raw(current)
                            )
                            color = (
                                _color(operation["sun_color"], "sun color")
                                if "sun_color" in operation
                                else _raw_color(
                                    (
                                        _raw_property(current, "color")
                                        or _raw_property(current, "_color")
                                    )
                                    if current is not None else "255 240 200",
                                    "sun color",
                                )
                            )
                            intensity = _number(
                                operation.get(
                                    "sun_intensity",
                                    _raw_float(
                                        _raw_property(current, "intensity")
                                        if current is not None else None,
                                        "sun intensity", 0.7,
                                    ),
                                ),
                                "sun intensity", minimum=0.0,
                            )
                            sun = {
                                "id": sun_id,
                                "direction": direction,
                                "color": color,
                                "intensity": intensity,
                            }
                            _validate_sun(sun)
                            replacement = _render_raw_entity(
                                "light_sun", sun_id, {
                                    "direction": _string_vec3(sun["direction"]),
                                    "color": _string_color_map(sun["color"]),
                                    "intensity": _format_number(sun["intensity"]),
                                }
                            )
                            if current is None:
                                text = _append_raw_entities(text, [replacement])
                            else:
                                replacement = _match_newlines(
                                    replacement,
                                    text[current["start"]:current["end"]],
                                )
                                text = _replace_text_spans(
                                    text,
                                    [(current["start"], current["end"], replacement)],
                                )
                    changed.append("worldspawn")
                elif op == "add_teleport":
                    if set(operation) != {
                        "op", "id", "min", "max", "destination", "exit_yaw"
                    }:
                        raise MapEditError("add_teleport has missing or unsupported fields")
                    object_id = _validate_id(operation["id"], "teleport id")
                    trigger_id = f"lg-internal-teleport-trigger-{object_id}"
                    target_id = f"lg-internal-teleport-target-{object_id}"
                    if {trigger_id, target_id}.intersection(raw["agent_ids"]):
                        raise MapEditError(f"managed object id '{object_id}' already exists")
                    target_name = _teleport_target_name(object_id)
                    if any(
                        entity["classname"] == "target_position"
                        and _raw_property(entity, "targetname") == target_name
                        for entity in raw["entities"]
                    ):
                        raise MapEditError(
                            f"teleport targetname '{target_name}' already exists"
                        )
                    low, high = _bounds(operation["min"], operation["max"])
                    destination = _vec3(
                        operation["destination"], "teleport destination"
                    )
                    self._direct_check_position(raw, low, f"teleport '{object_id}' min")
                    self._direct_check_position(raw, high, f"teleport '{object_id}' max")
                    self._direct_check_position(
                        raw, destination, f"teleport '{object_id}' destination"
                    )
                    teleport = {
                        "id": object_id, "min": low, "max": high,
                        "destination": destination,
                        "exit_yaw": _number(
                            operation["exit_yaw"], "teleport exit_yaw",
                            minimum=-MAX_COORDINATE, maximum=MAX_COORDINATE,
                        ),
                    }
                    text = _append_raw_entities(
                        text, [self._direct_teleport_text(teleport)]
                    )
                    added.append(object_id)
                elif op == "update_teleport":
                    object_id = _validate_id(operation.get("id"), "teleport id")
                    teleport, trigger, target = self._direct_teleport_from_raw(
                        raw, object_id
                    )
                    allowed = {
                        "op", "id", "min", "max", "destination", "exit_yaw"
                    }
                    if not set(operation).issubset(allowed) or len(operation) <= 2:
                        raise MapEditError("update_teleport has missing or unsupported fields")
                    low, high = _bounds(
                        operation.get("min", teleport["min"]),
                        operation.get("max", teleport["max"]),
                    )
                    teleport["min"], teleport["max"] = low, high
                    if "destination" in operation:
                        teleport["destination"] = _vec3(
                            operation["destination"], "teleport destination"
                        )
                    if "exit_yaw" in operation:
                        teleport["exit_yaw"] = _number(
                            operation["exit_yaw"], "teleport exit_yaw",
                            minimum=-MAX_COORDINATE, maximum=MAX_COORDINATE,
                        )
                    self._direct_check_position(raw, low, f"teleport '{object_id}' min")
                    self._direct_check_position(raw, high, f"teleport '{object_id}' max")
                    self._direct_check_position(
                        raw, teleport["destination"],
                        f"teleport '{object_id}' destination",
                    )
                    text = _replace_text_spans(
                        text,
                        [
                            (
                                trigger["start"], trigger["end"],
                                _match_newlines(
                                    self._direct_teleport_text(teleport),
                                    text[trigger["start"]:trigger["end"]],
                                ),
                            ),
                            (target["start"], target["end"], ""),
                        ],
                    )
                    changed.append(object_id)
                elif op == "remove_teleport":
                    if set(operation) != {"op", "id"}:
                        raise MapEditError("remove_teleport accepts only op and id")
                    object_id = _validate_id(operation["id"], "teleport id")
                    _teleport, trigger, target = self._direct_teleport_from_raw(
                        raw, object_id
                    )
                    text = _replace_text_spans(
                        text,
                        [
                            (trigger["start"], trigger["end"], ""),
                            (target["start"], target["end"], ""),
                        ],
                    )
                    deleted.append(object_id)
                else:
                    raise MapEditError(
                        f"direct non-lossy maps do not support operation '{op}'"
                    )
                _parse_raw_map(text.encode("utf-8"))
            except MapEditError as error:
                raise MapEditError(f"operation {index}: {error}") from error
        after = text.encode("utf-8")
        return self._apply_direct(
            name, path, before, after, expected_revision, dry_run,
            self._diff(
                before, after, summary,
                added=added, changed=changed, deleted=deleted,
            ),
        )

    def add_point_light(
        self, map_name: Any, object_id: Any, origin: Any, color: Any,
        intensity: Any, radius: Any, expected_revision: Any,
        dry_run: bool = False, **settings: Any,
    ) -> dict[str, Any]:
        operation = {
            "op": "add_point_light", "id": object_id, "origin": origin,
            "color": color, "intensity": intensity, "radius": radius,
            **settings,
        }
        return self._apply_lighting_operations(
            map_name, [operation], expected_revision, dry_run,
            f"add point light '{object_id}'",
        )

    def update_point_light(
        self, map_name: Any, object_id: Any, expected_revision: Any,
        dry_run: bool = False, **settings: Any,
    ) -> dict[str, Any]:
        return self._apply_lighting_operations(
            map_name,
            [{"op": "update_point_light", "id": object_id, **settings}],
            expected_revision, dry_run, f"update point light '{object_id}'",
        )

    def remove_point_light(
        self, map_name: Any, object_id: Any, expected_revision: Any,
        dry_run: bool = False,
    ) -> dict[str, Any]:
        return self._apply_lighting_operations(
            map_name, [{"op": "remove_point_light", "id": object_id}],
            expected_revision, dry_run, f"remove point light '{object_id}'",
        )

    def list_point_lights(self, map_name: Any) -> dict[str, Any]:
        inspected = self.inspect(map_name)
        return {
            "map": inspected["map"],
            "revision": inspected["revision"],
            "point_lights": inspected["point_lights"],
            "count": len(inspected["point_lights"]),
        }

    def add_teleport(
        self, map_name: Any, object_id: Any, minimum: Any, maximum: Any,
        destination: Any, exit_yaw: Any, expected_revision: Any,
        dry_run: bool = False,
    ) -> dict[str, Any]:
        return self._apply_lighting_operations(
            map_name,
            [{
                "op": "add_teleport", "id": object_id,
                "min": minimum, "max": maximum,
                "destination": destination, "exit_yaw": exit_yaw,
            }],
            expected_revision, dry_run, f"add teleport '{object_id}'",
        )

    def update_teleport(
        self, map_name: Any, object_id: Any, expected_revision: Any,
        dry_run: bool = False, **settings: Any,
    ) -> dict[str, Any]:
        return self._apply_lighting_operations(
            map_name,
            [{"op": "update_teleport", "id": object_id, **settings}],
            expected_revision, dry_run, f"update teleport '{object_id}'",
        )

    def remove_teleport(
        self, map_name: Any, object_id: Any, expected_revision: Any,
        dry_run: bool = False,
    ) -> dict[str, Any]:
        return self._apply_lighting_operations(
            map_name, [{"op": "remove_teleport", "id": object_id}],
            expected_revision, dry_run, f"remove teleport '{object_id}'",
        )

    def list_teleports(self, map_name: Any) -> dict[str, Any]:
        inspected = self.inspect(map_name)
        return {
            "map": inspected["map"],
            "revision": inspected["revision"],
            "teleports": inspected["teleports"],
            "count": len(inspected["teleports"]),
        }

    def set_world_lighting(
        self, map_name: Any, expected_revision: Any,
        dry_run: bool = False, **settings: Any,
    ) -> dict[str, Any]:
        return self._apply_lighting_operations(
            map_name, [{"op": "set_world_lighting", **settings}],
            expected_revision, dry_run, "set world lighting",
        )

    def get_world_lighting(self, map_name: Any) -> dict[str, Any]:
        inspected = self.inspect(map_name)
        return {
            "map": inspected["map"],
            "revision": inspected["revision"],
            "world_lighting": inspected["world_lighting"],
        }

    def apply_batch(
        self, map_name: Any, operations: Any, expected_revision: Any,
        dry_run: bool = False,
    ) -> dict[str, Any]:
        return self._apply_lighting_operations(
            map_name, operations, expected_revision, dry_run,
            f"apply {len(operations) if isinstance(operations, list) else 0} map operations",
        )

    def rollback(
        self, token: Any, expected_revision: Any, dry_run: bool = False
    ) -> dict[str, Any]:
        if not isinstance(token, str) or not ROLLBACK_RE.fullmatch(token):
            raise MapEditError("rollback_token must contain 32 lower-case hex characters")
        record_path = self.transaction_root / f"{token}.json"
        try:
            record = json.loads(record_path.read_text(encoding="utf-8"))
        except (FileNotFoundError, json.JSONDecodeError) as error:
            raise MapEditError("rollback token does not exist or is invalid") from error
        name, path = self._map_path(record.get("map"))
        expected = _validate_revision(expected_revision)
        with self._map_lock(name):
            try:
                current = path.read_bytes()
            except FileNotFoundError as error:
                raise MapEditError(f"map '{name}' no longer exists") from error
            current_revision = _sha256(current)
            if current_revision != expected or current_revision != record.get("after_revision"):
                raise MapEditError(
                    "rollback rejected because the map no longer matches the transaction result"
                )
            before = (
                base64.b64decode(record["before"]) if record.get("before_exists") else None
            )
            if before is not None:
                self._validate_editable_data(before)
                diff = self._diff(current, before, f"rollback transaction {token}")
                reverse_token = None
                if not dry_run:
                    reverse_token = self._record_transaction(name, current, before)
                    try:
                        self._atomic_write(path, before)
                    except Exception:
                        self._discard_transaction(reverse_token)
                        raise
                revision_after = _sha256(before)
            else:
                diff = self._diff(current, b"", f"rollback map creation {token}")
                reverse_token = None
                if not dry_run:
                    path.unlink()
                revision_after = None
        return {
            "map": name,
            "applied": not dry_run,
            "dry_run": dry_run,
            "revision_before": current_revision,
            "revision_after": revision_after,
            "rollback_token": reverse_token,
            "diff": diff,
        }

    def _apply_direct(
        self, name: str, path: Path, before: bytes, after: bytes,
        expected_revision: Any, dry_run: bool, diff: dict[str, Any],
    ) -> dict[str, Any]:
        expected = _validate_revision(expected_revision)
        raw_after = _parse_raw_map(after)
        validation = self._validate_snapshot(
            name, after,
            {"cuboids": [], "raw_materials": _raw_materials(raw_after)},
        )
        if validation.get("available") and not validation.get("ok"):
            raise MapEditError(
                "patched map failed runtime validation: "
                f"{validation.get('error') or validation.get('stderr') or validation.get('stdout')}"
            )
        after_revision = _sha256(after)
        token = None
        with self._map_lock(name):
            current = path.read_bytes()
            before_revision = _sha256(current)
            if expected != before_revision or current != before:
                raise MapEditError(
                    f"stale map revision: expected {expected}, current revision is "
                    f"{before_revision}"
                )
            if not dry_run and after != before:
                token = self._record_transaction(name, before, after)
                try:
                    self._atomic_write(path, after)
                except Exception:
                    self._discard_transaction(token)
                    raise
        return {
            "map": name,
            "applied": not dry_run and after != before,
            "dry_run": dry_run,
            "editing_mode": "direct_non_lossy",
            "revision_before": before_revision,
            "revision_after": after_revision,
            "rollback_token": token,
            "diff": diff,
        }

    def validator_path(self) -> Path | None:
        candidates = (
            self.repo_root / "build" / "default" / "lg_duel_map_validate.exe",
            self.repo_root / "build" / "default" / "lg_duel_map_validate",
            self.repo_root / "build" / "default" / "Debug" / "lg_duel_map_validate.exe",
            self.repo_root / "build" / "default" / "Release" / "lg_duel_map_validate.exe",
        )
        return next((path for path in candidates if path.is_file()), None)

    def _validate_snapshot(
        self, name: str, data: bytes, state: dict[str, Any]
    ) -> dict[str, Any]:
        executable = self.validator_path()
        if executable is None:
            return {
                "ok": False,
                "available": False,
                "map": name,
                "source_revision": _sha256(data),
                "structural": {"ok": True, "cuboids": len(state["cuboids"])},
                "error": "lg_duel_map_validate is not built in build/default",
            }
        validation_root = self.maps_root / ".lg-map-api" / "validation"
        validation_root.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=validation_root) as raw_temporary:
            root = Path(raw_temporary)
            map_path = root / "maps" / f"{name}.map"
            self._atomic_write(map_path, data)
            textures_root = (self.repo_root / "textures").resolve()
            for cuboid in state["cuboids"]:
                material = cuboid["material"]
                if material.lower() in COLLISION_MATERIALS:
                    continue
                source = self._material_path(material)
                target = root / "textures" / source.relative_to(textures_root)
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(source, target)
            for material in state.get("raw_materials", []):
                normalized = material.replace("\\", "/")
                if normalized.lower().startswith("textures/"):
                    normalized = normalized[len("textures/"):]
                if normalized.lower() in {
                    "common/clip", "common/playerclip", "common/weapclip",
                    "common/trigger",
                }:
                    continue
                source = self._material_path(normalized)
                target = root / "textures" / source.relative_to(textures_root)
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(source, target)
            try:
                completed = subprocess.run(
                    [str(executable), str(map_path)],
                    cwd=self.repo_root,
                    text=True,
                    capture_output=True,
                    timeout=30,
                    check=False,
                )
            except (OSError, subprocess.TimeoutExpired) as error:
                return {
                    "ok": False,
                    "available": True,
                    "map": name,
                    "source_revision": _sha256(data),
                    "structural": {"ok": True, "cuboids": len(state["cuboids"])},
                    "error": f"validator could not run: {error}",
                }
        return {
            "ok": completed.returncode == 0,
            "available": True,
            "map": name,
            "source_revision": _sha256(data),
            "structural": {"ok": True, "cuboids": len(state["cuboids"])},
            "exit_code": completed.returncode,
            "stdout": completed.stdout.strip(),
            "stderr": completed.stderr.strip(),
        }

    def validate(self, map_name: Any) -> dict[str, Any]:
        name, _path = self._map_path(map_name)
        with self._map_lock(name):
            _name, _path, data, editable = self._read_editable(name)
            state = editable.get("state")
            if state is None:
                state = {
                    "cuboids": [],
                    "raw_materials": _raw_materials(editable["raw"]),
                }
            return self._validate_snapshot(name, data, state)

    def _sync_snapshot(
        self, name: str, source_path: Path, data: bytes, expected_revision: Any
    ) -> dict[str, Any]:
        expected = _validate_revision(expected_revision)
        source_revision = _sha256(data)
        if source_revision != expected:
            raise MapEditError(
                f"stale map revision: expected {expected}, current revision is {source_revision}"
            )
        destination = (self.runtime_maps_root / f"{name}.map").resolve()
        if destination.parent != self.runtime_maps_root:
            raise MapEditError("runtime map path escaped the fixed runtime maps directory")
        if source_path.read_bytes() != data:
            raise MapEditError("source map changed during sync; retry with its new revision")
        self._atomic_write(destination, data)
        if source_path.read_bytes() != data:
            raise MapEditError(
                "source map changed during sync; runtime copy may hold the prior requested revision"
            )
        runtime_revision = _sha256(destination.read_bytes())
        if runtime_revision != source_revision:
            raise MapEditError("runtime map hash does not match the source after sync")
        return {
            "map": name,
            "source_revision": source_revision,
            "runtime_revision": runtime_revision,
            "runtime_relative_path": f"build/default/maps/{name}.map",
        }

    def sync_runtime(self, map_name: Any, expected_revision: Any) -> dict[str, Any]:
        name, _path = self._map_path(map_name)
        with self._map_lock(name):
            _name, source_path, data, _state = self._read_editable(name)
            return self._sync_snapshot(name, source_path, data, expected_revision)

    def validate_sync_reload(
        self,
        map_name: Any,
        expected_revision: Any,
        ensure_runtime: Callable[[], Any],
        status: Callable[[], dict[str, Any]],
        load: Callable[[str], dict[str, Any]],
        reload_current: Callable[[], dict[str, Any]],
    ) -> dict[str, Any]:
        name = _validate_map_name(map_name)
        with self._map_lock(name):
            _name, source_path, data, editable = self._read_editable(name)
            state = editable.get("state")
            if state is None:
                state = {
                    "cuboids": [],
                    "raw_materials": _raw_materials(editable["raw"]),
                }
            expected = _validate_revision(expected_revision)
            if _sha256(data) != expected:
                raise MapEditError(
                    f"stale map revision: expected {expected}, current revision is {_sha256(data)}"
                )
            validation = self._validate_snapshot(name, data, state)
            if (
                not validation["ok"]
                or validation.get("source_revision") != expected
            ):
                raise MapEditError(
                    "map validation failed: "
                    f"{validation.get('error') or validation.get('stderr') or validation.get('stdout')}"
                )
            sync = self._sync_snapshot(name, source_path, data, expected)
            with self._runtime_lock():
                ensure_runtime()
                before = status()
                runtime_path = self.runtime_maps_root / f"{name}.map"
                if _sha256(runtime_path.read_bytes()) != expected:
                    raise MapEditError("runtime map changed before the load request")
                loaded = reload_current() if before.get("map") == name else load(name)
                if _sha256(runtime_path.read_bytes()) != expected:
                    raise MapEditError("runtime map changed during the load request")
                if source_path.read_bytes() != data:
                    raise MapEditError("source map changed during the load request")
                authoritative_revision = loaded.get("map_revision")
                if not isinstance(authoritative_revision, int):
                    raise MapEditError("runtime load returned no authoritative map revision")
                final_status = status()
                if (
                    final_status.get("map") != name
                    or final_status.get("map_revision") != authoritative_revision
                ):
                    raise MapEditError(
                        "active runtime map changed before reload verification completed"
                    )
            return {
                "map": name,
                "validation": validation,
                "source_revision": sync["source_revision"],
                "runtime_revision": sync["runtime_revision"],
                "loaded": {
                    "operation": "reload" if before.get("map") == name else "load",
                    "map_revision": authoritative_revision,
                    "previous_map_revision": loaded.get("previous_map_revision"),
                },
            }
