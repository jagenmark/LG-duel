#!/usr/bin/env python3
"""Apply and verify the approved Overkill and Thunderstruck art pass."""

from __future__ import annotations

import argparse
import re
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OVERKILL = ROOT / "maps" / "overkill_import.map"
THUNDERSTRUCK = ROOT / "maps" / "thunderstruck.map"
DEFAULT_BASE = "13c1c75"

FACE_RE = re.compile(
    r"^(\( [^)]+ \) \( [^)]+ \) \( [^)]+ \) )(\S+)( .*)$"
)
ENTITY_RE = re.compile(r"^// entity (\d+)$")
BRUSH_RE = re.compile(r"^// brush (\d+)$")
PROPERTY_RE = re.compile(r'^"([^"]+)" "(.*)"$')

BRICK_25 = "Tiny/Bricks/Bricks_25-128x128"
BRICK_09 = "Tiny3/Brick/Brick_09-128x128"
BRICK_11 = "Tiny3/Brick/Brick_11-128x128"
BRICK_13 = "Tiny3/Brick/Brick_13-128x128"

WHOLE_GREY_BRUSHES = {
    1, 2, 3, 10, 13, *range(24, 33), 62, 63, 71, 91,
}
WORLD_STAIR_BRUSHES = {147, 153, 159, 166, 170, 171, 174, 178}
ENTITY_STAIR_BRUSHES = {
    3: {0, 4, 8, 11},
    6: {0, 4, 8, 10},
}

OVERKILL_LIGHTS = {
    None: {
        "classname": "light_sun",
        "direction": "0.35 -0.5 -1",
        "color": "255 226 184",
        "intensity": "0.85",
    },
    "-80 80 680": {
        "classname": "light",
        "origin": "-80 80 680",
        "color": "255 218 170",
        "intensity": "0.8",
        "radius": "1400",
    },
    "640 1050 1050": {
        "classname": "light",
        "origin": "640 1050 1050",
        "color": "188 214 255",
        "intensity": "0.75",
        "radius": "1300",
    },
    "640 -1350 680": {
        "classname": "light",
        "origin": "640 -1350 680",
        "color": "255 205 150",
        "intensity": "0.75",
        "radius": "1400",
    },
    "-1100 -100 560": {
        "classname": "light",
        "origin": "-1100 -100 560",
        "color": "196 220 255",
        "intensity": "0.7",
        "radius": "1200",
    },
    "1050 650 600": {
        "classname": "light",
        "origin": "1050 650 600",
        "color": "255 216 164",
        "intensity": "0.7",
        "radius": "1200",
    },
    "1184 2080 520": {
        "classname": "light",
        "origin": "1184 2080 520",
        "color": "255 174 82",
        "intensity": "0.9",
        "radius": "900",
    },
}

OVERKILL_FILL_LIGHTS = (
    {
        "classname": "light",
        "origin": "0 -360 440",
        "color": "168 202 255",
        "intensity": "1.1",
        "radius": "1600",
    },
    {
        "classname": "light",
        "origin": "640 360 760",
        "color": "176 208 255",
        "intensity": "1.05",
        "radius": "1600",
    },
    {
        "classname": "light",
        "origin": "640 -1040 440",
        "color": "185 210 255",
        "intensity": "1.0",
        "radius": "1500",
    },
    {
        "classname": "light",
        "origin": "-1300 -920 400",
        "color": "190 216 255",
        "intensity": "1.1",
        "radius": "1200",
    },
    {
        "classname": "light",
        "origin": "1184 1760 360",
        "color": "190 214 255",
        "intensity": "0.75",
        "radius": "1000",
    },
)

THUNDERSTRUCK_LIGHTS = {
    "56 -48 -976": ("255 226 196", "0.45", "720"),
    "-720 16 -888": ("235 240 246", "0.35", "800"),
    "-172 -720 -200": ("235 240 246", "0.30", "2400"),
    "456 -1032 -1010": ("255 220 180", "0.50", "900"),
    "-216 -392 -882": ("245 235 218", "0.40", "900"),
    "-472 -1048 -770": ("255 220 180", "0.45", "1000"),
}

THUNDERSTRUCK_AMBIENT_INTENSITY = "0.42"
THUNDERSTRUCK_SUN_INTENSITY = "0.85"
THUNDERSTRUCK_SHADOW_LIGHTS = {
    "56 -48 -976": {
        "casts_shadows": "1",
        "source_radius": "64",
        "priority": "20",
    },
    "-216 -392 -882": {
        "casts_shadows": "1",
        "source_radius": "64",
        "priority": "20",
    },
}


def split_lines(text: str) -> list[str]:
    return text.replace("\r\n", "\n").splitlines()


def join_lines(lines: list[str]) -> str:
    return "\n".join(lines) + "\n"


def entity_ranges(lines: list[str]) -> list[tuple[int, int]]:
    ranges: list[tuple[int, int]] = []
    depth = 0
    start = -1
    for index, line in enumerate(lines):
        if line == "{":
            if depth == 0:
                start = index
            depth += 1
        elif line == "}":
            depth -= 1
            if depth == 0 and start >= 0:
                ranges.append((start, index + 1))
                start = -1
    if depth != 0:
        raise AssertionError("unbalanced map braces")
    return ranges


def properties(lines: list[str], start: int, end: int) -> dict[str, str]:
    result: dict[str, str] = {}
    for line in lines[start:end]:
        match = PROPERTY_RE.match(line)
        if match:
            result[match.group(1)] = match.group(2)
    return result


def replace_light_entity(
    lines: list[str],
    start: int,
    end: int,
    desired: dict[str, str],
) -> list[str]:
    opening = next(index for index in range(start, end) if lines[index] == "{")
    closing = max(index for index in range(start, end) if lines[index] == "}")
    order = (
        "classname", "origin", "direction", "color", "intensity", "radius",
        "casts_shadows", "source_radius", "priority",
    )
    body = [f'"{key}" "{desired[key]}"' for key in order if key in desired]
    return lines[: opening + 1] + body + lines[closing:]


def light_entity_lines(desired: dict[str, str]) -> list[str]:
    order = (
        "classname", "origin", "direction", "color", "intensity", "radius",
        "casts_shadows", "source_radius", "priority",
    )
    return ["{", *(f'"{key}" "{desired[key]}"' for key in order if key in desired), "}"]


def transform_overkill(text: str) -> str:
    lines = split_lines(text)
    found: set[str | None] = set()
    for start, end in reversed(entity_ranges(lines)):
        props = properties(lines, start, end)
        classname = props.get("classname")
        if classname == "light_sun":
            key = None
        elif classname == "light":
            key = props.get("origin")
        else:
            continue
        if key not in OVERKILL_LIGHTS:
            continue
        if key in found:
            raise AssertionError(f"duplicate Overkill light target: {key}")
        found.add(key)
        lines = replace_light_entity(lines, start, end, OVERKILL_LIGHTS[key])
    if found != set(OVERKILL_LIGHTS):
        raise AssertionError(f"missing Overkill light targets: {set(OVERKILL_LIGHTS) - found}")
    for desired in OVERKILL_FILL_LIGHTS:
        lines.extend(light_entity_lines(desired))
    return join_lines(lines)


def face_z_values(line: str) -> tuple[float, float, float]:
    match = FACE_RE.match(line)
    if not match:
        raise AssertionError(f"not a face: {line}")
    points = re.findall(r"\( ([^)]+) \)", match.group(1))
    return tuple(float(point.split()[2]) for point in points)  # type: ignore[return-value]


def replace_face_texture(line: str, expected: str, replacement: str) -> str:
    match = FACE_RE.match(line)
    if not match or match.group(2) != expected:
        raise AssertionError(f"unexpected face material: {line}")
    return match.group(1) + replacement + match.group(3)


def transform_thunderstruck_materials(text: str) -> tuple[str, dict[str, int]]:
    lines = split_lines(text)
    entity = -1
    brush = -1
    brush_faces: dict[tuple[int, int], list[int]] = {}
    for index, line in enumerate(lines):
        match = ENTITY_RE.match(line)
        if match:
            entity = int(match.group(1))
            brush = -1
        match = BRUSH_RE.match(line)
        if match:
            brush = int(match.group(1))
        if FACE_RE.match(line):
            brush_faces.setdefault((entity, brush), []).append(index)

    counts = {"grey": 0, "stair": 0, "roof_shell": 0, "deep_roof": 0}
    for brush_id in WHOLE_GREY_BRUSHES:
        for index in brush_faces[(0, brush_id)]:
            lines[index] = replace_face_texture(lines[index], BRICK_25, BRICK_11)
            counts["grey"] += 1

    stair_targets = [(0, brush_id) for brush_id in WORLD_STAIR_BRUSHES]
    stair_targets += [
        (entity_id, brush_id)
        for entity_id, brushes in ENTITY_STAIR_BRUSHES.items()
        for brush_id in brushes
    ]
    for target in stair_targets:
        faces = brush_faces[target]
        horizontal = [
            index for index in faces
            if len(set(face_z_values(lines[index]))) == 1
        ]
        top_z = max(face_z_values(lines[index])[0] for index in horizontal)
        top_faces = [
            index for index in horizontal
            if face_z_values(lines[index])[0] == top_z
        ]
        if len(top_faces) != 1:
            raise AssertionError(f"expected one upward face for e{target[0]} b{target[1]}")
        index = top_faces[0]
        lines[index] = replace_face_texture(lines[index], BRICK_25, BRICK_13)
        counts["stair"] += 1

    for index in brush_faces[(2, 0)]:
        lines[index] = replace_face_texture(
            lines[index],
            "Circular/Square/Roofs/Square_Roofs_24-128x128",
            BRICK_13,
        )
        counts["roof_shell"] += 1

    for index in brush_faces[(0, 136)]:
        lines[index] = replace_face_texture(
            lines[index],
            "Circular/Square/Roofs/Square_Roofs_21-128x128",
            BRICK_09,
        )
        counts["deep_roof"] += 1

    expected = {"grey": 105, "stair": 16, "roof_shell": 6, "deep_roof": 9}
    if counts != expected:
        raise AssertionError(f"material counts {counts}, expected {expected}")
    return join_lines(lines), counts


def transform_thunderstruck_lights(text: str) -> str:
    lines = split_lines(text)
    found: set[str] = set()
    insert_at = None
    ambient_insert_at = None
    for start, end in entity_ranges(lines):
        props = properties(lines, start, end)
        if props.get("classname") != "worldspawn":
            continue
        for index in range(start + 1, end):
            match = PROPERTY_RE.match(lines[index])
            if match and match.group(1) == "lg_bounds_max":
                ambient_insert_at = index + 1
                break
        break
    if ambient_insert_at is None:
        raise AssertionError("Thunderstruck worldspawn bounds insertion point not found")
    lines.insert(
        ambient_insert_at,
        f'"lg_ambient_intensity" "{THUNDERSTRUCK_AMBIENT_INTENSITY}"',
    )
    for start, end in reversed(entity_ranges(lines)):
        props = properties(lines, start, end)
        origin = props.get("origin")
        if props.get("classname") != "light_point" or origin not in THUNDERSTRUCK_LIGHTS:
            continue
        color, intensity, radius = THUNDERSTRUCK_LIGHTS[origin]
        desired = {
            "classname": "light_point",
            "origin": origin,
            "color": color,
            "intensity": intensity,
            "radius": radius,
        }
        desired.update(THUNDERSTRUCK_SHADOW_LIGHTS.get(origin, {}))
        found.add(origin)
        lines = replace_light_entity(lines, start, end, desired)
    if found != set(THUNDERSTRUCK_LIGHTS):
        raise AssertionError(
            f"missing Thunderstruck light targets: {set(THUNDERSTRUCK_LIGHTS) - found}"
        )

    for start, end in entity_ranges(lines):
        props = properties(lines, start, end)
        if props.get("classname") == "func_group" and props.get("_tb_name") == "Spawn":
            insert_at = start
            if start > 0 and ENTITY_RE.match(lines[start - 1]):
                insert_at = start - 1
            break
    if insert_at is None:
        raise AssertionError("Thunderstruck Spawn group insertion point not found")
    sun = [
        "// art light_sun",
        "{",
        '"classname" "light_sun"',
        '"direction" "0.25 -0.45 -1"',
        '"color" "235 240 246"',
        f'"intensity" "{THUNDERSTRUCK_SUN_INTENSITY}"',
        "}",
    ]
    lines[insert_at:insert_at] = sun
    return join_lines(lines)


def transform_thunderstruck(text: str) -> tuple[str, dict[str, int]]:
    material_text, counts = transform_thunderstruck_materials(text)
    return transform_thunderstruck_lights(material_text), counts


def git_text(path: Path, base: str) -> str:
    relative = path.relative_to(ROOT).as_posix()
    result = subprocess.run(
        ["git", "show", f"{base}:{relative}"],
        cwd=ROOT,
        check=True,
        capture_output=True,
    )
    return result.stdout.decode("utf-8")


def plane_points(text: str) -> list[str]:
    return [
        match.group(1)
        for line in split_lines(text)
        if (match := FACE_RE.match(line))
    ]


def gameplay_origins(text: str) -> list[str]:
    lines = split_lines(text)
    result: list[str] = []
    for start, end in entity_ranges(lines):
        props = properties(lines, start, end)
        if props.get("classname") in {"light", "light_sun"}:
            continue
        if origin := props.get("origin"):
            result.append(origin)
    return result


def unresolved_textures(text: str) -> list[str]:
    missing = set()
    for line in split_lines(text):
        match = FACE_RE.match(line)
        if not match:
            continue
        texture = match.group(2).replace("\\", "/")
        if texture.lower().startswith("common/"):
            continue
        if not (ROOT / "textures" / f"{texture}.png").is_file():
            missing.add(texture)
    return sorted(missing)


def verify(base: str) -> dict[str, int]:
    overkill_base = git_text(OVERKILL, base)
    thunder_base = git_text(THUNDERSTRUCK, base)
    expected_overkill = transform_overkill(overkill_base)
    expected_thunder, counts = transform_thunderstruck(thunder_base)
    actual_overkill = OVERKILL.read_text(encoding="utf-8")
    actual_thunder = THUNDERSTRUCK.read_text(encoding="utf-8")

    if split_lines(actual_overkill) != split_lines(expected_overkill):
        raise AssertionError("Overkill differs from the exact approved light pass")
    if split_lines(actual_thunder) != split_lines(expected_thunder):
        raise AssertionError("Thunderstruck differs from the exact approved art pass")
    if plane_points(overkill_base) != plane_points(actual_overkill):
        raise AssertionError("Overkill plane points changed")
    if plane_points(thunder_base) != plane_points(actual_thunder):
        raise AssertionError("Thunderstruck plane points changed")
    if gameplay_origins(overkill_base) != gameplay_origins(actual_overkill):
        raise AssertionError("Overkill gameplay origins changed")
    if gameplay_origins(thunder_base) != gameplay_origins(actual_thunder):
        raise AssertionError("Thunderstruck gameplay origins changed")

    missing = unresolved_textures(actual_overkill) + unresolved_textures(actual_thunder)
    if missing:
        raise AssertionError(f"unresolved non-common textures: {sorted(set(missing))}")
    return counts


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--base",
        default=DEFAULT_BASE,
        help=f"read-only source revision for the approved maps (default: {DEFAULT_BASE})",
    )
    args = parser.parse_args()
    counts = verify(args.base)
    print(
        "map art pass verified: "
        f"{sum(counts.values())} Thunderstruck faces "
        f"({counts}), {len(OVERKILL_LIGHTS) + len(OVERKILL_FILL_LIGHTS)} "
        "Overkill lights, 6 Thunderstruck point lights, 1 sun"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
