#!/usr/bin/env python3
"""Validate the reviewed Worker GLB and its authored material sidecar."""

from __future__ import annotations

import hashlib
import json
import math
import struct
import sys
from pathlib import Path

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
RUNTIME_GLB = ROOT / "assets/models/quaternius_worker/quaternius_worker.glb"
REVIEW_GLB = ROOT / "imports/assets/review/quaternius_worker/quaternius_worker.glb"
MANIFEST = ROOT / "assets/models/quaternius_worker/material-manifest.json"
ENGINE_REPORT = ROOT / "imports/assets/review/quaternius_worker/reports/engine-validation.json"
ALBEDO = ROOT / "assets/models/quaternius_worker/materials/worker_albedo.png"
MASK = ROOT / "assets/models/quaternius_worker/materials/worker_material_mask.png"

EXPECTED_SHA256 = "b72bb9287f761550b059f4dffcf721c78ae19d814c0de74633e4cbe18c455c60"
EXPECTED_MATERIALS = [
    "Skin",
    "Worker_Yellow",
    "Worker_Vest",
    "LightBrown",
    "Grey",
    "Black",
    "Skin.001",
    "Eyebrows",
    "Worker_Yellow.001",
    "Moustache",
    "Eye",
    "Brown",
    "Brown2",
]
EXPECTED_FLAT_TINT_WEIGHTS = {
    "Worker_Yellow": 0.92,
    "Worker_Vest": 1.0,
    "Worker_Yellow.001": 0.92,
}
EXPECTED_MASK_CELLS = {
    (0, 0): (0, 158, 0, 0),
    (1, 0): (235, 209, 0, 0),
    (2, 0): (255, 219, 0, 0),
    (3, 0): (0, 194, 0, 0),
    (0, 1): (0, 128, 46, 0),
    (1, 1): (0, 184, 0, 0),
    (2, 1): (0, 209, 0, 0),
    (3, 1): (0, 115, 0, 0),
    (0, 2): (0, 161, 0, 0),
    (1, 2): (0, 178, 0, 0),
}
COMPONENTS = {
    "SCALAR": 1,
    "VEC2": 2,
    "VEC3": 3,
    "VEC4": 4,
    "MAT2": 4,
    "MAT3": 9,
    "MAT4": 16,
}
COMPONENT_FORMATS = {
    5120: ("b", 1),
    5121: ("B", 1),
    5122: ("h", 2),
    5123: ("H", 2),
    5125: ("I", 4),
    5126: ("f", 4),
}


def fail(message: str) -> None:
    raise ValueError(message)


def require(condition: bool, message: str) -> None:
    if not condition:
        fail(message)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load_glb(path: Path) -> tuple[dict, bytes]:
    data = path.read_bytes()
    require(len(data) >= 20, f"{path} is too small to be a GLB")
    magic, version, length = struct.unpack_from("<4sII", data, 0)
    require(magic == b"glTF" and version == 2, f"{path} is not a GLB 2 file")
    require(length == len(data), f"{path} has an invalid GLB length")
    json_length, json_type = struct.unpack_from("<I4s", data, 12)
    require(json_type == b"JSON", f"{path} has no JSON chunk")
    json_end = 20 + json_length
    require(json_end <= len(data), f"{path} has a truncated JSON chunk")
    document = json.loads(data[20:json_end].decode("utf-8"))
    binary = b""
    if json_end + 8 <= len(data):
        binary_length, binary_type = struct.unpack_from("<I4s", data, json_end)
        require(binary_type == b"BIN\x00", f"{path} has an unexpected second chunk")
        binary_start = json_end + 8
        require(binary_start + binary_length <= len(data), f"{path} has a truncated BIN chunk")
        binary = data[binary_start:binary_start + binary_length]
    return document, binary


def accessor_values(document: dict, binary: bytes, accessor_index: int) -> list[tuple[float, ...]]:
    accessors = document.get("accessors", [])
    views = document.get("bufferViews", [])
    require(0 <= accessor_index < len(accessors), "accessor index is outside the GLB")
    accessor = accessors[accessor_index]
    view_index = accessor.get("bufferView")
    require(isinstance(view_index, int) and 0 <= view_index < len(views), "accessor has no valid buffer view")
    view = views[view_index]
    require(view.get("buffer", 0) == 0, "Worker accessor does not use the GLB binary buffer")
    component_type = accessor.get("componentType")
    value_type = accessor.get("type")
    require(component_type in COMPONENT_FORMATS and value_type in COMPONENTS, "unsupported Worker accessor type")
    format_code, component_size = COMPONENT_FORMATS[component_type]
    component_count = COMPONENTS[value_type]
    element_size = component_count * component_size
    stride = view.get("byteStride", element_size)
    require(isinstance(stride, int) and stride >= element_size, "invalid Worker accessor stride")
    offset = view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
    count = accessor.get("count")
    require(isinstance(offset, int) and isinstance(count, int) and count >= 0, "invalid Worker accessor range")
    require(offset >= 0 and offset + max(0, count - 1) * stride + element_size <= len(binary), "Worker accessor is outside its BIN chunk")
    value_format = "<" + format_code * component_count
    return [struct.unpack_from(value_format, binary, offset + index * stride) for index in range(count)]


def skinned_primitives(document: dict) -> list[dict]:
    meshes = document.get("meshes", [])
    skinned_meshes = {
        node.get("mesh")
        for node in document.get("nodes", [])
        if isinstance(node.get("mesh"), int) and isinstance(node.get("skin"), int)
    }
    primitives: list[dict] = []
    for mesh_index in sorted(skinned_meshes):
        require(0 <= mesh_index < len(meshes), "a skinned node refers to a missing mesh")
        primitives.extend(meshes[mesh_index].get("primitives", []))
    return primitives


def animation_durations(document: dict, binary: bytes) -> dict[str, float]:
    durations: dict[str, float] = {}
    for animation in document.get("animations", []):
        name = animation.get("name", "")
        maximum = 0.0
        for sampler in animation.get("samplers", []):
            values = accessor_values(document, binary, sampler["input"])
            for value in values:
                maximum = max(maximum, float(value[0]))
        durations[name] = maximum
    return durations


def validate_glb_contract(runtime: dict, binary: bytes, review: dict, review_binary: bytes) -> None:
    runtime_materials = [material.get("name", "") for material in runtime.get("materials", [])]
    require(runtime_materials == EXPECTED_MATERIALS, "Worker material names changed")
    require(len(runtime.get("images", [])) == 0 and len(runtime.get("textures", [])) == 0, "Worker GLB must remain texture-free")
    require(len(runtime.get("skins", [])) == 1, "Worker needs one skin")
    joints = runtime["skins"][0].get("joints", [])
    require(len(joints) == 73 and all(isinstance(joint, int) for joint in joints), "Worker joint inventory changed")
    require(runtime.get("skins") == review.get("skins"), "Worker skin hierarchy changed")

    primitives = skinned_primitives(runtime)
    require(len(primitives) == 13, "Worker skinned primitive count changed")
    material_mapping = [primitive.get("material") for primitive in primitives]
    require(material_mapping == list(range(13)), "Worker material-to-primitive mapping changed")
    require(all(primitive.get("mode", 4) == 4 for primitive in primitives), "Worker primitive topology changed")

    vertex_count = 0
    triangle_count = 0
    positions: list[tuple[float, ...]] = []
    uv_values: list[tuple[float, ...]] = []
    for primitive in primitives:
        attributes = primitive.get("attributes", {})
        for name in ("POSITION", "NORMAL", "TEXCOORD_0", "JOINTS_0", "WEIGHTS_0"):
            require(name in attributes, f"Worker primitive is missing {name}")
        primitive_positions = accessor_values(runtime, binary, attributes["POSITION"])
        primitive_uvs = accessor_values(runtime, binary, attributes["TEXCOORD_0"])
        primitive_joints = accessor_values(runtime, binary, attributes["JOINTS_0"])
        primitive_weights = accessor_values(runtime, binary, attributes["WEIGHTS_0"])
        indices = accessor_values(runtime, binary, primitive["indices"])
        require(len(primitive_positions) == len(primitive_uvs) == len(primitive_joints) == len(primitive_weights), "Worker primitive accessor counts differ")
        require(len(indices) % 3 == 0, "Worker primitive index count is not triangular")
        require(all(0 <= index[0] < len(primitive_positions) for index in indices), "Worker primitive has an invalid index")
        require(all(all(0 <= joint < len(joints) for joint in values) for values in primitive_joints), "Worker has an invalid joint index")
        require(all(abs(sum(values) - 1.0) <= 0.002 for values in primitive_weights), "Worker skin weights are not normalized")
        require(all(math.isfinite(value) for values in primitive_uvs for value in values), "Worker has a non-finite UV")
        vertex_count += len(primitive_positions)
        triangle_count += len(indices) // 3
        positions.extend(primitive_positions)
        uv_values.extend(primitive_uvs)
    require(vertex_count == 10244 and triangle_count == 5240, "Worker geometry counts changed")
    require(all(abs(u - 0.0) <= 0.000001 and abs(v - 1.0) <= 0.000001 for u, v in uv_values), "Worker UV policy changed; update the material authoring plan")
    bounds_min = tuple(min(position[axis] for position in positions) for axis in range(3))
    bounds_max = tuple(max(position[axis] for position in positions) for axis in range(3))
    expected_min = (-0.837733626, -0.003672587, -0.102561384)
    expected_max = (0.837733626, 1.862758517, 0.286688447)
    require(all(abs(actual - expected) < 0.00001 for actual, expected in zip(bounds_min, expected_min)), "Worker model minimum bounds changed")
    require(all(abs(actual - expected) < 0.00001 for actual, expected in zip(bounds_max, expected_max)), "Worker model maximum bounds changed")
    require(bounds_min[1] <= 0.0 and bounds_min[1] >= -0.01, "Worker foot placement changed")

    animation_names = [animation.get("name", "") for animation in runtime.get("animations", [])]
    require(len(animation_names) == 33 and "Idle_Gun_TwoHanded" in animation_names and "Death" in animation_names, "Worker animation inventory changed")
    require(animation_names == [animation.get("name", "") for animation in review.get("animations", [])], "Worker animation names changed")
    runtime_durations = animation_durations(runtime, binary)
    review_durations = animation_durations(review, review_binary)
    require(runtime_durations.keys() == review_durations.keys(), "Worker animation duration inventory changed")
    require(all(abs(runtime_durations[name] - review_durations[name]) <= 0.0001 for name in runtime_durations), "Worker animation duration changed")

    runtime_socket = next((node for node in runtime.get("nodes", []) if node.get("name") == "weapon_socket"), None)
    review_socket = next((node for node in review.get("nodes", []) if node.get("name") == "weapon_socket"), None)
    require(runtime_socket is not None and runtime_socket == review_socket, "Worker weapon socket changed")
    require(runtime.get("nodes") == review.get("nodes"), "Worker node hierarchy, orientation, or attachments changed")


def validate_manifest_and_images() -> None:
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8-sig"))
    require(manifest.get("schema_version") == 1, "Worker material manifest schema changed")
    require(manifest.get("model") == RUNTIME_GLB.name, "Worker material manifest targets the wrong model")
    require(manifest.get("model_sha256") == EXPECTED_SHA256, "Worker material manifest hash changed")
    require(manifest.get("opaque") is True, "Worker material manifest must force opaque rendering")
    require(manifest.get("uv_mode") == "material_cell", "Worker must retain its material-cell UV policy")
    require(manifest.get("albedo_mode") == "replace", "Worker albedo mode changed")
    require(manifest.get("mip_policy") == "runtime_generate", "Worker material mip policy changed")
    atlas = manifest.get("atlas", {})
    require(atlas == {"columns": 4, "rows": 4, "padding_texels": 8}, "Worker atlas layout changed")
    textures = manifest.get("textures", {})
    require(textures.get("albedo") == {
        "path": "materials/worker_albedo.png", "color_space": "srgb", "width": 512, "height": 512
    }, "Worker albedo declaration changed")
    require(textures.get("packed_mask") == {
        "path": "materials/worker_material_mask.png", "color_space": "linear", "width": 512, "height": 512
    }, "Worker mask declaration changed")
    require(manifest.get("packed_mask_contract") == {
        "r": "team_tint_weight",
        "g": "perceptual_roughness",
        "b": "metallic_weight",
        "a": "emissive_weight_reserved_zero",
    }, "Worker packed-mask contract changed")
    bindings = manifest.get("materials", [])
    require(len(bindings) == 13 and manifest.get("require_material_coverage") is True, "Worker material coverage changed")
    require([binding.get("index") for binding in bindings] == list(range(13)), "Worker material binding order changed")
    require([binding.get("name") for binding in bindings] == EXPECTED_MATERIALS, "Worker material binding names changed")
    require(
        {
            binding.get("name"): binding.get("flat_tint_weight")
            for binding in bindings
            if binding.get("flat_tint_weight") != 0.0
        } == EXPECTED_FLAT_TINT_WEIGHTS,
        "Worker flat tint weights changed",
    )
    cells = [binding.get("cell") for binding in bindings]
    require(cells == [[0, 0], [1, 0], [2, 0], [3, 0], [0, 1], [1, 1], [0, 0], [2, 1], [1, 0], [2, 1], [3, 1], [0, 2], [1, 2]], "Worker material-cell mapping changed")

    albedo = Image.open(ALBEDO)
    mask = Image.open(MASK)
    require(albedo.mode == "RGBA" and albedo.size == (512, 512), "Worker albedo must be a 512px RGBA atlas")
    require(mask.mode == "RGBA" and mask.size == (512, 512), "Worker mask must be a 512px RGBA atlas")
    require(albedo.getchannel("A").getextrema() == (255, 255), "Worker albedo must remain opaque")
    require(mask.getchannel("A").getextrema() == (0, 0), "Worker mask alpha must remain reserved zero")
    for (column, row), expected in EXPECTED_MASK_CELLS.items():
        actual = mask.getpixel((column * 128 + 64, row * 128 + 64))
        require(actual == expected, f"Worker mask cell {column},{row} changed")
    require(mask.getpixel((128 + 64, 64))[0] > 0 and mask.getpixel((256 + 64, 64))[0] > 0, "Worker torso team tint is missing")
    require(mask.getpixel((64, 64))[0] == 0 and mask.getpixel((64, 128 + 64))[0] == 0, "Worker team tint reached skin or neutral gear")


def main() -> int:
    try:
        for path in (RUNTIME_GLB, REVIEW_GLB, MANIFEST, ENGINE_REPORT, ALBEDO, MASK):
            require(path.is_file(), f"missing Worker material validation input: {path.relative_to(ROOT)}")
        require(sha256(RUNTIME_GLB) == EXPECTED_SHA256, "Worker runtime GLB hash changed")
        require(sha256(REVIEW_GLB) == EXPECTED_SHA256, "Worker review GLB hash changed")
        runtime, binary = load_glb(RUNTIME_GLB)
        review, review_binary = load_glb(REVIEW_GLB)
        report = json.loads(ENGINE_REPORT.read_text(encoding="utf-8-sig"))
        require(report.get("ok") is True and report.get("counts", {}).get("triangles") == 5240, "Worker review engine report is not the approved one")
        validate_glb_contract(runtime, binary, review, review_binary)
        validate_manifest_and_images()
    except (OSError, ValueError, KeyError, json.JSONDecodeError, struct.error) as error:
        print(f"Worker material validation failed: {error}", file=sys.stderr)
        return 1
    print("Worker GLB, material manifest, atlas, mask, and invariants are valid")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
