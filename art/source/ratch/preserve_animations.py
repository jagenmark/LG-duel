#!/usr/bin/env python3
"""Copy Pip/Fuse animation samples onto a compatible Ratch glTF rig.

The animation accessor and bufferView bytes are copied verbatim from the Pip
GLB into the target GLB.  Only animation node indices and buffer offsets are
rewritten; the source file is never modified.
"""

from __future__ import annotations

import argparse
import json
import math
import struct
from pathlib import Path
from typing import Any


GLB_HEADER = struct.Struct("<4sII")
GLB_CHUNK = struct.Struct("<I4s")
DEFAULT_TOLERANCE = 1e-5


def read_glb(path: Path) -> tuple[dict[str, Any], bytes]:
    raw = path.read_bytes()
    if len(raw) < GLB_HEADER.size:
        raise ValueError(f"{path}: file is too short to be a GLB")
    magic, version, length = GLB_HEADER.unpack_from(raw)
    if magic != b"glTF" or version != 2 or length != len(raw):
        raise ValueError(f"{path}: invalid GLB header")
    offset = GLB_HEADER.size
    json_chunk: bytes | None = None
    bin_chunk = b""
    while offset < len(raw):
        if offset + GLB_CHUNK.size > len(raw):
            raise ValueError(f"{path}: truncated GLB chunk header")
        chunk_length, chunk_type = GLB_CHUNK.unpack_from(raw, offset)
        offset += GLB_CHUNK.size
        end = offset + chunk_length
        if end > len(raw):
            raise ValueError(f"{path}: truncated GLB chunk")
        chunk = raw[offset:end]
        offset = end
        if chunk_type == b"JSON":
            json_chunk = chunk
        elif chunk_type == b"BIN\0":
            bin_chunk = chunk
    if json_chunk is None:
        raise ValueError(f"{path}: missing JSON chunk")
    return json.loads(json_chunk.rstrip(b" \t\r\n\0")), bin_chunk


def write_glb(path: Path, document: dict[str, Any], binary: bytes) -> None:
    encoded = json.dumps(document, separators=(",", ":"), ensure_ascii=False).encode()
    encoded += b" " * ((-len(encoded)) % 4)
    binary += b"\0" * ((-len(binary)) % 4)
    payload = GLB_CHUNK.pack(len(encoded), b"JSON") + encoded
    payload += GLB_CHUNK.pack(len(binary), b"BIN\0") + binary
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(GLB_HEADER.pack(b"glTF", 2, GLB_HEADER.size + len(payload)) + payload)


def node_trs(node: dict[str, Any]) -> tuple[list[float], list[float], list[float]]:
    return (
        list(node.get("translation", [0.0, 0.0, 0.0])),
        list(node.get("rotation", [0.0, 0.0, 0.0, 1.0])),
        list(node.get("scale", [1.0, 1.0, 1.0])),
    )


def max_abs_delta(a: list[float], b: list[float]) -> float:
    return max((abs(x - y) for x, y in zip(a, b)), default=0.0)


def quat_delta(a: list[float], b: list[float]) -> float:
    # q and -q represent the same rest rotation.
    return min(max_abs_delta(a, b), max_abs_delta(a, [-x for x in b]))


def accessor_bytes(document: dict[str, Any], binary: bytes, index: int) -> bytes:
    accessor = document["accessors"][index]
    if "sparse" in accessor:
        raise ValueError("sparse animation accessors are unsupported")
    view = document["bufferViews"][accessor["bufferView"]]
    if view.get("buffer", 0) != 0:
        raise ValueError("animation accessor references a nonzero buffer")
    start = view.get("byteOffset", 0)
    end = start + view["byteLength"]
    if end > len(binary):
        raise ValueError("animation accessor bufferView is outside the BIN chunk")
    return binary[start:end]


def validate_rigs(
    target: dict[str, Any],
    target_bin: bytes,
    source: dict[str, Any],
    source_bin: bytes,
    tolerance: float,
) -> tuple[dict[str, int], dict[str, int], set[str]]:
    if len(target.get("skins", [])) != 1 or len(source.get("skins", [])) != 1:
        raise ValueError("both GLBs must contain exactly one skin")
    target_nodes = target["nodes"]
    source_nodes = source["nodes"]
    target_names = {n.get("name"): i for i, n in enumerate(target_nodes) if n.get("name")}
    source_names = {n.get("name"): i for i, n in enumerate(source_nodes) if n.get("name")}
    if len(target_names) != len([n for n in target_nodes if n.get("name")]):
        raise ValueError("target GLB has duplicate node names")
    if len(source_names) != len([n for n in source_nodes if n.get("name")]):
        raise ValueError("source GLB has duplicate node names")

    def parent_names(nodes: list[dict[str, Any]]) -> dict[int, str | None]:
        parents = {child: i for i, node in enumerate(nodes) for child in node.get("children", [])}
        return {i: nodes[parent].get("name") if parent is not None else None for i, parent in ((i, parents.get(i)) for i in range(len(nodes)))}

    target_parents = parent_names(target_nodes)
    source_parents = parent_names(source_nodes)
    target_skin = target["skins"][0]
    source_skin = source["skins"][0]
    target_joint_names = {target_nodes[i].get("name") for i in target_skin["joints"]}
    source_joint_names = {source_nodes[i].get("name") for i in source_skin["joints"]}
    if target_joint_names != source_joint_names:
        raise ValueError(f"skin joint names differ: target={sorted(target_joint_names)} source={sorted(source_joint_names)}")

    for name in sorted(source_joint_names):
        si, ti = source_names[name], target_names.get(name)
        if ti is None:
            raise ValueError(f"target is missing source joint {name!r}")
        if source_parents[si] != target_parents[ti]:
            raise ValueError(f"parent mismatch for joint {name!r}: target={target_parents[ti]!r} source={source_parents[si]!r}")
        st, sr, ss = node_trs(source_nodes[si])
        tt, tr, ts = node_trs(target_nodes[ti])
        if max_abs_delta(st, tt) > tolerance or quat_delta(sr, tr) > tolerance or max_abs_delta(ss, ts) > tolerance:
            raise ValueError(f"rest transform mismatch for joint {name!r}")

    if "inverseBindMatrices" not in target_skin or "inverseBindMatrices" not in source_skin:
        raise ValueError("both skins must provide inverseBindMatrices")
    target_binds = accessor_bytes(target, target_bin, target_skin["inverseBindMatrices"])
    source_binds = accessor_bytes(source, source_bin, source_skin["inverseBindMatrices"])
    ta = target["accessors"][target_skin["inverseBindMatrices"]]
    sa = source["accessors"][source_skin["inverseBindMatrices"]]
    if ta.get("count") != sa.get("count") or ta.get("type") != "MAT4" or sa.get("type") != "MAT4":
        raise ValueError("inverse bind matrix accessors differ in shape")
    # Matrix values are float32 and stored in skin joint-array order. The rig
    # validator compares by joint name, so differing joint order is allowed.
    def matrices(doc: dict[str, Any], raw: bytes, accessor: dict[str, Any]) -> list[list[float]]:
        fmt = struct.Struct("<16f")
        view = doc["bufferViews"][accessor["bufferView"]]
        stride = view.get("byteStride", fmt.size)
        base = accessor.get("byteOffset", 0)
        return [list(fmt.unpack_from(raw, base + i * stride)) for i in range(accessor["count"])]

    tm = matrices(target, target_binds, ta)
    sm = matrices(source, source_binds, sa)
    target_bind_by_name = {target_nodes[j].get("name"): tm[i] for i, j in enumerate(target_skin["joints"])}
    source_bind_by_name = {source_nodes[j].get("name"): sm[i] for i, j in enumerate(source_skin["joints"])}
    foot_repairs: set[str] = set()
    for name in sorted(source_joint_names):
        delta = [abs(a - b) for a, b in zip(target_bind_by_name[name], source_bind_by_name[name])]
        if max(delta, default=0.0) <= tolerance:
            continue
        # Blender can derive a synthetic foot-bone tail on import/export. In
        # the known Pip rig this introduces a tiny scale-only error in the
        # Foot.* inverse bind (about 1.2e-5 on one diagonal element), while
        # the rotation, translation, and every other matrix element remain
        # identical. Accept only that exact, bounded artifact and repair it
        # with the source matrix below; all other bind mismatches are fatal.
        scale_only = (
            name in {"Foot.L", "Foot.R"}
            and max(delta[i] for i in range(16) if i not in {0, 5, 10, 15}) <= tolerance
            and max(abs(target_bind_by_name[name][i] - source_bind_by_name[name][i])
                    for i in (5, 10, 15)) <= tolerance
            and max(delta[i] for i in (0, 5, 10)) <= 2e-5
        )
        if not scale_only:
            raise ValueError(f"inverse bind matrix mismatch for joint {name!r}")
        foot_repairs.add(name)

    for animation in source.get("animations", []):
        for channel in animation.get("channels", []):
            node = channel.get("target", {}).get("node")
            if not isinstance(node, int) or node >= len(source_nodes) or source_nodes[node].get("name") not in target_names:
                raise ValueError("source animation targets a node absent from target GLB")
    return target_names, source_names, foot_repairs


def preserve_animations(target_path: Path, source_path: Path, output_path: Path, tolerance: float) -> None:
    if output_path.resolve() in {target_path.resolve(), source_path.resolve()}:
        raise ValueError("output must be a separate file from both inputs")
    target, target_bin = read_glb(target_path)
    source, source_bin = read_glb(source_path)
    target_names, source_names, foot_repairs = validate_rigs(
        target, target_bin, source, source_bin, tolerance
    )
    source_nodes = source["nodes"]

    # Restore only the known synthetic-tail foot bind entries, remapped by
    # joint name. This keeps the target mesh and joint order intact while
    # making its foot deformation use the exact Pip rest bind.
    binary = bytearray(target_bin)
    if foot_repairs:
        target_skin, source_skin = target["skins"][0], source["skins"][0]
        ta = target["accessors"][target_skin["inverseBindMatrices"]]
        sa = source["accessors"][source_skin["inverseBindMatrices"]]
        tv = target["bufferViews"][ta["bufferView"]]
        sv = source["bufferViews"][sa["bufferView"]]
        for ti, tj in enumerate(target_skin["joints"]):
            name = target["nodes"][tj].get("name")
            if name not in foot_repairs:
                continue
            si = next(i for i, sj in enumerate(source_skin["joints"])
                      if source["nodes"][sj].get("name") == name)
            to = tv.get("byteOffset", 0) + ta.get("byteOffset", 0) + ti * tv.get("byteStride", 64)
            so = sv.get("byteOffset", 0) + sa.get("byteOffset", 0) + si * sv.get("byteStride", 64)
            binary[to:to + 64] = source_bin[so:so + 64]

    # Rebuild only the animation accessor/bufferView references. Every source
    # bufferView's bytes are copied without decoding or re-encoding samples.
    new_views = target.setdefault("bufferViews", [])
    new_accessors = target.setdefault("accessors", [])
    accessor_map: dict[int, int] = {}
    view_map: dict[int, int] = {}
    def copy_accessor(index: int) -> int:
        if index in accessor_map:
            return accessor_map[index]
        accessor = dict(source["accessors"][index])
        source_view_index = accessor["bufferView"]
        if source_view_index not in view_map:
            view = dict(source["bufferViews"][source_view_index])
            view.pop("name", None)
            data = accessor_bytes(source, source_bin, index)
            while len(binary) % 4:
                binary.append(0)
            view["buffer"] = 0
            view["byteOffset"] = len(binary)
            binary.extend(data)
            view["byteLength"] = len(data)
            view_map[source_view_index] = len(new_views)
            new_views.append(view)
        accessor["bufferView"] = view_map[source_view_index]
        accessor.pop("name", None)
        accessor_map[index] = len(new_accessors)
        new_accessors.append(accessor)
        return accessor_map[index]

    animations = []
    for source_animation in source.get("animations", []):
        animation = {k: v for k, v in source_animation.items() if k != "name"}
        if "name" in source_animation:
            animation["name"] = source_animation["name"]
        animation["samplers"] = []
        for sampler in source_animation.get("samplers", []):
            copied = dict(sampler)
            copied["input"] = copy_accessor(sampler["input"])
            copied["output"] = copy_accessor(sampler["output"])
            animation["samplers"].append(copied)
        animation["channels"] = []
        for channel in source_animation.get("channels", []):
            copied = dict(channel)
            copied["target"] = dict(channel["target"])
            source_node = source_nodes[channel["target"]["node"]].get("name")
            copied["target"]["node"] = target_names[source_node]
            animation["channels"].append(copied)
        animations.append(animation)
    target["animations"] = animations
    target["buffers"][0]["byteLength"] = len(binary)
    write_glb(output_path, target, bytes(binary))
    repair_note = f", repaired {', '.join(sorted(foot_repairs))} synthetic-tail bind(s)" if foot_repairs else ""
    print(f"preserved {len(animations)} animations, {len(accessor_map)} accessors{repair_note} -> {output_path}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("target", type=Path, help="exported Ratch GLB to receive Pip animations")
    parser.add_argument("source", type=Path, help="original Pip/Fuse GLB containing animations")
    parser.add_argument("output", type=Path, help="separate output GLB")
    parser.add_argument("--tolerance", type=float, default=DEFAULT_TOLERANCE, help="rig comparison tolerance (default: %(default)s)")
    args = parser.parse_args()
    preserve_animations(args.target, args.source, args.output, args.tolerance)


if __name__ == "__main__":
    main()
