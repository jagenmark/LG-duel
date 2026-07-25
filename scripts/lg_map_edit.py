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
REVISION_RE = re.compile(r"^[0-9a-f]{64}$")
MATERIAL_RE = re.compile(r"^[A-Za-z0-9_][A-Za-z0-9_./-]{0,191}$")
ROLLBACK_RE = re.compile(r"^[0-9a-f]{32}$")
STATE_PREFIX = "// lg-map-api-state-v1 "
FORMAT_VERSION = 1
MAX_COORDINATE = 40_000.0
MIN_CUBOID_SIZE = 0.01
MAX_CUBOIDS = 2048
MAX_SPAWNS = 32
MAX_MANAGED_MAP_BYTES = 8 * 1024 * 1024
COLLISION_MATERIALS = {"common/clip", "common/playerclip", "common/weapclip"}


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


def _render(state: dict[str, Any]) -> bytes:
    _validate_state(state)
    encoded = base64.urlsafe_b64encode(_canonical_json(state)).decode("ascii")
    lines = [
        f"{STATE_PREFIX}{encoded}",
        "// Game: LG Duel",
        "// Format: Standard",
    ]
    entities = sorted(
        state["entities"], key=lambda entity: (entity["classname"] != "worldspawn", entity["id"])
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
    return ("\n".join(lines) + "\n").encode("utf-8")


def _initial_state() -> dict[str, Any]:
    return {
        "format": FORMAT_VERSION,
        "template": "initial",
        "entities": [
            {
                "id": "worldspawn",
                "classname": "worldspawn",
                "properties": {
                    "lg_bounds_max": "512 512 256",
                    "lg_bounds_min": "-512 -512 -128",
                    "lg_map_api_version": "1",
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
    }


def _validate_properties(entity: dict[str, Any]) -> None:
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
        if properties.get("lg_map_api_version") != "1":
            raise MapEditError("worldspawn must keep lg_map_api_version=1")
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


def _validate_state(state: Any) -> None:
    if not isinstance(state, dict) or set(state) != {"format", "template", "entities", "cuboids"}:
        raise MapEditError("managed map state has an invalid shape")
    if state["format"] != FORMAT_VERSION or state["template"] != "initial":
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
        _validate_properties(entity)
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


def _decode_state(data: bytes) -> dict[str, Any]:
    if len(data) > MAX_MANAGED_MAP_BYTES:
        raise MapEditError("managed map exceeds the 8 MiB API limit")
    try:
        first_line = data.decode("utf-8").splitlines()[0]
    except (UnicodeDecodeError, IndexError) as error:
        raise MapEditError("map is not a UTF-8 MCP-managed map") from error
    if not first_line.startswith(STATE_PREFIX):
        raise MapEditError("map is not managed by the MCP map API")
    try:
        raw = base64.urlsafe_b64decode(first_line[len(STATE_PREFIX):].encode("ascii"))
        state = json.loads(raw)
    except (ValueError, UnicodeError, json.JSONDecodeError) as error:
        raise MapEditError("managed map state marker is invalid") from error
    _validate_state(state)
    if _render(state) != data:
        raise MapEditError(
            "managed map text changed outside the API; use TrenchBroom or recreate the managed map"
        )
    return state


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
                    if line.startswith(STATE_PREFIX)
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
            "objects": {"entities": ["worldspawn", "spawn-a", "spawn-b"], "cuboids": []},
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
                }
            except MapEditError as caught:
                managed = False
                error = str(caught)
            entry = {
                "map": path.stem,
                "revision": _sha256(data),
                "size_bytes": len(data),
                "managed": managed,
            }
            if objects is not None:
                entry["objects"] = objects
            if error is not None:
                entry["edit_status"] = error
            maps.append(entry)
        return {"maps": maps, "count": len(maps)}

    def inspect(self, map_name: Any) -> dict[str, Any]:
        name, _path, data, state = self._read(map_name)
        return {
            "map": name,
            "revision": _sha256(data),
            "size_bytes": len(data),
            "managed": True,
            "template": state["template"],
            "entities": copy.deepcopy(state["entities"]),
            "cuboids": copy.deepcopy(state["cuboids"]),
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
                self._validate_managed_data(before)
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
            _name, _path, data, state = self._read(name)
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
            _name, source_path, data, _state = self._read(name)
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
            _name, source_path, data, state = self._read(name)
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
