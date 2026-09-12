#!/usr/bin/env python3
"""Bounded, explicitly attached visual review of player animations."""

from __future__ import annotations

import argparse
import html
import json
import math
import shutil
import sys
import uuid
from pathlib import Path
from typing import Any

import lg_control
from lg_playtest import PlaytestSession


POSES = ("stand", "crouch", "crouch_walk", "slide", "jump", "land")
VIEWS = ("front", "side")
REVIEW_CVARS = ("r_dev_camera_draw_connected_body", "r_draw_remote_players", "r_show_collision")


class ReviewAssertionError(RuntimeError):
    pass


def _csv(value: str, allowed: tuple[str, ...], label: str) -> list[str]:
    values = [part.strip() for part in value.split(",") if part.strip()]
    if not values or any(part not in allowed for part in values) or len(values) != len(set(values)):
        raise argparse.ArgumentTypeError(f"{label} must be a comma-separated subset of {','.join(allowed)}")
    return values


def parse_poses(value: str) -> list[str]:
    return _csv(value, POSES, "poses")


def parse_views(value: str) -> list[str]:
    return _csv(value, VIEWS, "views")


def pose_input(name: str) -> tuple[list[dict[str, Any]], int, dict[str, bool]]:
    if name == "stand":
        return ([{"ticks": 80}], 40, {"on_ground": True})
    if name == "crouch":
        return ([{"ticks": 100, "crouch": True}], 60, {"crouched": True})
    if name == "crouch_walk":
        return ([{"ticks": 100, "forward": 0.3, "crouch": True}], 60, {"crouched": True})
    if name == "slide":
        return ([{"ticks": 45, "forward": 1}, {"ticks": 100, "forward": 1, "crouch": True}], 80, {"crouched": True})
    if name == "jump":
        return ([{"ticks": 1, "jump": True}, {"ticks": 80}], 15, {"on_ground": False})
    if name == "land":
        return ([{"ticks": 1, "jump": True}, {"ticks": 160}], 90, {"on_ground": True})
    raise ValueError(f"unknown pose {name}")


def _player_position(state: dict[str, Any]) -> list[float]:
    player = state.get("predicted_local_player") or {}
    position = player.get("position")
    if not isinstance(position, list) or len(position) != 3:
        raise lg_control.ControlError("get_client_state did not return a local player position")
    return [float(value) for value in position]


def camera_for(position: list[float], view: str) -> dict[str, Any]:
    angle = math.radians(45.0 if view == "front" else 0.0)
    camera = [position[0] + 4.5 * math.cos(angle), position[1] + 4.5 * math.sin(angle), position[2] + 0.8]
    yaw = math.degrees(math.atan2(position[1] - camera[1], position[0] - camera[0]))
    return {"position": camera, "yaw": yaw, "pitch": -10.0, "fov": 45.0}


def _state_assertions(frame_state: dict[str, Any], expected: dict[str, bool]) -> list[str]:
    failures: list[str] = []
    if frame_state.get("body_visible") is not True:
        failures.append("body_visible: expected True")
    if frame_state.get("development_camera") is not True:
        failures.append("development_camera: expected True")
    player = frame_state.get("rendered_player") or {}
    for field, wanted in expected.items():
        actual = player.get(field)
        if not isinstance(actual, bool):
            failures.append(f"{field}: unavailable")
        elif actual != wanted:
            failures.append(f"{field}: expected {wanted}, got {actual}")
    return failures


def _write_evidence(directory: Path, result: dict[str, Any]) -> None:
    directory.mkdir(parents=True, exist_ok=True)
    rows: list[str] = []
    for index, shot in enumerate(result.get("shots", [])):
        capture = shot.get("capture") or {}
        picture = ""
        source = capture.get("path")
        if source and Path(source).is_file():
            filename = f"{index:03d}.png"
            shutil.copyfile(source, directory / filename)
            shot["source_path"] = str(source)
            shot["evidence_path"] = str((directory / filename).resolve())
            picture = f'<a href="{filename}"><img src="{filename}" alt="{html.escape(shot.get("label", "capture"))}"></a>'
        detail = html.escape(json.dumps(shot, indent=2, sort_keys=True))
        rows.append(f'<article><h2>{html.escape(shot.get("label", "capture"))}</h2>{picture}<pre>{detail}</pre></article>')
    (directory / "result.json").write_text(json.dumps(result, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    (directory / "index.html").write_text(
        '<!doctype html><meta charset="utf-8"><title>LG Duel animation review</title>'
        '<style>body{font:16px system-ui;background:#111923;color:#e9eff8;max-width:1100px;margin:32px auto;padding:0 24px}img{width:100%}article{margin:32px 0}pre{white-space:pre-wrap;background:#1c2938;padding:16px}</style>'
        '<h1>LG Duel animation review</h1><p>Bounded visual evidence from fixed camera captures. Labels and state assertions describe coverage; they do not certify aesthetic quality.</p>'
        + f'<p>Outcome: {html.escape(result.get("outcome", "incomplete"))}</p>' + "".join(rows), encoding="utf-8"
    )


def run_review(args: argparse.Namespace) -> dict[str, Any]:
    if isinstance(args.port, bool) or not isinstance(args.port, int) or not 1 <= args.port <= 65535:
        raise lg_control.ControlError("port must be an integer from 1 to 65535")
    if not isinstance(args.timeout, (int, float)) or isinstance(args.timeout, bool) or not math.isfinite(args.timeout) or not 1 <= args.timeout <= 30:
        raise lg_control.ControlError("timeout must be a finite number from 1 to 30 seconds")
    if not args.poses or any(p not in POSES for p in args.poses) or len(args.poses) != len(set(args.poses)):
        raise lg_control.ControlError("poses must be a non-empty unique subset of the predefined poses")
    if not args.views or any(v not in VIEWS for v in args.views) or len(args.views) != len(set(args.views)):
        raise lg_control.ControlError("views must be a non-empty unique subset of front,side")
    if args.collision_debug is not None and (isinstance(args.collision_debug, bool) or not isinstance(args.collision_debug, int) or not 0 <= args.collision_debug <= 5):
        raise lg_control.ControlError("collision debug mode must be an integer from 0 to 5")
    output = Path(args.output).expanduser().resolve()
    if output.exists() and any(output.iterdir()):
        raise lg_control.ControlError(f"output directory is not empty: {output}")
    session = PlaytestSession(args.port, session_id=args.session_id, timeout=args.timeout,
                              expected_map="glide_duel", expected_phase="WAITING FOR PLAYERS")
    original_camera = session.request("get_camera")
    original_status = session.request("status")
    original_cvars = {name: session.request("get_cvar", name=name).get("value", "") for name in REVIEW_CVARS}
    result: dict[str, Any] = {"version": 1, "outcome": "recorded", "control_port": args.port,
                              "control_session_id": session.session_id, "poses": args.poses,
                              "views": args.views, "shots": []}
    try:
        session.request("set_cvar", name="r_dev_camera_draw_connected_body", value="1")
        session.request("set_cvar", name="r_draw_remote_players", value="1")
        if args.collision_debug is not None:
            session.request("set_cvar", name="r_show_collision", value=str(args.collision_debug))
        for pose in args.poses:
            segments, capture_tick, expected = pose_input(pose)
            total_ticks = sum(segment["ticks"] for segment in segments)
            if capture_tick >= total_ticks:
                raise lg_control.ControlError(f"pose {pose} capture tick must be before final release")
            for view in args.views:
                session.request("reload_map")
                session.request("exec_console", command="resetmatch")
                state = session.request("get_client_state")
                session.request("set_camera", **camera_for(_player_position(state), view))
                name = f"animation-{pose}-{view}-{uuid.uuid4().hex[:8]}"
                input_segments = [dict(segment) for segment in segments]
                input_segments[0]["yaw"] = 90.0
                input_segments[0]["pitch"] = 0.0
                session.request("arm_phase_capture", name=name, phase="input_tick",
                                at_input_tick=capture_tick, hide_hud=True, hide_overlays=True)
                response = session.request("send_input_sequence", segments=input_segments)
                capture = session.request("collect_phase_capture", name=name)
                frame = capture.get("frame_state") or {}
                failures = _state_assertions(frame, expected)
                result["shots"].append({"label": f"{pose} / {view}", "pose": pose, "view": view,
                                        "input": input_segments, "requested_input_tick": capture_tick,
                                        "input_result": response, "capture": capture,
                                        "assertion_failures": failures})
                if failures:
                    result["outcome"] = "failed"
                    raise ReviewAssertionError("; ".join(failures))
        if result["outcome"] == "recorded":
            result["outcome"] = "reviewed"
    except lg_control.ControlError as error:
        result["outcome"] = "incomplete"
        result["error"] = str(error)
    except ReviewAssertionError as error:
        result["error"] = str(error)
    finally:
        if not session.broken:
            try:
                for name, value in original_cvars.items():
                    session.request("set_cvar", name=name, value=value)
                if original_status.get("development_camera") is True:
                    camera = {key: original_camera[key] for key in ("position", "yaw", "pitch") if key in original_camera}
                    if original_camera.get("fov") is not None:
                        camera["fov"] = original_camera["fov"]
                    session.request("set_camera", **camera)
                else:
                    session.request("set_player_view", yaw=original_camera.get("yaw", 0),
                                    pitch=original_camera.get("pitch", 0))
            except lg_control.ControlError as error:
                result["outcome"] = "incomplete"
                result["restoration_error"] = str(error)
        result["restored"] = not session.broken and "restoration_error" not in result
        result["initial_status"] = original_status
        _write_evidence(output, result)
    return result


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Review bounded player animation captures")
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--session-id")
    parser.add_argument("--timeout", type=float, default=30.0)
    parser.add_argument("--poses", type=parse_poses, default=list(POSES))
    parser.add_argument("--views", type=parse_views, default=list(VIEWS))
    parser.add_argument("--output", required=True)
    parser.add_argument("--collision-debug", type=lg_control.bounded_integer(0, 5))
    return parser


def main(argv: list[str] | None = None) -> int:
    try:
        args = build_parser().parse_args(argv)
        result = run_review(args)
        print(json.dumps({key: result.get(key) for key in ("outcome", "restored", "error")}, indent=2))
        return 0 if result["outcome"] == "reviewed" else 1
    except (lg_control.ControlError, OSError, ValueError) as error:
        print(f"animation review failed: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
