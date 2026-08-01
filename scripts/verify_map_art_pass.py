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
DEFAULT_BASE = "471937bda917bd2c48fc9bececae8f2126009535"

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
    "-80 80 712": {
        "classname": "light",
        "origin": "-80 80 712",
        "color": "255 218 170",
        "intensity": "0.8",
        "radius": "1400",
    },
    "640 1050 1082": {
        "classname": "light",
        "origin": "640 1050 1082",
        "color": "188 214 255",
        "intensity": "0.75",
        "radius": "1300",
    },
    "640 -1350 712": {
        "classname": "light",
        "origin": "640 -1350 712",
        "color": "255 205 150",
        "intensity": "0.75",
        "radius": "1400",
    },
    "-1100 -100 592": {
        "classname": "light",
        "origin": "-1100 -100 592",
        "color": "196 220 255",
        "intensity": "0.7",
        "radius": "1200",
    },
    "1050 650 632": {
        "classname": "light",
        "origin": "1050 650 632",
        "color": "255 216 164",
        "intensity": "0.7",
        "radius": "1200",
    },
    "1184 2080 552": {
        "classname": "light",
        "origin": "1184 2080 552",
        "color": "255 174 82",
        "intensity": "0.9",
        "radius": "900",
    },
}

OVERKILL_FILL_LIGHTS = (
    {
        "classname": "light",
        "origin": "0 -360 472",
        "color": "168 202 255",
        "intensity": "1.1",
        "radius": "1600",
    },
    {
        "classname": "light",
        "origin": "640 360 792",
        "color": "176 208 255",
        "intensity": "1.05",
        "radius": "1600",
    },
    {
        "classname": "light",
        "origin": "640 -1040 472",
        "color": "185 210 255",
        "intensity": "1.0",
        "radius": "1500",
    },
    {
        "classname": "light",
        "origin": "-1300 -920 432",
        "color": "190 216 255",
        "intensity": "1.1",
        "radius": "1200",
    },
    {
        "classname": "light",
        "origin": "1184 1760 392",
        "color": "190 214 255",
        "intensity": "0.75",
        "radius": "1000",
    },
)

THUNDERSTRUCK_LIGHTS = {
    "56 -48 -944": ("255 226 196", "0.45", "720"),
    "-720 16 -856": ("235 240 246", "0.35", "800"),
    "-172 -720 -168": ("235 240 246", "0.30", "2400"),
    "456 -1032 -978": ("255 220 180", "0.50", "900"),
    "-216 -392 -850": ("245 235 218", "0.40", "900"),
    "-472 -1048 -738": ("255 220 180", "0.45", "1000"),
}

THUNDERSTRUCK_AMBIENT_INTENSITY = "0.42"
THUNDERSTRUCK_SUN_INTENSITY = "0.85"
THUNDERSTRUCK_SHADOW_LIGHTS = {
    "56 -48 -944": {
        "casts_shadows": "1",
        "source_radius": "64",
        "priority": "20",
    },
    "-216 -392 -850": {
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
        if props.get("classname") in {"light", "light_point", "light_sun"}:
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


LIGHT_EXPECTATIONS = {
    "thunderstruck": [
        ("56 -48 -944", "1.10", "560", True, "10", "100"),
        ("-720 16 -856", "0.75", "720", False, "0", "0"),
        ("-172 -720 -168", "0.65", "1500", False, "0", "0"),
        ("456 -1032 -978", "1.05", "650", False, "0", "0"),
        ("-216 -392 -850", "1.00", "560", True, "10", "90"),
        ("-472 -1048 -738", "0.90", "720", False, "0", "0"),
    ],
    "overkill": [
        ("-80 80 712", "1.25", "760", True, "10", "100"),
        ("640 1050 1082", "1.00", "850", False, "0", "0"),
        ("640 -1350 712", "1.00", "900", False, "0", "0"),
        ("-1100 -100 592", "0.95", "820", False, "0", "0"),
        ("1050 650 632", "1.00", "820", False, "0", "0"),
        ("1184 2080 552", "1.35", "620", True, "10", "90"),
        ("0 -360 472", "1.30", "950", False, "0", "0"),
        ("640 360 792", "1.25", "950", False, "0", "0"),
        ("640 -1040 472", "1.25", "900", False, "0", "0"),
        ("-1300 -920 432", "1.30", "800", False, "0", "0"),
        ("1184 1760 392", "1.00", "720", False, "0", "0"),
    ],
}

FIXTURE_PREFIXES = {
    "thunderstruck": [
        ("thunderstruck-central", "56 -48 -944"),
        ("thunderstruck-west", "-720 16 -856"),
        ("thunderstruck-upper", "-172 -720 -168"),
        ("thunderstruck-south", "456 -1032 -978"),
        ("thunderstruck-mid", "-216 -392 -850"),
        ("thunderstruck-east", "-472 -1048 -738"),
    ],
    "overkill": [
        ("overkill-west", "-80 80 712"),
        ("overkill-north", "640 1050 1082"),
        ("overkill-south", "640 -1350 712"),
        ("overkill-northwest", "-1100 -100 592"),
        ("overkill-east", "1050 650 632"),
        ("overkill-teleport", "1184 2080 552"),
        ("overkill-center-west", "0 -360 472"),
        ("overkill-center", "640 360 792"),
        ("overkill-center-south", "640 -1040 472"),
        ("overkill-southwest", "-1300 -920 432"),
        ("overkill-exit", "1184 1760 392"),
    ],
}


def entity_properties(text: str) -> list[tuple[dict[str, str], list[str]]]:
    lines = split_lines(text)
    result: list[tuple[dict[str, str], list[str]]] = []
    for start, end in entity_ranges(lines):
        result.append((
            properties(lines, start, end),
            lines[start:end],
        ))
    return result


def verify_lights(
    text: str,
    expected: list[tuple[str, str, str, bool, str, str]],
) -> None:
    entities = entity_properties(text)
    by_origin: dict[str, list[dict[str, str]]] = {}
    for props, _ in entities:
        if props.get("classname") in {"light", "light_point"}:
            by_origin.setdefault(props.get("origin", ""), []).append(props)
    for origin, intensity, radius, shadows, source_radius, priority in expected:
        matches = by_origin.get(origin, [])
        if len(matches) != 1:
            raise AssertionError(
                f"expected one authored point light at {origin}, found {len(matches)}"
            )
        props = matches[0]
        if (
            props.get("intensity") != intensity or
            props.get("radius") != radius or
            (props.get("casts_shadows", "0") == "1") != shadows or
            props.get("source_radius", "0") != source_radius or
            props.get("priority", "0") != priority
        ):
            raise AssertionError(f"light at {origin} does not match explicit light tuning")


def verify_fixtures(
    text: str,
    name: str,
    expected: list[tuple[str, str]],
) -> int:
    expected_origins = {origin for _, origin in expected}
    expected_ids = {
        f"{prefix}-{part}"
        for prefix, _ in expected
        for part in ("housing", "lens")
    }
    entities = entity_properties(text)
    seen_ids: set[str] = set()
    source_locators: set[tuple[str, str]] = set()
    fixture_count = 0
    for props, lines in entities:
        visual_id = props.get("lg_adaptation_visual_id", "")
        if visual_id not in expected_ids:
            continue
        if (
            props.get("classname") != "func_group" or
            props.get("lg_geometry_role") != "render_only"
        ):
            raise AssertionError(f"{name} fixture {visual_id} is not render-only")
        if visual_id in seen_ids:
            raise AssertionError(f"duplicate {name} fixture id: {visual_id}")
        seen_ids.add(visual_id)
        origin = props.get("lg_light_origin", "")
        if origin not in expected_origins:
            raise AssertionError(f"{name} fixture {visual_id} has no authored light")
        locator = (
            props.get("lg_source_entity_index", ""),
            props.get("lg_source_brush_index", ""),
        )
        if "" in locator or locator in source_locators:
            raise AssertionError(f"duplicate or missing source locator on {visual_id}")
        source_locators.add(locator)
        materials = {
            match.group(2)
            for line in lines
            if (match := FACE_RE.match(line))
        }
        if visual_id.endswith("-housing"):
            required = "Overkill/Overkill_Oxidized_Trim-128x128"
        else:
            required = "Overkill/Overkill_Amber_Route-128x128"
        if required not in materials:
            raise AssertionError(f"{name} fixture {visual_id} has no {required} face")
        fixture_count += 1
    if seen_ids != expected_ids:
        raise AssertionError(
            f"{name} fixtures differ from the authored light list: "
            f"missing={sorted(expected_ids - seen_ids)} "
            f"extra={sorted(seen_ids - expected_ids)}"
        )
    return fixture_count


def verify(base: str) -> dict[str, int]:
    overkill_base = git_text(OVERKILL, base)
    thunder_base = git_text(THUNDERSTRUCK, base)
    actual_overkill = OVERKILL.read_text(encoding="utf-8")
    actual_thunder = THUNDERSTRUCK.read_text(encoding="utf-8")

    for name, original, actual in (
        ("Overkill", overkill_base, actual_overkill),
        ("Thunderstruck", thunder_base, actual_thunder),
    ):
        original_planes = plane_points(original)
        actual_planes = plane_points(actual)
        if actual_planes[:len(original_planes)] != original_planes:
            raise AssertionError(f"{name} source plane points changed")
        if gameplay_origins(original) != gameplay_origins(actual):
            raise AssertionError(f"{name} gameplay origins changed")

    verify_lights(actual_thunder, LIGHT_EXPECTATIONS["thunderstruck"])
    verify_lights(actual_overkill, LIGHT_EXPECTATIONS["overkill"])
    thunder_fixtures = verify_fixtures(
        actual_thunder,
        "Thunderstruck",
        FIXTURE_PREFIXES["thunderstruck"],
    )
    overkill_fixtures = verify_fixtures(
        actual_overkill,
        "Overkill",
        FIXTURE_PREFIXES["overkill"],
    )

    missing = unresolved_textures(actual_overkill) + unresolved_textures(actual_thunder)
    if missing:
        raise AssertionError(f"unresolved non-common textures: {sorted(set(missing))}")
    return {
        "thunder_fixtures": thunder_fixtures,
        "overkill_fixtures": overkill_fixtures,
    }


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
        "explicit map lighting verified: "
        f"{counts['thunder_fixtures']} Thunderstruck fixture brushes, "
        f"{counts['overkill_fixtures']} Overkill fixture brushes, "
        "6 Thunderstruck point lights, 11 Overkill point lights"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
