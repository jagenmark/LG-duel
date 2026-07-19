#!/usr/bin/env python3
"""Inventory a q3map2 classic .map decompile and emit an LG map candidate.

The default remains conservative and never tessellates patches or imports
shaders. A checked-in adaptation file may deliberately select spawns, map
source material roles to original LG textures, reconstruct named patch indices,
and enable clean authored teleport routes. Reports keep those choices explicit.
"""

from __future__ import annotations

import argparse
import collections
import dataclasses
import fnmatch
import hashlib
import itertools
import json
import math
import os
import pathlib
import struct
import sys
import tempfile
from typing import Iterable, Sequence


PLACEHOLDER_MATERIALS = (
    "Tiny3/Brick/Brick_09-128x128",
    "Tiny3/Metal/Metal_04-128x128",
    "Tiny3/Stone/Stone_14-128x128",
)
Q3MAP2_TOOLCHAIN = {
    "distribution": "NetRadiant-custom",
    "release": "20260114",
    "version": "2.5.17n-git-68ecbed",
    "archive_sha256": "25c2e14e2b0bd7a9897b2f943c8821458873c8713973f9c3d68d49f26fe79e35",
    "setup_script": "scripts/setup-q3map2.ps1",
}
LIMITS = {
    "walls": 2048,
    "convex_brushes": 1024,
    "lights": 96,
    "jump_pads": 48,
    "health_pickups": 32,
    "spawns": 32,
    "teleports": 16,
}
BSP_LUMPS = (
    ("entities", 1),
    ("textures", 72),
    ("planes", 16),
    ("nodes", 36),
    ("leafs", 48),
    ("leaf_faces", 4),
    ("leaf_brushes", 4),
    ("models", 40),
    ("brushes", 12),
    ("brush_sides", 8),
    ("vertices", 44),
    ("mesh_vertices", 4),
    ("effects", 72),
    ("faces", 104),
    ("lightmaps", 128 * 128 * 3),
    ("light_volumes", 8),
    ("visibility", None),
)
NON_SOLID_MATERIALS = {
    "common/trigger",
    "common/origin",
    "common/hint",
    "common/skip",
    "common/areaportal",
    "common/clusterportal",
    "common/portal",
    "common/visportal",
    "common/lightgrid",
    "common/nodraw",
    "common/nodrop",
}

# Fog shaders describe atmospheric volumes, not visible or collidable brush
# shells. They are omitted before adaptation roles can assign a render material.
FOG_VOLUME_MATERIALS = {
    "sfx/hellfog",
}

COLLISION_ONLY_MATERIALS = {
    "common/clip",
    "common/playerclip",
    "common/weapclip",
}

BRUSH_CLASSIFICATIONS = {"visible_solid", "playerclip", "weapclip"}


class ConversionError(ValueError):
    pass


@dataclasses.dataclass(frozen=True)
class Token:
    kind: str
    value: str
    line: int


@dataclasses.dataclass(frozen=True)
class Face:
    points: tuple[tuple[float, float, float], ...]
    material: str
    line: int


@dataclasses.dataclass
class Brush:
    faces: list[Face]
    line: int
    parse_error: str | None = None


@dataclasses.dataclass(frozen=True)
class Patch:
    kind: str
    material: str | None
    line: int
    width: int = 0
    height: int = 0
    control_points: tuple[tuple[tuple[float, float, float], ...], ...] = ()


@dataclasses.dataclass(frozen=True)
class UnsupportedConstruct:
    kind: str
    line: int
    reason: str


@dataclasses.dataclass
class Entity:
    properties: list[tuple[str, str]]
    brushes: list[Brush]
    patches: list[Patch]
    unsupported: list[UnsupportedConstruct]
    line: int

    def get(self, key: str) -> str | None:
        for candidate, value in self.properties:
            if candidate == key:
                return value
        return None


@dataclasses.dataclass(frozen=True)
class GeometryResult:
    valid: bool
    kind: str | None
    bounds: tuple[tuple[float, float, float], tuple[float, float, float]] | None
    reason: str | None
    degenerate_faces: int = 0


def _normalize_material(material: str) -> str:
    value = material.replace("\\", "/").lstrip("/").lower()
    return value.removeprefix("textures/")


def _lex(text: str) -> list[Token]:
    tokens: list[Token] = []
    offset = 0
    line = 1
    length = len(text)
    punctuation = {"{": "LBRACE", "}": "RBRACE", "(": "LPAREN", ")": "RPAREN"}
    while offset < length:
        char = text[offset]
        if char in " \t\r":
            offset += 1
            continue
        if char == "\n":
            tokens.append(Token("NEWLINE", "\n", line))
            line += 1
            offset += 1
            continue
        if char == "/" and offset + 1 < length and text[offset + 1] == "/":
            offset += 2
            while offset < length and text[offset] != "\n":
                offset += 1
            continue
        if char == '"':
            token_line = line
            offset += 1
            value: list[str] = []
            while offset < length and text[offset] != '"':
                if text[offset] == "\n":
                    raise ConversionError(f"line {token_line}: newline in quoted string")
                if text[offset] == "\\" and offset + 1 < length:
                    offset += 1
                value.append(text[offset])
                offset += 1
            if offset >= length:
                raise ConversionError(f"line {token_line}: unterminated quoted string")
            offset += 1
            tokens.append(Token("STRING", "".join(value), token_line))
            continue
        if char in punctuation:
            tokens.append(Token(punctuation[char], char, line))
            offset += 1
            continue
        start = offset
        while (
            offset < length
            and not text[offset].isspace()
            and text[offset] not in '{}()"'
            and not (text[offset] == "/" and offset + 1 < length and text[offset + 1] == "/")
        ):
            offset += 1
        if start == offset:
            raise ConversionError(f"line {line}: cannot tokenize {text[offset]!r}")
        tokens.append(Token("WORD", text[start:offset], line))
    return tokens


def _strip_newlines(tokens: Sequence[Token]) -> list[Token]:
    return [token for token in tokens if token.kind != "NEWLINE"]


def _parse_float(token: Token) -> float:
    try:
        value = float(token.value)
    except ValueError as error:
        raise ConversionError(f"line {token.line}: expected finite number, got {token.value!r}") from error
    if not math.isfinite(value):
        raise ConversionError(f"line {token.line}: non-finite number")
    return value


def _parse_face_line(tokens: Sequence[Token]) -> Face:
    flat = _strip_newlines(tokens)
    index = 0
    points: list[tuple[float, float, float]] = []
    for _ in range(3):
        if index >= len(flat) or flat[index].kind != "LPAREN":
            raise ConversionError(f"line {flat[0].line if flat else 0}: expected face point")
        if index + 4 >= len(flat) or flat[index + 4].kind != "RPAREN":
            raise ConversionError(f"line {flat[index].line}: malformed face point")
        points.append(tuple(_parse_float(flat[index + axis]) for axis in (1, 2, 3)))
        index += 5
    if index >= len(flat) or flat[index].kind not in {"WORD", "STRING"}:
        raise ConversionError(f"line {flat[0].line}: face is missing material")
    material = flat[index].value
    # Valve 220 axes and brush primitives are not accepted by LG's map parser.
    if any(token.value in {"[", "]"} for token in flat[index + 1 :]):
        raise ConversionError(f"line {flat[0].line}: Valve 220 texture axes are unsupported")
    return Face(tuple(points), material, flat[0].line)


def _parse_classic_brush(tokens: Sequence[Token], line: int) -> Brush:
    faces: list[Face] = []
    current: list[Token] = []
    try:
        for token in list(tokens) + [Token("NEWLINE", "\n", line)]:
            if token.kind == "NEWLINE":
                if current:
                    faces.append(_parse_face_line(current))
                    current = []
            else:
                current.append(token)
        if not faces:
            raise ConversionError(f"line {line}: empty classic brush")
        return Brush(faces, line)
    except ConversionError as error:
        return Brush(faces, line, str(error))


def _parse_patch(tokens: Sequence[Token], line: int) -> Patch:
    flat = _strip_newlines(tokens)
    kind = flat[0].value
    material = None
    material_index = -1
    for index, token in enumerate(flat[1:], start=1):
        if token.kind == "LBRACE" and index + 1 < len(flat):
            candidate = flat[index + 1]
            if candidate.kind in {"WORD", "STRING"}:
                material = candidate.value
                material_index = index + 1
            break
    numeric_groups: list[tuple[float, ...]] = []
    index = max(0, material_index + 1)
    while index + 6 < len(flat):
        if (
            flat[index].kind == "LPAREN" and
            all(flat[index + offset].kind == "WORD" for offset in range(1, 6)) and
            flat[index + 6].kind == "RPAREN"
        ):
            try:
                numeric_groups.append(tuple(_parse_float(flat[index + offset]) for offset in range(1, 6)))
            except ConversionError:
                pass
            index += 7
            continue
        index += 1
    if not numeric_groups:
        return Patch(kind, material, line)
    width = int(numeric_groups[0][0])
    height = int(numeric_groups[0][1])
    controls = numeric_groups[1:]
    if width < 3 or height < 3 or width % 2 == 0 or height % 2 == 0 or len(controls) != width * height:
        return Patch(kind, material, line, width, height)
    rows = tuple(
        tuple(tuple(point[axis] for axis in range(3)) for point in controls[row * height:(row + 1) * height])
        for row in range(width)
    )
    return Patch(kind, material, line, width, height, rows)


def _classify_construct(tokens: Sequence[Token], line: int) -> tuple[Brush | None, Patch | None, UnsupportedConstruct | None]:
    flat = _strip_newlines(tokens)
    if not flat:
        return None, None, UnsupportedConstruct("empty", line, "empty nested block")
    if flat[0].kind == "WORD" and flat[0].value in {"patchDef2", "patchDef3"}:
        return None, _parse_patch(tokens, line), None
    if flat[0].kind == "LPAREN":
        return _parse_classic_brush(tokens, line), None, None
    kind = flat[0].value
    return None, None, UnsupportedConstruct(kind, line, "unsupported nested map construct")


def parse_map(text: str) -> list[Entity]:
    tokens = _lex(text)
    index = 0

    def skip_newlines(position: int) -> int:
        while position < len(tokens) and tokens[position].kind == "NEWLINE":
            position += 1
        return position

    entities: list[Entity] = []
    index = skip_newlines(index)
    while index < len(tokens):
        if tokens[index].kind != "LBRACE":
            raise ConversionError(f"line {tokens[index].line}: expected entity '{{'")
        entity_line = tokens[index].line
        index += 1
        properties: list[tuple[str, str]] = []
        brushes: list[Brush] = []
        patches: list[Patch] = []
        unsupported: list[UnsupportedConstruct] = []
        while True:
            index = skip_newlines(index)
            if index >= len(tokens):
                raise ConversionError(f"line {entity_line}: unterminated entity")
            token = tokens[index]
            if token.kind == "RBRACE":
                index += 1
                break
            if token.kind == "STRING":
                if index + 1 >= len(tokens) or tokens[index + 1].kind != "STRING":
                    raise ConversionError(f"line {token.line}: property is missing quoted value")
                properties.append((token.value, tokens[index + 1].value))
                index += 2
                continue
            if token.kind != "LBRACE":
                raise ConversionError(f"line {token.line}: expected property or nested block")
            block_line = token.line
            index += 1
            depth = 1
            block: list[Token] = []
            while index < len(tokens) and depth:
                current = tokens[index]
                index += 1
                if current.kind == "LBRACE":
                    depth += 1
                elif current.kind == "RBRACE":
                    depth -= 1
                    if depth == 0:
                        break
                block.append(current)
            if depth:
                raise ConversionError(f"line {block_line}: unterminated nested block")
            brush, patch, unknown = _classify_construct(block, block_line)
            if brush:
                brushes.append(brush)
            if patch:
                patches.append(patch)
            if unknown:
                unsupported.append(unknown)
        entities.append(Entity(properties, brushes, patches, unsupported, entity_line))
        index = skip_newlines(index)
    return entities


def _sub(a: Sequence[float], b: Sequence[float]) -> tuple[float, float, float]:
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def _cross(a: Sequence[float], b: Sequence[float]) -> tuple[float, float, float]:
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def _dot(a: Sequence[float], b: Sequence[float]) -> float:
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def _length(a: Sequence[float]) -> float:
    return math.sqrt(_dot(a, a))


def _plane_intersection(a: Sequence[float], b: Sequence[float], c: Sequence[float]) -> tuple[float, float, float] | None:
    ac = _cross(b[:3], c[:3])
    denominator = _dot(a[:3], ac)
    if abs(denominator) <= 1e-8:
        return None
    ca = _cross(c[:3], a[:3])
    ab = _cross(a[:3], b[:3])
    point = tuple((ac[i] * a[3] + ca[i] * b[3] + ab[i] * c[3]) / denominator for i in range(3))
    return point if all(math.isfinite(value) for value in point) else None


def _is_cuboid(faces: Sequence[Face]) -> bool:
    if len(faces) != 6:
        return False
    coordinates: list[list[float]] = [[], [], []]
    for face in faces:
        first, second, third = face.points
        normal = _cross(_sub(second, first), _sub(third, first))
        if _length(normal) <= 1e-6:
            return False
        normal_axis = max(range(3), key=lambda axis: abs(normal[axis]))
        if any(abs(normal[axis]) > 1e-5 * _length(normal) for axis in range(3) if axis != normal_axis):
            return False
        plane_axes = [axis for axis in range(3) if max(point[axis] for point in face.points) - min(point[axis] for point in face.points) <= 1e-5]
        if plane_axes != [normal_axis]:
            return False
        coordinate = first[normal_axis]
        if any(abs(existing - coordinate) <= 1e-4 for existing in coordinates[normal_axis]):
            return False
        coordinates[normal_axis].append(coordinate)
    return all(len(axis) == 2 and min(axis) < max(axis) for axis in coordinates)


def validate_brush(brush: Brush) -> GeometryResult:
    if brush.parse_error:
        return GeometryResult(False, None, None, brush.parse_error)
    degenerate = 0
    if len(brush.faces) < 4 or len(brush.faces) > 16:
        return GeometryResult(False, None, None, f"unsupported face count {len(brush.faces)}")
    supplied = [point for face in brush.faces for point in face.points]
    centroid = tuple(sum(point[axis] for point in supplied) / len(supplied) for axis in range(3))
    planes: list[tuple[float, float, float, float]] = []
    for face in brush.faces:
        first, second, third = face.points
        normal = _cross(_sub(second, first), _sub(third, first))
        magnitude = _length(normal)
        if magnitude <= 1e-6:
            degenerate += 1
            continue
        normal = tuple(value / magnitude for value in normal)
        distance = _dot(normal, first)
        # The source winding is not trusted; orient each plane so the aggregate
        # of authored plane points stays in the convex interior half-space.
        if _dot(normal, centroid) > distance:
            normal = tuple(-value for value in normal)
            distance = -distance
        planes.append((*normal, distance))
    if degenerate:
        return GeometryResult(False, None, None, f"{degenerate} degenerate face(s)", degenerate)
    vertices: list[tuple[float, float, float]] = []
    for first, second, third in itertools.combinations(planes, 3):
        point = _plane_intersection(first, second, third)
        if point is None or any(_dot(plane[:3], point) > plane[3] + 1e-3 for plane in planes):
            continue
        if not any(_length(_sub(point, existing)) <= 5e-4 for existing in vertices):
            vertices.append(point)
    if len(vertices) < 4:
        return GeometryResult(False, None, None, "convex brush has no closed volume")
    if len(vertices) > 32:
        return GeometryResult(False, None, None, f"convex brush has {len(vertices)} vertices (maximum 32)")
    for plane in planes:
        on_face = sum(abs(_dot(plane[:3], vertex) - plane[3]) <= 2e-3 for vertex in vertices)
        if on_face < 3 or on_face > 12:
            return GeometryResult(False, None, None, f"convex face resolves to {on_face} vertices")
    minimum = tuple(min(point[axis] for point in vertices) for axis in range(3))
    maximum = tuple(max(point[axis] for point in vertices) for axis in range(3))
    if any(maximum[axis] - minimum[axis] <= 1e-4 for axis in range(3)):
        return GeometryResult(False, None, None, "degenerate brush bounds")
    return GeometryResult(True, "wall" if _is_cuboid(brush.faces) else "convex_brush", (minimum, maximum), None)


def _sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def read_bsp_metadata(path: pathlib.Path) -> dict[str, object]:
    data = path.read_bytes()
    header_size = 8 + len(BSP_LUMPS) * 8
    if len(data) < header_size:
        raise ConversionError(f"BSP {path.name} is shorter than its {header_size}-byte header")
    magic, version = struct.unpack_from("<4si", data)
    # q3map2 uses the same 17-lump header for base Q3 v46 and Quake Live v47.
    # Record the version verbatim; rejecting v47 would exclude the supplied map.
    if magic != b"IBSP" or version not in {46, 47}:
        raise ConversionError(f"BSP {path.name} must be Quake 3/Quake Live IBSP version 46 or 47")
    lumps: list[dict[str, object]] = []
    for index, (name, record_size) in enumerate(BSP_LUMPS):
        offset, length = struct.unpack_from("<ii", data, 8 + index * 8)
        if offset < 0 or length < 0 or offset + length > len(data):
            raise ConversionError(f"BSP lump {name} is outside the file")
        entry: dict[str, object] = {"index": index, "name": name, "offset": offset, "length": length}
        if record_size:
            entry["record_size"] = record_size
            entry["count"] = length // record_size
            entry["trailing_bytes"] = length % record_size
        lumps.append(entry)
    models = lumps[7]
    bounds = None
    if models["length"] >= 40:
        values = struct.unpack_from("<6f", data, int(models["offset"]))
        if all(math.isfinite(value) for value in values):
            bounds = {"min": list(values[:3]), "max": list(values[3:])}
    return {
        "name": path.name,
        "size": len(data),
        "sha256": _sha256(data),
        "magic": magic.decode("ascii"),
        "version": version,
        "world_model_bounds": bounds,
        "lumps": lumps,
    }


def read_aas_metadata(path: pathlib.Path) -> dict[str, object]:
    data = path.read_bytes()
    if len(data) < 12:
        raise ConversionError(f"AAS {path.name} is shorter than its 12-byte header")
    magic, version, bsp_checksum = struct.unpack_from("<4sii", data)
    if magic != b"EAAS" or version != 5:
        raise ConversionError(f"AAS {path.name} must use EAAS little-endian version 5")
    return {
        "name": path.name,
        "size": len(data),
        "sha256": _sha256(data),
        "magic": magic.decode("ascii"),
        "version": version,
        "bsp_checksum": bsp_checksum,
        "bsp_checksum_unsigned": bsp_checksum & 0xFFFFFFFF,
        "route_import_attempted": False,
        "note": "Metadata only; no AAS route import was attempted.",
    }


def _vector(value: str | None) -> tuple[float, float, float] | None:
    if value is None:
        return None
    parts = value.split()
    if len(parts) != 3:
        return None
    try:
        result = tuple(float(part) for part in parts)
    except ValueError:
        return None
    return result if all(math.isfinite(part) for part in result) else None


def _placeholder(material: str) -> str:
    normalized = _normalize_material(material)
    if "metal" in normalized or "rust" in normalized or "trim" in normalized:
        return PLACEHOLDER_MATERIALS[1]
    if "stone" in normalized or "rock" in normalized or "gothic" in normalized:
        return PLACEHOLDER_MATERIALS[2]
    return PLACEHOLDER_MATERIALS[0]


def _adapted_material(material: str, adaptation: dict[str, object] | None) -> str:
    if not adaptation:
        return _placeholder(material)
    normalized = _normalize_material(material)
    roles = adaptation.get("material_roles", {})
    materials = adaptation.get("materials", {})
    if not isinstance(roles, dict) or not isinstance(materials, dict):
        raise ConversionError("adaptation material_roles and materials must be objects")
    for selected in materials.values():
        if isinstance(selected, str) and _normalize_material(selected) == normalized:
            return selected
    for role, patterns in roles.items():
        if not isinstance(patterns, list) or not all(isinstance(pattern, str) for pattern in patterns):
            raise ConversionError(f"adaptation material role '{role}' must be a string array")
        if any(fnmatch.fnmatchcase(normalized, pattern.lower()) for pattern in patterns):
            selected = materials.get(role)
            if not isinstance(selected, str) or not selected:
                raise ConversionError(f"adaptation material role '{role}' has no material")
            return selected
    fallback = materials.get("wall")
    if not isinstance(fallback, str) or not fallback:
        raise ConversionError("adaptation materials.wall is required")
    return fallback


def _validate_adaptation_binding(
    adaptation: dict[str, object] | None,
    raw_sha256: str,
    bsp: dict[str, object],
) -> None:
    if not adaptation or adaptation.get("schema_version") != 2:
        return
    expected_bsp = adaptation.get("source_bsp_sha256")
    expected_raw = adaptation.get("raw_decompile_sha256")
    actual_bsp = bsp.get("sha256")
    if not isinstance(expected_bsp, str) or len(expected_bsp) != 64 or any(char not in "0123456789abcdefABCDEF" for char in expected_bsp):
        raise ConversionError("adaptation source_bsp_sha256 must be a SHA-256 hex string")
    if not isinstance(expected_raw, str) or len(expected_raw) != 64 or any(char not in "0123456789abcdefABCDEF" for char in expected_raw):
        raise ConversionError("adaptation raw_decompile_sha256 must be a SHA-256 hex string")
    if not isinstance(actual_bsp, str) or expected_bsp.lower() != actual_bsp.lower():
        raise ConversionError(
            f"adaptation source BSP SHA-256 mismatch: expected {expected_bsp.lower()}, got {actual_bsp}"
        )
    if expected_raw.lower() != raw_sha256:
        raise ConversionError(
            f"adaptation raw decompile SHA-256 mismatch: expected {expected_raw.lower()}, got {raw_sha256}"
        )


def _automatic_brush_classification(brush: Brush, result: GeometryResult) -> tuple[str | None, str]:
    normalized = {_normalize_material(face.material) for face in brush.faces}
    if not result.valid:
        return None, "invalid_geometry"
    if normalized and normalized <= FOG_VOLUME_MATERIALS:
        return None, "fog_volume"
    if normalized and normalized <= NON_SOLID_MATERIALS:
        return None, "confident_non_solid_utility"
    collision = normalized & COLLISION_ONLY_MATERIALS
    if collision:
        if normalized <= {"common/clip", "common/playerclip"}:
            return "playerclip", "automatic_playerclip"
        if normalized == {"common/weapclip"}:
            return "weapclip", "automatic_weapclip"
        return None, "mixed_collision_materials"
    return "visible_solid", "automatic_visible_solid"


def _brush_policy_rules(
    adaptation: dict[str, object] | None,
    entities: Sequence[Entity],
) -> tuple[dict[tuple[int, int], dict[str, object]], dict[str, object]]:
    policy = adaptation.get("brush_policy", {}) if adaptation else {}
    if not isinstance(policy, dict):
        raise ConversionError("adaptation brush_policy must be an object")
    unknown_policy = set(policy) - {"default_action", "rules"}
    if unknown_policy:
        raise ConversionError(f"adaptation brush_policy has invalid fields: {sorted(unknown_policy)}")
    default_action = policy.get("default_action", "allow")
    if default_action != "allow":
        raise ConversionError("adaptation brush_policy.default_action must be 'allow'")
    raw_rules = policy.get("rules", [])
    if not isinstance(raw_rules, list):
        raise ConversionError("adaptation brush_policy.rules must be an array")
    configured_roles = adaptation.get("materials", {}) if adaptation else {}
    if not isinstance(configured_roles, dict):
        raise ConversionError("adaptation materials must be an object")
    rules: dict[tuple[int, int], dict[str, object]] = {}
    for index, raw_rule in enumerate(raw_rules):
        if not isinstance(raw_rule, dict):
            raise ConversionError(f"adaptation brush policy rule {index} must be an object")
        action = raw_rule.get("action")
        allowed_fields = {"source_entity_index", "source_brush_index", "action", "reason"}
        if action == "override":
            allowed_fields |= {"classification", "material_role"}
        unknown = set(raw_rule) - allowed_fields
        if unknown:
            raise ConversionError(f"adaptation brush policy rule {index} has invalid fields: {sorted(unknown)}")
        entity_index = raw_rule.get("source_entity_index")
        brush_index = raw_rule.get("source_brush_index")
        reason = raw_rule.get("reason")
        if not isinstance(entity_index, int) or isinstance(entity_index, bool) or entity_index < 0:
            raise ConversionError(f"adaptation brush policy rule {index} has invalid source_entity_index")
        if not isinstance(brush_index, int) or isinstance(brush_index, bool) or brush_index < 0:
            raise ConversionError(f"adaptation brush policy rule {index} has invalid source_brush_index")
        if action not in {"allow", "drop", "override"}:
            raise ConversionError(f"adaptation brush policy rule {index} has invalid action")
        if not isinstance(reason, str) or not reason.strip():
            raise ConversionError(f"adaptation brush policy rule {index} needs a nonempty reason")
        locator = (entity_index, brush_index)
        if locator in rules:
            raise ConversionError(f"adaptation brush policy has duplicate locator {locator}")
        if entity_index >= len(entities) or brush_index >= len(entities[entity_index].brushes):
            raise ConversionError(f"adaptation brush policy locator {locator} is outside the source inventory")
        if (entities[entity_index].get("classname") or "<missing>") not in {"worldspawn", "func_static"}:
            raise ConversionError(f"adaptation brush policy locator {locator} is not static geometry")
        if action == "override":
            classification = raw_rule.get("classification")
            role = raw_rule.get("material_role")
            if classification not in BRUSH_CLASSIFICATIONS:
                raise ConversionError(f"adaptation brush policy rule {index} has invalid classification")
            if classification == "visible_solid":
                configured_material = configured_roles.get(role) if isinstance(role, str) else None
                if role is not None and (
                    not isinstance(role, str) or not role or
                    not isinstance(configured_material, str) or not configured_material
                ):
                    raise ConversionError(f"adaptation brush policy rule {index} names an invalid material_role")
            elif role is not None:
                raise ConversionError(f"adaptation brush policy rule {index} material_role requires visible_solid")
        rules[locator] = dict(raw_rule)
    return rules, {"default_action": "allow", "rule_count": len(rules)}


def _quadratic(a: Sequence[float], b: Sequence[float], c: Sequence[float], t: float) -> tuple[float, float, float]:
    inverse = 1.0 - t
    return tuple(
        (inverse * inverse * a[axis]) + (2.0 * inverse * t * b[axis]) + (t * t * c[axis])
        for axis in range(3)
    )


def _patch_point(patch: Patch, cell_x: int, cell_y: int, u: float, v: float) -> tuple[float, float, float]:
    intermediate = [
        _quadratic(
            patch.control_points[(cell_x * 2) + offset][cell_y * 2],
            patch.control_points[(cell_x * 2) + offset][(cell_y * 2) + 1],
            patch.control_points[(cell_x * 2) + offset][(cell_y * 2) + 2],
            v,
        )
        for offset in range(3)
    ]
    return _quadratic(intermediate[0], intermediate[1], intermediate[2], u)


def _triangle_prism(
    first: tuple[float, float, float],
    second: tuple[float, float, float],
    third: tuple[float, float, float],
    material: str,
    line: int,
    thickness: float,
) -> Brush | None:
    normal = _cross(_sub(second, first), _sub(third, first))
    magnitude = _length(normal)
    if magnitude <= 1e-4:
        return None
    half = thickness * 0.5
    offset = tuple(normal[axis] * half / magnitude for axis in range(3))
    upper = [tuple(point[axis] + offset[axis] for axis in range(3)) for point in (first, second, third)]
    lower = [tuple(point[axis] - offset[axis] for axis in range(3)) for point in (first, second, third)]
    return Brush(
        [
            Face((upper[0], upper[1], upper[2]), material, line),
            Face((lower[0], lower[2], lower[1]), material, line),
            Face((lower[0], lower[1], upper[1]), material, line),
            Face((lower[1], lower[2], upper[2]), material, line),
            Face((lower[2], lower[0], upper[0]), material, line),
        ],
        line,
    )


def _reconstruct_patch(patch: Patch, subdivisions: int, thickness: float) -> list[Brush]:
    if not patch.control_points:
        return []
    brushes: list[Brush] = []
    for cell_x in range((patch.width - 1) // 2):
        for cell_y in range((patch.height - 1) // 2):
            samples = [index / subdivisions for index in range(subdivisions + 1)]
            points = {
                (x, y): _patch_point(patch, cell_x, cell_y, u, v)
                for x, u in enumerate(samples)
                for y, v in enumerate(samples)
            }
            for x in range(subdivisions):
                for y in range(subdivisions):
                    corners = (
                        points[(x, y)], points[(x + 1, y)],
                        points[(x + 1, y + 1)], points[(x, y + 1)],
                    )
                    for triangle in ((corners[0], corners[1], corners[2]), (corners[0], corners[2], corners[3])):
                        brush = _triangle_prism(*triangle, patch.material or "common/caulk", patch.line, thickness)
                        if brush is not None and validate_brush(brush).valid:
                            brushes.append(brush)
    return brushes


def _box_brush(
    minimum: tuple[float, float, float],
    maximum: tuple[float, float, float],
    material: str,
) -> Brush:
    x0, y0, z0 = minimum
    x1, y1, z1 = maximum
    return Brush([
        Face(((x0, y0, z0), (x0, y0, z1), (x0, y1, z1)), material, 0),
        Face(((x1, y0, z0), (x1, y1, z0), (x1, y1, z1)), material, 0),
        Face(((x0, y0, z0), (x1, y0, z0), (x1, y0, z1)), material, 0),
        Face(((x0, y1, z0), (x0, y1, z1), (x1, y1, z1)), material, 0),
        Face(((x0, y0, z0), (x0, y1, z0), (x1, y1, z0)), material, 0),
        Face(((x0, y0, z1), (x1, y0, z1), (x1, y1, z1)), material, 0),
    ], 0)


def _format_number(value: float) -> str:
    if value == 0:
        return "0"
    if value.is_integer():
        return str(int(value))
    return format(value, ".9g")


def _quote(value: str) -> str:
    return value.replace("\\", "\\\\").replace('"', '\\"')


def _emit_brush(
    brush: Brush,
    trigger: bool = False,
    adaptation: dict[str, object] | None = None,
    classification: str | None = None,
    material_role: str | None = None,
) -> list[str]:
    lines = ["{"]
    materials_by_role = adaptation.get("materials", {}) if adaptation else {}
    for face in brush.faces:
        points = " ".join("( " + " ".join(_format_number(value) for value in point) + " )" for point in face.points)
        material = (
            "common/trigger" if trigger else
            "common/playerclip" if classification == "playerclip" else
            "common/weapclip" if classification == "weapclip" else
            str(materials_by_role[material_role]) if material_role else
            _adapted_material(face.material, adaptation)
        )
        lines.append(f"{points} {material} 0 0 0 1 1")
    lines.append("}")
    return lines


def _emit_entity(
    properties: Sequence[tuple[str, str]],
    brushes: Sequence[Brush] = (),
    trigger: bool = False,
    adaptation: dict[str, object] | None = None,
    classification: str | None = None,
    material_role: str | None = None,
) -> list[str]:
    lines = ["{"]
    for key, value in properties:
        lines.append(f'"{_quote(key)}" "{_quote(value)}"')
    for index, brush in enumerate(brushes):
        lines.append(f"// brush {index}")
        lines.extend(_emit_brush(brush, trigger, adaptation, classification, material_role))
    lines.append("}")
    return lines


def _material_counter(entities: Sequence[Entity]) -> collections.Counter[str]:
    return collections.Counter(face.material for entity in entities for brush in entity.brushes for face in brush.faces)


def convert(
    raw_text: str,
    raw_name: str,
    bsp: dict[str, object],
    aas: dict[str, object] | None = None,
    adaptation: dict[str, object] | None = None,
) -> tuple[str, dict[str, object]]:
    raw_sha256 = _sha256(raw_text.encode("utf-8"))
    _validate_adaptation_binding(adaptation, raw_sha256, bsp)
    entities = parse_map(raw_text)
    classnames = collections.Counter(entity.get("classname") or "<missing>" for entity in entities)
    materials = _material_counter(entities)
    patch_materials = collections.Counter(patch.material or "<missing>" for entity in entities for patch in entity.patches)
    patch_kinds = collections.Counter(patch.kind for entity in entities for patch in entity.patches)
    construct_kinds = collections.Counter(item.kind for entity in entities for item in entity.unsupported)

    results: dict[int, GeometryResult] = {}
    geometry_issues: list[dict[str, object]] = []
    valid_bounds: list[tuple[tuple[float, float, float], tuple[float, float, float]]] = []
    for entity_index, entity in enumerate(entities):
        for brush_index, brush in enumerate(entity.brushes):
            result = validate_brush(brush)
            results[id(brush)] = result
            if result.valid and result.bounds:
                valid_bounds.append(result.bounds)
            else:
                geometry_issues.append({
                    "entity_index": entity_index,
                    "brush_index": brush_index,
                    "line": brush.line,
                    "classname": entity.get("classname") or "<missing>",
                    "reason": result.reason,
                    "degenerate_faces": result.degenerate_faces,
                })

    policy_rules, policy_summary = _brush_policy_rules(adaptation, entities)
    emitted_world: list[Brush] = []
    emitted_static: list[dict[str, object]] = []
    brush_rows: list[dict[str, object]] = []
    omitted_brushes = collections.Counter()
    for entity_index, entity in enumerate(entities):
        classname = entity.get("classname") or "<missing>"
        if classname not in {"worldspawn", "func_static"}:
            continue
        for brush_index, brush in enumerate(entity.brushes):
            result = results[id(brush)]
            automatic, automatic_reason = _automatic_brush_classification(brush, result)
            rule = policy_rules.get((entity_index, brush_index))
            requested_action = str(rule["action"]) if rule else "allow"
            effective = automatic
            omission_reason: str | None = None
            material_role: str | None = None
            # Policy cannot turn source content that failed the conservative gate into geometry.
            if automatic is None:
                omission_reason = automatic_reason
            elif requested_action == "drop":
                effective = None
                omission_reason = str(rule["reason"])
            elif requested_action == "override":
                effective = str(rule["classification"])
                material_role = str(rule["material_role"]) if rule and rule.get("material_role") is not None else None
            emitted = effective is not None
            if emitted:
                emitted_static.append({
                    "entity_index": entity_index,
                    "brush_index": brush_index,
                    "classname": classname,
                    "brush": brush,
                    "classification": effective,
                    "material_role": material_role,
                })
            else:
                omitted_brushes[automatic_reason if automatic is None else "policy_drop"] += 1
            brush_rows.append({
                "source_entity_index": entity_index,
                "source_brush_index": brush_index,
                "source_classname": classname,
                "source_line": brush.line,
                "source_materials": sorted({_normalize_material(face.material) for face in brush.faces}),
                "automatic_classification": automatic,
                "automatic_omission_reason": automatic_reason if automatic is None else None,
                "requested_action": requested_action,
                "requested_reason": str(rule["reason"]) if rule else None,
                "effective_classification": effective,
                "material_role": material_role,
                "emitted": emitted,
                "omission_reason": omission_reason,
            })

    reconstructed_patch_count = 0
    reconstructed_patch_brushes = 0
    adaptation_marker_count = 0
    selected_patch_indices: set[int] = set()
    if adaptation:
        raw_indices = adaptation.get("reconstruct_patch_indices", [])
        if not isinstance(raw_indices, list) or not all(isinstance(index, int) and index >= 0 for index in raw_indices):
            raise ConversionError("adaptation reconstruct_patch_indices must be a non-negative integer array")
        selected_patch_indices = set(raw_indices)
        subdivisions = adaptation.get("patch_subdivisions", 2)
        thickness = adaptation.get("patch_thickness", 2.0)
        if not isinstance(subdivisions, int) or subdivisions < 1 or subdivisions > 4:
            raise ConversionError("adaptation patch_subdivisions must be between 1 and 4")
        if not isinstance(thickness, (int, float)) or not 0.1 <= float(thickness) <= 8.0:
            raise ConversionError("adaptation patch_thickness must be between 0.1 and 8")
        patch_index = 0
        for entity in entities:
            for patch in entity.patches:
                if patch_index in selected_patch_indices:
                    reconstructed = _reconstruct_patch(patch, subdivisions, float(thickness))
                    if not reconstructed:
                        raise ConversionError(f"adaptation patch {patch_index} at line {patch.line} could not be reconstructed")
                    for brush in reconstructed:
                        result = validate_brush(brush)
                        results[id(brush)] = result
                        if result.bounds:
                            valid_bounds.append(result.bounds)
                    emitted_world.extend(reconstructed)
                    reconstructed_patch_count += 1
                    reconstructed_patch_brushes += len(reconstructed)
                patch_index += 1
        missing_indices = selected_patch_indices - set(range(patch_index))
        if missing_indices:
            raise ConversionError(f"adaptation patch indices are outside the source inventory: {sorted(missing_indices)}")

        raw_markers = adaptation.get("marker_boxes", [])
        materials_by_role = adaptation.get("materials", {})
        if not isinstance(raw_markers, list) or not isinstance(materials_by_role, dict):
            raise ConversionError("adaptation marker_boxes must be an array")
        for index, marker in enumerate(raw_markers):
            if not isinstance(marker, dict):
                raise ConversionError(f"adaptation marker box {index} must be an object")
            minimum = marker.get("min")
            maximum = marker.get("max")
            role = marker.get("material")
            if not isinstance(minimum, list) or len(minimum) != 3 or not all(isinstance(value, (int, float)) for value in minimum):
                raise ConversionError(f"adaptation marker box {index}.min must be a three-number array")
            if not isinstance(maximum, list) or len(maximum) != 3 or not all(isinstance(value, (int, float)) for value in maximum):
                raise ConversionError(f"adaptation marker box {index}.max must be a three-number array")
            if not isinstance(role, str) or not isinstance(materials_by_role.get(role), str):
                raise ConversionError(f"adaptation marker box {index}.material must name a configured material role")
            minimum_tuple = tuple(float(value) for value in minimum)
            maximum_tuple = tuple(float(value) for value in maximum)
            if not all(minimum_tuple[axis] < maximum_tuple[axis] for axis in range(3)):
                raise ConversionError(f"adaptation marker box {index} has inverted or degenerate bounds")
            marker_brush = _box_brush(minimum_tuple, maximum_tuple, str(materials_by_role[role]))
            result = validate_brush(marker_brush)
            if not result.valid:
                raise ConversionError(f"adaptation marker box {index} is invalid: {result.reason}")
            results[id(marker_brush)] = result
            if result.bounds:
                valid_bounds.append(result.bounds)
            emitted_world.append(marker_brush)
            adaptation_marker_count += 1

    mapped_entities: list[tuple[list[tuple[str, str]], list[Brush], bool]] = []
    converted_counts = collections.Counter()
    omitted_entities = collections.Counter()
    targets_by_name: dict[str, list[Entity]] = collections.defaultdict(list)
    for entity in entities:
        target_name = entity.get("targetname")
        if entity.get("classname") in {"target_position", "misc_teleporter_dest"} and target_name:
            targets_by_name[target_name].append(entity)
    target_by_name = {
        target_name: matching[0]
        for target_name, matching in targets_by_name.items()
        if len(matching) == 1
    }
    ambiguous_targetnames = {
        target_name: len(matching)
        for target_name, matching in sorted(targets_by_name.items())
        if len(matching) > 1
    }
    used_targets: set[str] = set()

    selected_spawn_origins: list[tuple[float, float, float]] | None = None
    if adaptation and "selected_spawn_origins" in adaptation:
        raw_spawns = adaptation["selected_spawn_origins"]
        if not isinstance(raw_spawns, list) or not 2 <= len(raw_spawns) <= LIMITS["spawns"]:
            raise ConversionError(
                "adaptation selected_spawn_origins must contain between 2 and "
                f"{LIMITS['spawns']} origins"
            )
        selected_spawn_origins = []
        for raw_spawn in raw_spawns:
            if not isinstance(raw_spawn, list) or len(raw_spawn) != 3 or not all(isinstance(value, (int, float)) for value in raw_spawn):
                raise ConversionError("adaptation selected_spawn_origins entries must be three-number arrays")
            selected_spawn_origins.append(tuple(float(value) for value in raw_spawn))
        if len(set(selected_spawn_origins)) != len(selected_spawn_origins):
            raise ConversionError("adaptation selected_spawn_origins must not contain duplicates")
        source_spawns = {
            _vector(entity.get("origin")): entity
            for entity in entities
            if entity.get("classname") in {"info_player_deathmatch", "info_player_start"} and _vector(entity.get("origin"))
        }
        for origin in selected_spawn_origins:
            entity = source_spawns.get(origin)
            if entity is None:
                raise ConversionError(f"adaptation selected spawn {origin} does not match a source spawn")
            properties = [("classname", "lg_spawn"), ("origin", entity.get("origin") or "")]
            if entity.get("angle") is not None:
                properties.append(("angle", entity.get("angle") or ""))
            mapped_entities.append((properties, [], False))
            converted_counts["spawns"] += 1
        unselected_spawn_count = classnames["info_player_deathmatch"] + classnames["info_player_start"] - len(selected_spawn_origins)
        if unselected_spawn_count:
            omitted_entities["spawn:not_selected"] += unselected_spawn_count

    for entity in entities:
        classname = entity.get("classname") or "<missing>"
        origin = entity.get("origin")
        if classname in {"worldspawn", "func_static", "target_position", "misc_teleporter_dest"}:
            continue
        if classname in {"info_player_deathmatch", "info_player_start"} and _vector(origin):
            if selected_spawn_origins is not None:
                continue
            properties = [("classname", "lg_spawn"), ("origin", origin or "")]
            if entity.get("angle") is not None:
                properties.append(("angle", entity.get("angle") or ""))
            mapped_entities.append((properties, [], False))
            converted_counts["spawns"] += 1
        elif classname == "light" and _vector(origin):
            properties = [("classname", "light"), ("origin", origin or "")]
            for key in ("_color", "color", "_light", "light"):
                if entity.get(key) is not None:
                    properties.append((key, entity.get(key) or ""))
            mapped_entities.append((properties, [], False))
            converted_counts["lights"] += 1
        elif classname in {"item_health", "item_health_small", "item_health_large"} and _vector(origin):
            output_class = "item_health_large" if classname == "item_health_large" else "item_health_small"
            mapped_entities.append(([("classname", output_class), ("origin", origin or "")], [], False))
            converted_counts["health_pickups"] += 1
        elif classname == "trigger_push":
            target_name = entity.get("target")
            if target_name in ambiguous_targetnames:
                omitted_entities["trigger_push:ambiguous_target"] += 1
                continue
            target = target_by_name.get(target_name)
            valid_brushes = [brush for brush in entity.brushes if results[id(brush)].valid]
            clean = (
                bool(target_name)
                and target is not None
                and _vector(target.get("origin")) is not None
                and bool(valid_brushes)
                and len(valid_brushes) == len(entity.brushes)
            )
            if clean:
                properties = [("classname", "trigger_jumppad"), ("target", target_name or "")]
                if entity.get("speed") is not None:
                    properties.append(("speed", entity.get("speed") or ""))
                mapped_entities.append((properties, valid_brushes, True))
                used_targets.add(target_name or "")
                converted_counts["jump_pads"] += len(valid_brushes)
            else:
                omitted_entities["trigger_push:unclean_or_unresolved"] += 1
        elif classname == "trigger_teleport":
            target_name = entity.get("target")
            if target_name in ambiguous_targetnames:
                omitted_entities["trigger_teleport:ambiguous_target"] += 1
                continue
            target = target_by_name.get(target_name)
            valid_brushes = [brush for brush in entity.brushes if results[id(brush)].valid]
            clean = (
                bool(target_name) and target is not None and _vector(target.get("origin")) is not None and
                bool(valid_brushes) and len(valid_brushes) == len(entity.brushes)
            )
            if clean:
                mapped_entities.append(([("classname", "trigger_teleport"), ("target", target_name or "")], valid_brushes, True))
                used_targets.add(target_name or "")
                converted_counts["teleports"] += len(valid_brushes)
            else:
                omitted_entities["trigger_teleport:unclean_or_unresolved"] += 1
        else:
            omitted_entities[f"{classname}:unsupported"] += 1
    for target_name in sorted(used_targets):
        target = target_by_name[target_name]
        properties = [("classname", "target_position"), ("targetname", target_name), ("origin", target.get("origin") or "")]
        if target.get("angle") is not None:
            properties.append(("angle", target.get("angle") or ""))
        mapped_entities.append((properties, [], False))
        converted_counts["target_positions"] += 1

    if adaptation and "sun" in adaptation:
        sun = adaptation["sun"]
        if not isinstance(sun, dict):
            raise ConversionError("adaptation sun must be an object")
        direction = sun.get("direction")
        color = sun.get("color")
        intensity = sun.get("intensity")
        if not isinstance(direction, list) or len(direction) != 3 or not all(isinstance(value, (int, float)) for value in direction):
            raise ConversionError("adaptation sun.direction must be a three-number array")
        if not isinstance(color, list) or len(color) != 3 or not all(isinstance(value, (int, float)) for value in color):
            raise ConversionError("adaptation sun.color must be a three-number array")
        if not isinstance(intensity, (int, float)) or not 0.0 <= float(intensity) <= 4.0:
            raise ConversionError("adaptation sun.intensity must be between 0 and 4")
        mapped_entities.append(([
            ("classname", "light_sun"),
            ("direction", " ".join(_format_number(float(value)) for value in direction)),
            ("color", " ".join(_format_number(float(value)) for value in color)),
            ("intensity", _format_number(float(intensity))),
        ], [], False))
        converted_counts["sun_lights"] += 1

    if adaptation and "lights" in adaptation:
        lights = adaptation["lights"]
        if not isinstance(lights, list):
            raise ConversionError("adaptation lights must be an array")
        for index, light in enumerate(lights):
            if not isinstance(light, dict):
                raise ConversionError(f"adaptation light {index} must be an object")
            origin = light.get("origin")
            color = light.get("color")
            intensity = light.get("intensity")
            radius = light.get("radius")
            if not isinstance(origin, list) or len(origin) != 3 or not all(isinstance(value, (int, float)) for value in origin):
                raise ConversionError(f"adaptation light {index}.origin must be a three-number array")
            if not isinstance(color, list) or len(color) != 3 or not all(isinstance(value, (int, float)) for value in color):
                raise ConversionError(f"adaptation light {index}.color must be a three-number array")
            if not isinstance(intensity, (int, float)) or not 0.0 < float(intensity) <= 4.0:
                raise ConversionError(f"adaptation light {index}.intensity must be between 0 and 4")
            if not isinstance(radius, (int, float)) or not 1.0 <= float(radius) <= 4096.0:
                raise ConversionError(f"adaptation light {index}.radius must be between 1 and 4096")
            mapped_entities.append(([
                ("classname", "light"),
                ("origin", " ".join(_format_number(float(value)) for value in origin)),
                ("color", " ".join(_format_number(float(value)) for value in color)),
                ("intensity", _format_number(float(intensity))),
                ("radius", _format_number(float(radius))),
            ], [], False))
            converted_counts["lights"] += 1

    wall_count = sum(results[id(brush)].kind == "wall" for brush in emitted_world) + sum(
        results[id(item["brush"])].kind == "wall" for item in emitted_static
    )
    convex_count = sum(results[id(brush)].kind == "convex_brush" for brush in emitted_world) + sum(
        results[id(item["brush"])].kind == "convex_brush" for item in emitted_static
    )
    projected = {
        "walls": wall_count,
        "convex_brushes": convex_count,
        "lights": converted_counts["lights"],
        "jump_pads": converted_counts["jump_pads"],
        "health_pickups": converted_counts["health_pickups"],
        "spawns": converted_counts["spawns"],
        "teleports": converted_counts["teleports"],
        "entities": 1 + len(emitted_static) + len(mapped_entities),
    }
    over_limits = {
        name: {"projected": projected[name], "limit": limit, "excess": projected[name] - limit}
        for name, limit in LIMITS.items()
        if projected[name] > limit
    }

    bsp_bounds = bsp.get("world_model_bounds")
    if isinstance(bsp_bounds, dict):
        bounds_min = tuple(float(value) for value in bsp_bounds["min"])
        bounds_max = tuple(float(value) for value in bsp_bounds["max"])
    elif valid_bounds:
        bounds_min = tuple(min(bounds[0][axis] for bounds in valid_bounds) for axis in range(3))
        bounds_max = tuple(max(bounds[1][axis] for bounds in valid_bounds) for axis in range(3))
    else:
        bounds_min, bounds_max = (-4096.0,) * 3, (4096.0,) * 3
    world_properties = [
        ("classname", "worldspawn"),
        ("lg_bounds_min", " ".join(_format_number(value - 40.0) for value in bounds_min)),
        ("lg_bounds_max", " ".join(_format_number(value + 40.0) for value in bounds_max)),
        ("lg_source_bsp_sha256", str(bsp.get("sha256", ""))),
        ("lg_raw_decompile_sha256", raw_sha256),
    ]
    output_lines = [
        "// Generated by scripts/import_q3_map.py; adaptation choices are recorded in reports."
        if adaptation else
        "// Generated conservatively by scripts/import_q3_map.py; patches and Q3 shaders were not imported."
    ]
    output_lines.extend(_emit_entity(world_properties, emitted_world, adaptation=adaptation))
    for item in emitted_static:
        output_lines.extend(_emit_entity([
            ("classname", "func_group"),
            ("lg_source_entity_index", str(item["entity_index"])),
            ("lg_source_brush_index", str(item["brush_index"])),
            ("lg_source_classname", str(item["classname"])),
            ("lg_collision_class", str(item["classification"])),
        ], [item["brush"]], adaptation=adaptation,
            classification=str(item["classification"]),
            material_role=item["material_role"] if isinstance(item["material_role"], str) else None))
    for properties, brushes, trigger in mapped_entities:
        output_lines.extend(_emit_entity(properties, brushes, trigger, adaptation))
    output_map = "\n".join(output_lines) + "\n"

    report: dict[str, object] = {
        "schema_version": 2,
        "status": "over_limit" if over_limits else "convertible",
        "sources": {
            "q3map2": Q3MAP2_TOOLCHAIN,
            "raw_map": {"name": raw_name, "size": len(raw_text.encode("utf-8")), "sha256": raw_sha256},
            "bsp": bsp,
            "aas": aas,
        },
        "outputs": {
            "candidate_map": {
                "size": len(output_map.encode("utf-8")),
                "sha256": _sha256(output_map.encode("utf-8")),
            },
        },
        "inventory": {
            "entity_count": len(entities),
            "classnames": dict(sorted(classnames.items())),
            "classic_brush_count": sum(len(entity.brushes) for entity in entities),
            "patch_count": sum(patch_kinds.values()),
            "patch_kinds": dict(sorted(patch_kinds.items())),
            "patch_materials": dict(sorted(patch_materials.items())),
            "face_materials": dict(sorted(materials.items())),
            "unsupported_constructs": dict(sorted(construct_kinds.items())),
        },
        "geometry": {
            "valid_brush_count": sum(result.valid for result in results.values()),
            "invalid_brush_count": len(geometry_issues),
            "degenerate_face_count": sum(issue["degenerate_faces"] for issue in geometry_issues),
            "issues": geometry_issues,
        },
        "gameplay": {
            "source_spawns": classnames["info_player_deathmatch"] + classnames["info_player_start"],
            "source_lights": classnames["light"],
            "source_health_pickups": sum(classnames[name] for name in ("item_health", "item_health_small", "item_health_large")),
            "source_trigger_push": classnames["trigger_push"],
            "source_teleports": classnames["trigger_teleport"],
            "converted": dict(sorted(converted_counts.items())),
        },
        "conversion": {
            "brushes": brush_rows,
            "brush_policy": {
                **policy_summary,
                "matched_rule_count": sum(row["requested_action"] != "allow" or row["requested_reason"] is not None for row in brush_rows),
                "drop_count": sum(row["requested_action"] == "drop" for row in brush_rows),
                "override_count": sum(row["requested_action"] == "override" for row in brush_rows),
            },
            "adaptation_binding": ({
                "schema_version": adaptation.get("schema_version"),
                "source_bsp_sha256": adaptation.get("source_bsp_sha256"),
                "raw_decompile_sha256": adaptation.get("raw_decompile_sha256"),
                "verified": adaptation.get("schema_version") == 2,
            } if adaptation else None),
            "projected_counts": projected,
            "limits": LIMITS,
            "over_limits": over_limits,
            "runtime_effective_spawns": min(projected["spawns"], LIMITS["spawns"]),
            "runtime_inactive_spawns": max(0, projected["spawns"] - LIMITS["spawns"]),
            "omitted_brushes": dict(sorted(omitted_brushes.items())),
            "omitted_entities": dict(sorted(omitted_entities.items())),
            "ambiguous_targetnames": ambiguous_targetnames,
            "reconstructed_patches": reconstructed_patch_count,
            "reconstructed_patch_brushes": reconstructed_patch_brushes,
            "reconstructed_patch_indices": sorted(selected_patch_indices),
            "adaptation_marker_boxes": adaptation_marker_count,
            "omitted_patches": sum(patch_kinds.values()) - reconstructed_patch_count,
            "omitted_patch_policy": (
                "Only adaptation-selected patches are reconstructed as thin deterministic convex prisms; all others remain omitted."
                if adaptation else "Patches are never approximated or tessellated."
            ),
            "shader_policy": (
                "Source shader names map by checked-in adaptation roles to original LG textures."
                if adaptation else "Every emitted solid face uses an existing Tiny3 placeholder; Q3 shaders are inventory-only."
            ),
            "collision_material_policy": (
                "All-common/clip and common/playerclip brushes emit common/playerclip; all-common/weapclip brushes "
                "remain common/weapclip. LG-Duel currently traces both conservatively as collision."
            ),
            "volume_material_policy": (
                "All-sfx/hellfog brushes are omitted atmospheric volumes and emit neither render nor collision geometry."
            ),
            "placeholder_materials": list(PLACEHOLDER_MATERIALS) if not adaptation else [],
            "adaptation": adaptation,
            "static_geometry_policy": (
                "Each emitted source worldspawn or func_static brush is a one-brush func_group carrying stable source "
                "entity/brush indices, classname, and effective collision classification."
            ),
            "scale_policy": (
                "No rescale or rebalance is applied. Authored Quake coordinates are preserved; "
                "LG-Duel's existing loader converts them at 1/40 and bounds gain 40 Quake units of padding."
            ),
            "capacity_policy": (
                "Geometry and gameplay entities are emitted without converter truncation; over-limit output is marked and "
                "the CLI fails unless explicitly allowed. LG-Duel currently activates up to 32 authored spawns, "
                "so runtime_effective_spawns and runtime_inactive_spawns make that loader behavior explicit."
            ),
            "aas_route_import_attempted": False,
        },
    }
    return output_map, report


def markdown_report(report: dict[str, object]) -> str:
    inventory = report["inventory"]
    geometry = report["geometry"]
    conversion = report["conversion"]
    gameplay = report["gameplay"]
    sources = report["sources"]
    candidate = report["outputs"]["candidate_map"]
    adaptation = conversion.get("adaptation")
    lines = [
        "# Quake 3 map conversion inventory",
        "",
        f"Status: **{report['status']}**",
        "",
        "## Sources",
        "",
        f"- q3map2: {sources['q3map2']['distribution']} {sources['q3map2']['release']}, `{sources['q3map2']['version']}`, setup `{sources['q3map2']['setup_script']}`, archive SHA-256 `{sources['q3map2']['archive_sha256']}`",
        f"- Raw map: `{sources['raw_map']['name']}` ({sources['raw_map']['size']} bytes, SHA-256 `{sources['raw_map']['sha256']}`)",
        f"- BSP: `{sources['bsp']['name']}`, {sources['bsp']['magic']} v{sources['bsp']['version']}, {sources['bsp']['size']} bytes, SHA-256 `{sources['bsp']['sha256']}`",
        f"- Generated candidate: {candidate['size']} bytes, SHA-256 `{candidate['sha256']}`",
    ]
    bsp_bounds = sources["bsp"]["world_model_bounds"]
    if bsp_bounds:
        lines.append(f"- BSP world bounds: min `{bsp_bounds['min']}`, max `{bsp_bounds['max']}`")
    if sources["aas"]:
        aas = sources["aas"]
        lines.append(f"- AAS: `{aas['name']}`, {aas['magic']} v{aas['version']}, BSP checksum `{aas['bsp_checksum']}`, {aas['size']} bytes, SHA-256 `{aas['sha256']}`")
    else:
        lines.append("- AAS: not provided")
    binding = conversion.get("adaptation_binding")
    if binding:
        lines.append(
            f"- Adaptation binding: schema v{binding['schema_version']}, verified `{binding['verified']}`, "
            f"BSP `{binding['source_bsp_sha256']}`, raw `{binding['raw_decompile_sha256']}`"
        )
    lines.extend([
        "- AAS routes: metadata only; no AAS route import was attempted.",
        "",
        "### BSP lumps",
        "",
        "| Index | Lump | Offset | Bytes | Records | Trailing bytes |",
        "|---:|---|---:|---:|---:|---:|",
    ])
    for lump in sources["bsp"]["lumps"]:
        lines.append(
            f"| {lump['index']} | `{lump['name']}` | {lump['offset']} | {lump['length']} | "
            f"{lump.get('count', 'n/a')} | {lump.get('trailing_bytes', 'n/a')} |"
        )
    lines.extend([
        "",
        "## Inventory",
        "",
        f"- Entities: {inventory['entity_count']}",
        f"- Classic brushes: {inventory['classic_brush_count']}",
        f"- Source patches: {inventory['patch_count']}",
        f"- Reconstructed patches: {conversion['reconstructed_patches']} ({conversion['reconstructed_patch_brushes']} convex brushes)",
        f"- Valid brushes: {geometry['valid_brush_count']}",
        f"- Invalid brushes: {geometry['invalid_brush_count']}",
        f"- Degenerate faces: {geometry['degenerate_face_count']}",
        "",
        "### Classnames",
        "",
        "| Classname | Count |",
        "|---|---:|",
    ])
    lines.extend(f"| `{name}` | {count} |" for name, count in inventory["classnames"].items())
    lines.extend(["", "### Source face materials", "", "| Material | Faces |", "|---|---:|"])
    lines.extend(f"| `{name}` | {count} |" for name, count in inventory["face_materials"].items())
    lines.extend([
        "",
        "## Gameplay conversion",
        "",
        f"- Source spawns: {gameplay['source_spawns']}",
        f"- Source lights: {gameplay['source_lights']}",
        f"- Source health pickups: {gameplay['source_health_pickups']}",
        f"- Source trigger_push entities: {gameplay['source_trigger_push']}",
        f"- Source teleports: {gameplay['source_teleports']} (converted: {gameplay['converted'].get('teleports', 0)})",
        f"- Runtime-active spawns: {conversion['runtime_effective_spawns']} (inactive authored spawns: {conversion['runtime_inactive_spawns']})",
        "",
        "## Projected LG counts",
        "",
        "| Category | Projected | Limit | Result |",
        "|---|---:|---:|---|",
    ])
    for name, limit in conversion["limits"].items():
        projected = conversion["projected_counts"][name]
        lines.append(f"| {name} | {projected} | {limit} | {'OVER' if projected > limit else 'OK'} |")
    policy = conversion["brush_policy"]
    lines.extend([
        "",
        "## Static brush provenance and policy",
        "",
        f"Default action: `{policy['default_action']}`. Rules: {policy['rule_count']} "
        f"(drops: {policy['drop_count']}, overrides: {policy['override_count']}).",
        "",
        "| Entity | Brush | Class | Line | Materials | Automatic | Requested | Effective | Result / reason |",
        "|---:|---:|---|---:|---|---|---|---|---|",
    ])
    for row in conversion["brushes"]:
        automatic = row["automatic_classification"] or f"omit:{row['automatic_omission_reason']}"
        effective = row["effective_classification"] or "omitted"
        result = "emitted" if row["emitted"] else f"omitted: {row['omission_reason']}"
        if row["requested_reason"]:
            result += f" ({row['requested_reason']})"
        lines.append(
            f"| {row['source_entity_index']} | {row['source_brush_index']} | `{row['source_classname']}` | "
            f"{row['source_line']} | `{', '.join(row['source_materials'])}` | `{automatic}` | "
            f"`{row['requested_action']}` | `{effective}` | {result} |"
        )
    lines.extend([
        "",
        (
            "The converter did not truncate the candidate. The checked-in adaptation deliberately selects its authored spawns, assigns original LG materials by named source-shader roles, and reconstructs only its listed patches; remaining unsupported content stays explicit below."
            if adaptation else
            "The converter did not truncate the candidate. LG-Duel activates up to 32 authored spawns; the active/inactive counts above state that runtime behavior. Unsupported source content stays explicit below."
        ),
        "",
        f"Patch policy: {conversion['omitted_patch_policy']}",
        "",
        f"Shader policy: {conversion['shader_policy']}",
        "",
        f"Collision material policy: {conversion['collision_material_policy']}",
        "",
        f"Volume material policy: {conversion['volume_material_policy']}",
        "",
        f"Scale policy: {conversion['scale_policy']}",
        "",
        "## Omitted content",
        "",
        f"- Patches: {conversion['omitted_patches']}",
    ])
    lines.extend(f"- Brushes `{reason}`: {count}" for reason, count in conversion["omitted_brushes"].items())
    lines.extend(f"- Entities `{reason}`: {count}" for reason, count in conversion["omitted_entities"].items())
    if geometry["issues"]:
        lines.extend(["", "## Invalid geometry", "", "| Entity | Brush | Line | Reason |", "|---:|---:|---:|---|"])
        lines.extend(
            f"| {issue['entity_index']} | {issue['brush_index']} | {issue['line']} | {issue['reason']} |"
            for issue in geometry["issues"]
        )
    return "\n".join(lines) + "\n"


def _parse_args(argv: Sequence[str] | None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-bsp", required=True, type=pathlib.Path)
    parser.add_argument("--source-aas", type=pathlib.Path, help="optional Quake 3 EAAS v5 metadata source")
    parser.add_argument("--raw-map", required=True, type=pathlib.Path)
    parser.add_argument("--output-map", required=True, type=pathlib.Path)
    parser.add_argument("--json-report", required=True, type=pathlib.Path)
    parser.add_argument("--markdown-report", required=True, type=pathlib.Path)
    parser.add_argument("--adaptation", type=pathlib.Path, help="checked-in deterministic adaptation JSON")
    parser.add_argument("--allow-over-limit", action="store_true", help="return success while retaining explicit over-limit status")
    return parser.parse_args(argv)


def _atomic_write_text(path: pathlib.Path, content: str) -> None:
    """Replace one artifact only after its complete sibling temp file is durable."""
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(prefix=f".{path.name}.", suffix=".tmp", dir=path.parent)
    temporary_path = pathlib.Path(temporary_name)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8", newline="\n") as output:
            output.write(content)
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary_path, path)
    except BaseException:
        temporary_path.unlink(missing_ok=True)
        raise


def main(argv: Sequence[str] | None = None) -> int:
    args = _parse_args(argv)
    try:
        raw_bytes = args.raw_map.read_bytes()
        raw_text = raw_bytes.decode("utf-8")
        bsp = read_bsp_metadata(args.source_bsp)
        aas = read_aas_metadata(args.source_aas) if args.source_aas else None
        adaptation = None
        if args.adaptation:
            adaptation = json.loads(args.adaptation.read_text(encoding="utf-8"))
            if not isinstance(adaptation, dict) or adaptation.get("schema_version") != 2:
                raise ConversionError("adaptation must be a JSON object with schema_version 2")
            adaptation = dict(adaptation)
            adaptation["source_file"] = args.adaptation.name
        output_map, report = convert(raw_text, args.raw_map.name, bsp, aas, adaptation)
        report["outputs"]["candidate_map"]["name"] = args.output_map.name
        _atomic_write_text(args.output_map, output_map)
        _atomic_write_text(args.json_report, json.dumps(report, indent=2, sort_keys=True) + "\n")
        _atomic_write_text(args.markdown_report, markdown_report(report))
    except (ConversionError, OSError, UnicodeError, json.JSONDecodeError) as error:
        print(f"import_q3_map: {error}", file=sys.stderr)
        return 1
    if report["status"] == "over_limit" and not args.allow_over_limit:
        print("import_q3_map: output exceeds LG capacities; artifacts were written without truncation", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
