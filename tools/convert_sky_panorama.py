#!/usr/bin/env python3
"""Convert one 2:1 equirectangular panorama to a deterministic cube map."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import pathlib
from collections import defaultdict
from collections.abc import Callable

from PIL import Image


Direction = tuple[float, float, float]
FACE_ORDER = ("posx", "negx", "posy", "negy", "posz", "negz")
FACE_DIRECTIONS: dict[str, Callable[[float, float], Direction]] = {
    "posx": lambda u, v: (1.0, -v, -u),
    "negx": lambda u, v: (-1.0, -v, u),
    "posy": lambda u, v: (u, 1.0, v),
    "negy": lambda u, v: (u, -1.0, -v),
    "posz": lambda u, v: (u, -v, 1.0),
    "negz": lambda u, v: (-u, -v, -1.0),
}

def image_pixels(
    image: Image.Image,
) -> list[tuple[int, int, int, int]]:
    getter = getattr(image, "get_flattened_data", image.getdata)
    return list(getter())


def sha256_file(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def validate_panorama(image: Image.Image) -> None:
    width, height = image.size
    if width != height * 2:
        raise ValueError(
            f"panorama must be exactly 2:1; got {width}x{height}"
        )


def normalized_face_direction(
    face: str,
    x: int,
    y: int,
    size: int,
) -> Direction:
    if face not in FACE_DIRECTIONS:
        raise ValueError(f"unknown cube face {face!r}")
    if size < 2:
        raise ValueError("face size must be at least 2")
    # End pixels land on exact cube edges. This keeps shared edge and corner
    # directions equal across faces before equirectangular sampling.
    u = -1.0 + (2.0 * x / (size - 1))
    v = -1.0 + (2.0 * y / (size - 1))
    dx, dy, dz = FACE_DIRECTIONS[face](u, v)
    length = math.sqrt(dx * dx + dy * dy + dz * dz)
    return dx / length, dy / length, dz / length


def sample_equirect_bilinear(
    pixels: list[tuple[int, int, int, int]],
    width: int,
    height: int,
    direction: Direction,
) -> tuple[int, int, int, int]:
    dx, dy, dz = direction
    longitude = math.atan2(dy, dx)
    latitude = math.asin(max(-1.0, min(1.0, dz)))
    source_x = (longitude / (2.0 * math.pi) + 0.5) * width - 0.5
    source_y = (0.5 - latitude / math.pi) * height - 0.5

    x0_unwrapped = math.floor(source_x)
    y0_unclamped = math.floor(source_y)
    x_fraction = source_x - x0_unwrapped
    y_fraction = source_y - y0_unclamped
    x0 = x0_unwrapped % width
    x1 = (x0_unwrapped + 1) % width
    y0 = max(0, min(height - 1, y0_unclamped))
    y1 = max(0, min(height - 1, y0_unclamped + 1))

    top_left = pixels[y0 * width + x0]
    top_right = pixels[y0 * width + x1]
    bottom_left = pixels[y1 * width + x0]
    bottom_right = pixels[y1 * width + x1]
    result = []
    for channel in range(4):
        top = (
            top_left[channel] * (1.0 - x_fraction)
            + top_right[channel] * x_fraction
        )
        bottom = (
            bottom_left[channel] * (1.0 - x_fraction)
            + bottom_right[channel] * x_fraction
        )
        value = top * (1.0 - y_fraction) + bottom * y_fraction
        result.append(max(0, min(255, int(value + 0.5))))
    return tuple(result)  # type: ignore[return-value]


def build_cube_faces(
    panorama: Image.Image,
    face_size: int,
) -> dict[str, Image.Image]:
    validate_panorama(panorama)
    if face_size < 2:
        raise ValueError("face size must be at least 2")
    source = panorama.convert("RGBA")
    width, height = source.size
    pixels = image_pixels(source)
    faces: dict[str, Image.Image] = {}
    for face in FACE_ORDER:
        output_pixels = [
            sample_equirect_bilinear(
                pixels,
                width,
                height,
                normalized_face_direction(face, x, y, face_size),
            )
            for y in range(face_size)
            for x in range(face_size)
        ]
        output = Image.new("RGBA", (face_size, face_size))
        output.putdata(output_pixels)
        faces[face] = output
    return faces


def continuity_audit(faces: dict[str, Image.Image]) -> dict[str, object]:
    if set(faces) != set(FACE_ORDER):
        raise ValueError("cube audit needs all six named faces")
    sizes = {image.size for image in faces.values()}
    if len(sizes) != 1:
        raise ValueError("cube faces must have one size")
    width, height = next(iter(sizes))
    if width != height or width < 2:
        raise ValueError("cube faces must be square and at least 2x2")

    samples: dict[
        tuple[float, float, float],
        list[tuple[str, tuple[int, int, int, int]]],
    ] = defaultdict(list)
    for face in FACE_ORDER:
        face_pixels = image_pixels(faces[face].convert("RGBA"))
        for y in range(height):
            for x in range(width):
                if x not in {0, width - 1} and y not in {0, height - 1}:
                    continue
                direction = normalized_face_direction(face, x, y, width)
                key = tuple(round(value, 12) for value in direction)
                samples[key].append((face, face_pixels[y * width + x]))

    edge_deltas: dict[str, int] = defaultdict(int)
    corner_deltas: list[int] = []
    for shared in samples.values():
        if len(shared) not in {2, 3}:
            continue
        delta = max(
            abs(first[channel] - second[channel])
            for index, (_, first) in enumerate(shared)
            for _, second in shared[index + 1 :]
            for channel in range(4)
        )
        if len(shared) == 2:
            pair = "/".join(sorted((shared[0][0], shared[1][0])))
            edge_deltas[pair] = max(edge_deltas[pair], delta)
        else:
            corner_deltas.append(delta)
    return {
        "edge_pair_count": len(edge_deltas),
        "edge_max_channel_delta": max(edge_deltas.values(), default=0),
        "edge_pair_max_channel_delta": dict(sorted(edge_deltas.items())),
        "corner_count": len(corner_deltas),
        "corner_max_channel_delta": max(corner_deltas, default=0),
    }


def write_cube_faces(
    faces: dict[str, Image.Image],
    output_directory: pathlib.Path,
) -> dict[str, str]:
    output_directory.mkdir(parents=True, exist_ok=True)
    hashes: dict[str, str] = {}
    for face in FACE_ORDER:
        path = output_directory / f"{face}.png"
        faces[face].save(
            path,
            format="PNG",
            optimize=False,
            compress_level=9,
        )
        hashes[path.name] = sha256_file(path)
    return hashes


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=pathlib.Path)
    parser.add_argument("output_directory", type=pathlib.Path)
    parser.add_argument("--size", type=int, default=512)
    parser.add_argument("--expect-sha256")
    args = parser.parse_args()

    source_hash = sha256_file(args.source)
    if (
        args.expect_sha256 is not None
        and source_hash.lower() != args.expect_sha256.lower()
    ):
        raise SystemExit(
            "source SHA-256 mismatch: "
            f"expected {args.expect_sha256.lower()}, got {source_hash}"
        )
    with Image.open(args.source) as panorama:
        faces = build_cube_faces(panorama, args.size)
    result = {
        "source": str(args.source),
        "source_sha256": source_hash,
        "face_size": args.size,
        "face_order": list(FACE_ORDER),
        "continuity": continuity_audit(faces),
        "outputs": write_cube_faces(faces, args.output_directory),
    }
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
