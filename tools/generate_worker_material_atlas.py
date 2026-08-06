#!/usr/bin/env python3
"""Build the checked-in Worker albedo and linear packed-mask atlases.

The input is a generated palette reference held with the source materials.
This script makes the runtime images deterministic and avoids authoring any
lighting or high-frequency texture detail into the material set.
"""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path

from PIL import Image, ImageStat


ROOT = Path(__file__).resolve().parents[1]
MATERIALS = ROOT / "assets" / "models" / "quaternius_worker" / "materials"
REFERENCE = MATERIALS / "source" / "worker_albedo_grid_reference.png"
ALBEDO = MATERIALS / "worker_albedo.png"
MASK = MATERIALS / "worker_material_mask.png"
SIZE = 512
CELL = 128
PAD = 8

# Atlas cell -> source-reference cell. Reuse only repeats the same reviewed
# Worker material, such as Skin and Skin.001.
SOURCE_CELLS = {
    (0, 0): (0, 0),  # Skin
    (1, 0): (1, 0),  # Worker yellow shirt
    (2, 0): (2, 0),  # Worker vest
    (3, 0): (3, 0),  # Light brown gloves
    (0, 1): (0, 1),  # Grey hard accessories
    (1, 1): (1, 1),  # Black boots
    (2, 1): (2, 2),  # Brows and moustache
    (3, 1): (0, 2),  # Eyes
    (0, 2): (1, 3),  # Brown leather
    (1, 2): (3, 3),  # Brown leather shadow
}

# R team-tint, G roughness, B metallic, A emissive/reserved.
MASK_CELLS = {
    (0, 0): (0.00, 0.62, 0.00, 0.00),  # Skin
    (1, 0): (0.92, 0.82, 0.00, 0.00),  # Shirt: front and rear readable
    (2, 0): (1.00, 0.86, 0.00, 0.00),  # Vest: torso, side, and rear
    (3, 0): (0.00, 0.76, 0.00, 0.00),  # Gloves
    (0, 1): (0.00, 0.50, 0.18, 0.00),  # Genuine small metal accessories
    (1, 1): (0.00, 0.72, 0.00, 0.00),  # Boots
    (2, 1): (0.00, 0.82, 0.00, 0.00),  # Hair and facial hair
    (3, 1): (0.00, 0.45, 0.00, 0.00),  # Eyes
    (0, 2): (0.00, 0.63, 0.00, 0.00),  # Brown leather
    (1, 2): (0.00, 0.70, 0.00, 0.00),  # Brown leather shadow
}
DEFAULT_MASK = (0.00, 0.85, 0.00, 0.00)


def nonwhite_runs(image: Image.Image, axis: int) -> list[tuple[int, int]]:
    """Find the four coloured cell spans in the white-grid reference."""
    rgb = image.convert("RGB")
    length = rgb.width if axis == 0 else rgb.height
    other = rgb.height if axis == 0 else rgb.width
    marked: list[bool] = []
    for coord in range(length):
        found = False
        for secondary in range(other):
            pixel = rgb.getpixel((coord, secondary) if axis == 0 else (secondary, coord))
            if min(pixel) < 242:
                found = True
                break
        marked.append(found)
    runs: list[tuple[int, int]] = []
    start: int | None = None
    for index, value in enumerate(marked + [False]):
        if value and start is None:
            start = index
        elif not value and start is not None:
            runs.append((start, index))
            start = None
    # Ignore a one-pixel encoder artifact at the outer edge of some generated
    # references; an actual atlas cell is much wider than the padding.
    runs = [run for run in runs if run[1] - run[0] >= 32]
    if len(runs) != 4:
        raise ValueError(f"expected four reference cells per axis, found {runs}")
    return runs


def paste_padded(target: Image.Image, cell: Image.Image, column: int, row: int) -> None:
    inner = cell.resize((CELL - PAD * 2, CELL - PAD * 2), Image.Resampling.LANCZOS)
    average = tuple(int(round(value)) for value in ImageStat.Stat(inner).mean[:3])
    background = Image.new("RGBA", (CELL, CELL), average + (255,))
    background.paste(inner, (PAD, PAD))
    target.paste(background, (column * CELL, row * CELL))


def make_albedo() -> Image.Image:
    reference = Image.open(REFERENCE).convert("RGB")
    x_runs = nonwhite_runs(reference, 0)
    y_runs = nonwhite_runs(reference, 1)
    output = Image.new("RGBA", (SIZE, SIZE), (128, 128, 128, 255))
    for row in range(4):
        for column in range(4):
            source_column, source_row = SOURCE_CELLS.get((column, row), (1, 1))
            x0, x1 = x_runs[source_column]
            y0, y1 = y_runs[source_row]
            paste_padded(output, reference.crop((x0, y0, x1, y1)), column, row)
    return output


def make_mask() -> Image.Image:
    output = Image.new("RGBA", (SIZE, SIZE), (0, 217, 0, 0))
    for row in range(4):
        for column in range(4):
            values = MASK_CELLS.get((column, row), DEFAULT_MASK)
            rgba = tuple(int(round(max(0.0, min(1.0, value)) * 255.0)) for value in values)
            output.paste(Image.new("RGBA", (CELL, CELL), rgba), (column * CELL, row * CELL))
    return output


def image_hash(image: Image.Image) -> str:
    """Hash the pixels and image shape, independent of PNG compression."""
    canonical = image.convert("RGBA")
    digest = hashlib.sha256()
    digest.update(image.mode.encode("ascii"))
    digest.update(b"\0")
    digest.update(str(image.width).encode("ascii"))
    digest.update(b"x")
    digest.update(str(image.height).encode("ascii"))
    digest.update(b"\0")
    digest.update(canonical.tobytes())
    return digest.hexdigest()


def file_image_hash(path: Path) -> str:
    with Image.open(path) as image:
        return image_hash(image)


def build() -> None:
    if not REFERENCE.is_file():
        raise FileNotFoundError(f"missing albedo reference: {REFERENCE}")
    MATERIALS.mkdir(parents=True, exist_ok=True)
    make_albedo().save(ALBEDO, format="PNG", optimize=False)
    make_mask().save(MASK, format="PNG", optimize=False)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true", help="verify generated files are current")
    args = parser.parse_args()
    if args.check:
        if not ALBEDO.is_file() or not MASK.is_file():
            print("missing generated Worker material atlas")
            return 1
        if (
            file_image_hash(ALBEDO) != image_hash(make_albedo()) or
            file_image_hash(MASK) != image_hash(make_mask())
        ):
            print("Worker material atlas is stale; run tools/generate_worker_material_atlas.py")
            return 1
        print("Worker material atlas is current")
        return 0
    build()
    print(f"wrote {ALBEDO}")
    print(f"wrote {MASK}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
