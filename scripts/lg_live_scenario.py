#!/usr/bin/env python3
"""Run one C++-validated LG Duel scenario through a real client and server."""

from __future__ import annotations

import argparse
import json
import os
import shutil
import socket
import subprocess
import time
import uuid
from datetime import datetime, timezone
from pathlib import Path
from typing import Any
from xml.sax.saxutils import escape

import lg_control
import lg_launch


REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUTPUT_ROOT = REPO_ROOT / "build" / "scenario-results"
DEFAULT_BUILD_DIR = REPO_ROOT / "build" / "default"
BASE_SCHEDULE_LATE_TICKS = 32
MAX_CAPTURE_RECOVERY_TICKS = 93
# Direct state checks run on the client frame loop. Keep the original bounded
# dispatch recovery; exact-frame capture no longer needs a wider late limit.
# Captures block the client while the server keeps its fixed clock running.
# Reserve 1.5 server seconds per readback so every independent phase shot can
# still run. The runner removes the measured span from logical schedule checks.
CAPTURE_RUNTIME_PADDING_TICKS = 188
CLIENT_CVAR_OVERRIDE_RULES = {
    "r_combat_effects": {"0", "1", "2"},
    "r_bloom": {"0", "1"},
    "r_player_model": {"0", "1", "2"},
    "r_material_quality": {"0", "1", "2"},
    "r_draw_player_outlines": {"0", "1"},
    "r_player_outline_mode": {"0", "1", "2"},
    "r_show_weapons": {"0", "1"},
}


def _schedule_late_limit(capture_pause_ticks: int) -> int:
    """Allow extra dispatch time only after a measured capture pause."""
    measured_pause = max(0, int(capture_pause_ticks))
    return BASE_SCHEDULE_LATE_TICKS + min(
        measured_pause,
        MAX_CAPTURE_RECOVERY_TICKS,
    )


class LiveScenarioError(RuntimeError):
    """A live scenario did not reach a valid completed result."""


class LiveScenarioStageError(LiveScenarioError):
    def __init__(self, stage: str, detail: str) -> None:
        self.stage = stage
        super().__init__(f"{stage}: {detail}")


def _parse_client_cvar_override(value: str) -> tuple[str, str]:
    """Parse one narrow, presentation-only client cvar override."""
    name, separator, requested = value.partition("=")
    allowed = CLIENT_CVAR_OVERRIDE_RULES.get(name)
    if not separator or allowed is None or requested not in allowed:
        choices = ", ".join(
            f"{key}={option}"
            for key, values in CLIENT_CVAR_OVERRIDE_RULES.items()
            for option in sorted(values)
        )
        raise argparse.ArgumentTypeError(
            f"client cvar override must be one of: {choices}"
        )
    return name, requested


def _client_cvar_overrides(
    entries: list[tuple[str, str]] | None,
) -> dict[str, str]:
    overrides: dict[str, str] = {}
    for name, value in entries or []:
        if name in overrides:
            raise LiveScenarioStageError(
                "client_cvars",
                f"duplicate client cvar override: {name}",
            )
        overrides[name] = value
    return overrides


def _cvar_response_value(response: dict[str, Any]) -> str | None:
    value = response.get("value")
    if isinstance(value, bool):
        return "1" if value else "0"
    if isinstance(value, (str, int, float)) and not isinstance(value, bool):
        return str(value)
    return None


def _apply_client_cvar_overrides(
    session: dict[str, Any],
    timeout: float,
    requested: dict[str, str],
    attestation: dict[str, Any] | None = None,
) -> dict[str, Any]:
    """Set and read back owned-client presentation cvars before scenario start."""
    output = attestation if attestation is not None else {}
    records: list[dict[str, Any]] = []
    applied: dict[str, str] = {}
    output.update(
        {
            "requested": requested,
            "applied": applied,
            "records": records,
            "applied_before_scenario_start": False,
            "restore": "owned client exits during runner cleanup",
        }
    )
    for name, value in requested.items():
        before = _request("get_cvar", session, timeout, name=name)
        set_response = _request(
            "set_cvar",
            session,
            timeout,
            name=name,
            value=value,
        )
        after = _request("get_cvar", session, timeout, name=name)
        read_back = _cvar_response_value(after)
        record = {
            "name": name,
            "requested": value,
            "before": _cvar_response_value(before),
            "set_response": set_response,
            "applied": read_back,
            "matched": read_back == value,
        }
        records.append(record)
        if read_back != value:
            raise LiveScenarioStageError(
                "client_cvars",
                f"{name} read back as {read_back!r}, expected {value!r}",
            )
        applied[name] = read_back
    output["applied_before_scenario_start"] = True
    return output


def _json(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def _read_json(path: Path, *, stage: str) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise LiveScenarioStageError(stage, f"could not read {path.name}: {error}") from error
    if not isinstance(value, dict):
        raise LiveScenarioStageError(stage, f"{path.name} must contain a JSON object")
    return value

def _wait_json(path: Path, *, stage: str, deadline: float) -> dict[str, Any]:
    last_error: LiveScenarioStageError | None = None
    while time.monotonic() < deadline:
        if path.is_file():
            try:
                return _read_json(path, stage=stage)
            except LiveScenarioStageError as error:
                last_error = error
        time.sleep(0.02)
    if last_error is not None:
        raise last_error
    raise LiveScenarioStageError(stage, f"timed out waiting for {path.name}")


def _copy(source: Path | str | None, target: Path) -> None:
    if not source:
        return
    try:
        path = Path(source)
        if path.is_file():
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(path, target)
    except OSError:
        pass


def _free_port(socket_type: int) -> int:
    with socket.socket(socket.AF_INET, socket_type) as probe:
        probe.bind(("127.0.0.1", 0))
        return int(probe.getsockname()[1])


def _safe_live_fields(scenario: Any) -> dict[str, Any]:
    """Only guard runner fields. The C++ parser owns schema validation."""
    if not isinstance(scenario, dict):
        raise LiveScenarioStageError("runtime_safety", "canonical scenario must be an object")
    execution, world = scenario.get("execution"), scenario.get("world")
    if not isinstance(execution, dict) or execution.get("mode") != "client_server":
        raise LiveScenarioStageError("runtime_safety", "execution.mode must be client_server")
    if not isinstance(world, dict) or not isinstance(world.get("map"), str) or not world["map"]:
        raise LiveScenarioStageError("runtime_safety", "world.map must be a non-empty string")
    if not isinstance(execution.get("max_ticks"), int) or execution["max_ticks"] < 1:
        raise LiveScenarioStageError("runtime_safety", "execution.max_ticks must be a positive integer")
    if not isinstance(scenario.get("timeline", []), list):
        raise LiveScenarioStageError("runtime_safety", "timeline must be an array")
    return scenario


def validate_live_scenario(path: Path, build_dir: Path, timeout: float) -> dict[str, Any]:
    executable = build_dir / "lg_duel_scenarios.exe"
    if not executable.is_file():
        raise LiveScenarioStageError("validate", f"C++ scenario runner is missing: {executable}")
    try:
        completed = subprocess.run(
            [str(executable), "--scenario", str(path), "--validate-only"],
            cwd=build_dir, capture_output=True, text=True, timeout=timeout, check=False,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        raise LiveScenarioStageError("validate", f"C++ validation could not run: {error}") from error
    if completed.returncode != 0:
        detail = (completed.stderr or completed.stdout or f"exit {completed.returncode}").strip()
        raise LiveScenarioStageError("validate", f"C++ validation rejected scenario: {detail}")
    try:
        return _safe_live_fields(json.loads(completed.stdout))
    except json.JSONDecodeError as error:
        raise LiveScenarioStageError("validate", f"C++ validation did not return canonical JSON: {error}") from error


def _request(operation: str, session: dict[str, Any], timeout: float, **parameters: Any) -> dict[str, Any]:
    return lg_control.send_request(
        operation, host="127.0.0.1", port=int(session["control_port"]), timeout=timeout, **parameters,
    )


def _checkpoint(run_dir: Path, tick: int, deadline: float) -> dict[str, Any]:
    """Read only completed, immutable server checkpoint files."""
    while time.monotonic() < deadline:
        choices: list[tuple[int, Path]] = []
        for path in run_dir.glob("checkpoint-*.json"):
            try:
                value = int(path.stem.removeprefix("checkpoint-"))
            except ValueError:
                continue
            if value >= tick:
                choices.append((value, path))
        if choices:
            _, path = min(choices)
            try:
                return _read_json(path, stage="server_checkpoint")
            except LiveScenarioStageError:
                pass
        time.sleep(0.02)
    raise LiveScenarioStageError("server_checkpoint", f"timed out waiting for server tick {tick}")

def _latest_checkpoint(run_dir: Path) -> dict[str, Any]:
    deadline = time.monotonic() + 1.0
    while time.monotonic() < deadline:
        choices: list[tuple[int, Path]] = []
        for path in run_dir.glob("checkpoint-*.json"):
            try:
                choices.append(
                    (int(path.stem.removeprefix("checkpoint-")), path)
                )
            except ValueError:
                continue
        if choices:
            try:
                return _read_json(
                    max(choices)[1],
                    stage="server_checkpoint",
                )
            except LiveScenarioStageError:
                pass
        time.sleep(0.02)
    raise LiveScenarioStageError(
        "server_checkpoint",
        "server has no readable checkpoint",
    )

def _checkpoint_for_event(
    run_dir: Path,
    expected: dict[str, Any],
    deadline: float,
) -> dict[str, Any]:
    inspected: set[Path] = set()
    while time.monotonic() < deadline:
        choices: list[tuple[int, Path]] = []
        for path in run_dir.glob("checkpoint-*.json"):
            try:
                choices.append(
                    (int(path.stem.removeprefix("checkpoint-")), path)
                )
            except ValueError:
                continue
        for _, path in sorted(choices):
            if path in inspected:
                continue
            try:
                checkpoint = _read_json(path, stage="capture_checkpoint")
            except LiveScenarioStageError:
                continue
            inspected.add(path)
            if _event_occurrence_reached(_events(checkpoint), expected):
                return checkpoint
        time.sleep(0.02)
    raise LiveScenarioStageError(
        "capture_checkpoint",
        f"timed out waiting for event {expected}",
    )


def _capture_frame_checkpoint(
    run_dir: Path,
    shot: dict[str, Any],
    deadline: float,
) -> dict[str, Any]:
    result = shot.get("result")
    frame = result.get("frame_state") if isinstance(result, dict) else None
    absolute_tick = (
        frame.get("latest_snapshot_tick") if isinstance(frame, dict) else None
    )
    if not isinstance(absolute_tick, int) or isinstance(absolute_tick, bool):
        raise LiveScenarioStageError(
            "capture_checkpoint",
            f"{shot.get('name')!r} has no exact capture snapshot tick",
        )
    while time.monotonic() < deadline:
        for path in run_dir.glob("checkpoint-*.json"):
            try:
                checkpoint = _read_json(path, stage="capture_checkpoint")
            except LiveScenarioStageError:
                continue
            if checkpoint.get("absolute_server_tick") == absolute_tick:
                return checkpoint
        time.sleep(0.02)
    raise LiveScenarioStageError(
        "capture_checkpoint",
        f"{shot.get('name')!r} has no server checkpoint for capture tick "
        f"{absolute_tick}",
    )


def _event_matches(event: dict[str, Any], expected: dict[str, Any]) -> bool:
    return all(
        event.get(key) == value
        for key, value in expected.items()
        if key != "occurrence"
    )


def _event_occurrence_reached(
    events: list[dict[str, Any]],
    expected: dict[str, Any],
) -> bool:
    occurrence = int(expected.get("occurrence", 1))
    return sum(_event_matches(event, expected) for event in events) >= occurrence


def _phase_capture_for_rocket_attack(
    captures: list[Any],
    captured: set[str],
    occurrence: int,
) -> tuple[dict[str, Any], str] | None:
    phase_events = {
        "muzzle": ("weapon_fired", "local_rocket_launcher_muzzle"),
        "projectile": (
            "projectile_spawned",
            "local_rocket_launcher_projectile",
        ),
        "impact": ("explosion_created", "local_rocket_launcher_impact"),
    }
    for candidate in captures:
        wanted = (
            candidate.get("after_event")
            if isinstance(candidate, dict)
            else None
        )
        render_phase = (
            candidate.get("render_phase")
            if isinstance(candidate, dict)
            else None
        )
        phase_event = phase_events.get(render_phase)
        if (
            not isinstance(candidate, dict)
            or phase_event is None
            or candidate["name"] in captured
            or not isinstance(wanted, dict)
            or wanted.get("type") != phase_event[0]
            or wanted.get("actor", 0) != 0
            or wanted.get("weapon") != "rocket_launcher"
            or int(wanted.get("occurrence", 1)) != occurrence
        ):
            continue
        return candidate, phase_event[1]
    return None


def _client_authority_distance(state: dict[str, Any]) -> float | None:
    predicted = state.get("predicted_local_player")
    authoritative = state.get("authoritative_local_player")
    left = predicted.get("position") if isinstance(predicted, dict) else None
    right = authoritative.get("position") if isinstance(authoritative, dict) else None
    if not (
        isinstance(left, list)
        and isinstance(right, list)
        and len(left) == len(right) == 3
    ):
        return None
    return sum(
        (float(a) - float(b)) ** 2
        for a, b in zip(left, right)
    ) ** 0.5


def _events(checkpoint: dict[str, Any]) -> list[dict[str, Any]]:
    value = checkpoint.get("events_since_setup", checkpoint.get("events", checkpoint.get("authoritative_events", [])))
    return [event for event in value if isinstance(event, dict)] if isinstance(value, list) else []


def _sequence(response: dict[str, Any]) -> int | None:
    for key in ("release_sequence", "command_sequence", "sequence", "first_command_sequence"):
        value = response.get(key)
        if isinstance(value, int) and not isinstance(value, bool):
            return value
    return None


def _capture_source_name(
    scenario_name: str,
    capture_name: str,
    run_token: str,
    sequence: int,
    *,
    captured_at: datetime | None = None,
) -> str:
    """Create a UTC evidence name that cannot collide with another run."""
    timestamp = (captured_at or datetime.now(timezone.utc)).astimezone(
        timezone.utc
    )

    def safe(value: str) -> str:
        lowered = value.lower().replace("_", "-")
        filtered = "".join(
            character
            for character in lowered
            if character.isascii()
            and (character.islower() or character.isdigit() or character == "-")
        )
        return "-".join(part for part in filtered.split("-") if part)

    # The control protocol caps capture names at 64 bytes. Keep the UTC stamp,
    # run key, view, and sequence visible while shortening user-owned labels.
    task = (safe(scenario_name) or "live-scenario")[:12].strip("-")
    view = (safe(capture_name) or "capture")[:20].strip("-")
    token = (safe(run_token) or "run")[:8].strip("-")
    return (
        f"{timestamp.strftime('%Y%m%dT%H%M%SZ')}-"
        f"{task}-{token}-{view}-{sequence:02d}"
    )


def _has_rocket_event(
    events: list[dict[str, Any]],
    event_type: str,
    actor: int,
) -> bool:
    return any(
        event.get("type") == event_type
        and event.get("actor") == actor
        and event.get("weapon") == "rocket_launcher"
        for event in events
    )


def _validate_capture_phase(shot: dict[str, Any]) -> None:
    """Reject a screenshot unless its exact input frame and server history agree."""
    phase = shot.get("render_phase")
    if phase is None:
        return
    if phase not in {"before_fire", "muzzle", "projectile", "impact"}:
        raise LiveScenarioStageError(
            "capture_phase",
            f"{shot.get('name')!r} has unknown render phase {phase!r}",
        )
    result = shot.get("result")
    frame = result.get("frame_state") if isinstance(result, dict) else None
    if not isinstance(frame, dict):
        raise LiveScenarioStageError(
            "capture_phase",
            f"{shot.get('name')!r} has no capture-time frame_state",
        )
    trigger = shot.get("trigger")
    events = trigger.get("events") if isinstance(trigger, dict) else None
    if not isinstance(events, list):
        raise LiveScenarioStageError(
            "capture_phase",
            f"{shot.get('name')!r} has no authoritative trigger events",
        )
    events = [event for event in events if isinstance(event, dict)]
    actor = shot.get("actor", 0)
    local_index = frame.get("local_player_index")
    fired = frame.get("local_rocket_launcher_fired")
    projectiles = frame.get("local_rocket_launcher_projectiles")
    explosions = frame.get("local_rocket_launcher_explosions")
    rendered_rockets = frame.get("renderer_rocket_instances")
    rendered_tracers = frame.get("renderer_tracer_instances")
    rendered_explosions = frame.get("renderer_explosion_instances")
    if (
        local_index != actor
        or not isinstance(fired, bool)
        or not isinstance(projectiles, int)
        or isinstance(projectiles, bool)
        or not isinstance(explosions, int)
        or isinstance(explosions, bool)
        or not isinstance(rendered_rockets, int)
        or isinstance(rendered_rockets, bool)
        or not isinstance(rendered_tracers, int)
        or isinstance(rendered_tracers, bool)
        or not isinstance(rendered_explosions, int)
        or isinstance(rendered_explosions, bool)
    ):
        raise LiveScenarioStageError(
            "capture_phase",
            f"{shot.get('name')!r} has incomplete local Rocket Launcher frame state",
        )

    rocket_events = [
        event
        for event in events
        if event.get("actor") == actor
        and event.get("weapon") == "rocket_launcher"
    ]
    shot_events = [
        event for event in rocket_events if event.get("type") == "weapon_fired"
    ]
    latest_shot = shot_events[-1] if shot_events else None
    shot_sequence = (
        latest_shot.get("sequence") if isinstance(latest_shot, dict) else None
    )
    shot_window = [
        event
        for event in rocket_events
        if (
            not isinstance(shot_sequence, int)
            or not isinstance(event.get("sequence"), int)
            or event["sequence"] >= shot_sequence
        )
    ]
    fired_event = latest_shot is not None
    spawned_event = _has_rocket_event(
        shot_window,
        "projectile_spawned",
        actor,
    )
    impact_event = _has_rocket_event(
        shot_window,
        "explosion_created",
        actor,
    )
    accepted = {
        "before_fire": (
            not fired
            and projectiles == 0
            and explosions == 0
            and rendered_rockets == 0
            and rendered_tracers == 0
            and rendered_explosions == 0
            and not fired_event
            and not spawned_event
            and not impact_event
        ),
        "muzzle": (
            projectiles >= 1
            and explosions == 0
            and rendered_rockets >= 1
            and rendered_tracers >= 1
            and rendered_explosions == 0
            and fired_event
            and spawned_event
            and not impact_event
        ),
        "projectile": (
            not fired
            and projectiles >= 1
            and explosions == 0
            and rendered_rockets >= 1
            and rendered_tracers == 0
            and rendered_explosions == 0
            and fired_event
            and spawned_event
            and not impact_event
        ),
        "impact": (
            not fired
            and projectiles == 0
            and rendered_rockets == 0
            and rendered_tracers == 0
            and rendered_explosions >= 2
            and impact_event
        ),
    }[phase]
    if not accepted:
        raise LiveScenarioStageError(
            "capture_phase",
            f"{shot.get('name')!r} is idle or does not match phase {phase!r}; "
            f"frame={frame}, events={[event.get('type') for event in events]}",
        )
    shot["phase_evidence"] = {
        "shot_event": latest_shot,
        "event_window": shot_window,
        "capture_frame": frame,
    }


def _input_parts(entry: dict[str, Any]) -> list[dict[str, Any]]:
    source = dict(entry.get("input", {}))
    weapon = source.get("weapon")
    if isinstance(weapon, str):
        # Scenario JSON uses canonical names such as machine_gun while the
        # bounded client control protocol accepts its gameplay console tokens.
        source["weapon"] = weapon.replace("_", "")
    duration = int(entry["duration_ticks"])
    edges = [str(value) for value in entry.get("one_tick_edges", [])]
    for edge in edges:
        source[edge] = True
    return [{"ticks": duration, "one_tick_edges": edges, **source}]


def _capture(
    capture: dict[str, Any],
    source_name: str,
    session: dict[str, Any],
    timeout: float,
    shots: list[dict[str, Any]],
) -> None:
    frames = int(capture.get("wait_rendered_frames", 0))
    if frames:
        _request("wait_frames", session, timeout, frames=frames)
    result = _request(
        "capture_screenshot",
        session,
        timeout,
        name=source_name,
        hide_hud=True,
        hide_overlays=True,
    )
    _record_capture_result(capture, source_name, result, shots)


def _record_capture_result(
    capture: dict[str, Any],
    source_name: str,
    result: dict[str, Any],
    shots: list[dict[str, Any]],
) -> None:
    source = Path(str(result.get("path", "")))
    if not source.is_file():
        raise LiveScenarioStageError(
            "capture",
            f"renderer did not write screenshot {capture['name']!r}",
        )
    width, height = _png_dimensions(source)
    if result.get("width") != width or result.get("height") != height:
        raise LiveScenarioStageError(
            "capture",
            "renderer capture dimensions do not match the PNG header",
        )
    result["verified_width"] = width
    result["verified_height"] = height
    shots.append(
        {
            "name": capture["name"],
            "source_name": source_name,
            "render_phase": capture.get("render_phase"),
            "actor": capture.get("actor", 0),
            "result": result,
            "render_state": result.get("frame_state"),
        }
    )


def _png_dimensions(path: Path) -> tuple[int, int]:
    try:
        header = path.read_bytes()[:24]
    except OSError as error:
        raise LiveScenarioStageError(
            "capture",
            f"could not read screenshot {path}: {error}",
        ) from error
    if (
        len(header) != 24
        or header[:8] != b"\x89PNG\r\n\x1a\n"
        or header[12:16] != b"IHDR"
    ):
        raise LiveScenarioStageError(
            "capture",
            f"screenshot is not a PNG with an IHDR header: {path}",
        )
    return (
        int.from_bytes(header[16:20], "big"),
        int.from_bytes(header[20:24], "big"),
    )


def _live_assertions(scenario: dict[str, Any], client: dict[str, Any], session: dict[str, Any],
                     acknowledgements: list[dict[str, Any]], screenshots: list[dict[str, Any]],
                     server_result: dict[str, Any], client_samples: list[dict[str, Any]]) -> list[dict[str, Any]]:
    output: list[dict[str, Any]] = []
    authoritative_types = {
        "player_position", "player_velocity", "player_health", "player_alive",
        "player_weapon", "projectile_exists", "projectile_removed", "event",
        "state_hash",
    }
    for index, assertion in enumerate(scenario.get("assertions", [])):
        if not isinstance(assertion, dict):
            continue
        kind = assertion.get("type")
        if kind in authoritative_types:
            continue
        classification = assertion.get("classification")
        record: dict[str, Any] = {"index": index, "type": kind, "classification": classification}
        if kind == "command_acknowledged":
            timeline = assertion.get("timeline_index")
            found = [row for row in acknowledgements if row.get("timeline_index") == timeline]
            maximum = assertion.get("max_ticks")
            passed = bool(found) and all(
                isinstance(row.get("ack_delay_ticks"), int) and
                row["ack_delay_ticks"] <= maximum
                for row in found
            )
            record.update({"status": "passed" if passed else "failed", "actual": found})
        elif kind == "input_edge_count":
            edge = assertion.get("edge")
            command = server_result.get("latest_consumed_command", {})
            consumed = command.get("consumed_action_edge_counts", {}) if isinstance(command, dict) else {}
            count = consumed.get(edge) if isinstance(consumed, dict) else None
            record.update({"status": "passed" if count == assertion.get("count") else "failed", "actual": count})
        elif kind == "client_pending_commands_max":
            actual = client.get("maximum_pending_command_count")
            record.update({"status": "passed" if isinstance(actual, (int, float)) and actual <= assertion.get("max") else "failed", "actual": actual})
        elif kind == "client_correction_magnitude_max":
            actual = client.get("maximum_correction_distance")
            record.update({"status": "passed" if isinstance(actual, (int, float)) and actual <= assertion.get("max") else "failed", "actual": actual})
        elif kind == "client_correction_count":
            actual, minimum, maximum = client.get("correction_count"), assertion.get("min"), assertion.get("max")
            passed = isinstance(actual, (int, float)) and actual >= minimum and (maximum is None or actual <= maximum)
            record.update({"status": "passed" if passed else "failed", "actual": actual})
        elif kind == "client_converged":
            start_tick = max(
                (
                    int(entry["at_tick"]) + int(entry["duration_ticks"])
                    for entry in scenario.get("timeline", [])
                ),
                default=0,
            )
            deadline = start_tick + int(assertion.get("within_ticks", 0))
            distances = []
            for sample in client_samples:
                tick = sample.get("server_relative_tick")
                sample_state = sample.get("state", {})
                if not isinstance(tick, int) or tick < start_tick or tick > deadline:
                    continue
                distance = _client_authority_distance(sample_state)
                if distance is not None:
                    distances.append(
                        {
                            "tick": tick,
                            "distance": distance,
                        }
                    )
            passed = any(
                row["distance"] <= assertion.get("tolerance")
                for row in distances
            )
            record.update({"status": "passed" if passed else "failed", "actual": distances})
        elif kind == "client_connected":
            actual, expected = bool(client.get("connected")), assertion.get("expected")
            record.update({"status": "passed" if actual == expected else "failed", "actual": actual})
        elif kind == "renderer_backend":
            actual, expected = session.get("status", {}).get("renderer"), assertion.get("backend")
            record.update({"status": "passed" if actual == expected else "failed", "actual": actual})
        elif kind == "screenshot_checkpoint":
            found = next((row for row in screenshots if row.get("name") == assertion.get("capture")), None)
            image = dict(found.get("result", {})) if isinstance(found, dict) else {}
            if isinstance(found, dict) and isinstance(found.get("trigger"), dict):
                image["trigger"] = found["trigger"]
            if isinstance(found, dict) and isinstance(found.get("render_state"), dict):
                image["render_state"] = found["render_state"]
            if isinstance(found, dict) and isinstance(found.get("phase_evidence"), dict):
                image["phase_evidence"] = found["phase_evidence"]
            if isinstance(found, dict):
                image["source_name"] = found.get("source_name")
                image["render_phase"] = found.get("render_phase")
            passed = bool(found) and image.get("verified_width") == assertion.get("width") and image.get("verified_height") == assertion.get("height")
            record.update({"status": "passed" if passed else "failed", "actual": image})
        else:
            record.update({"status": "skipped", "reason": "not a Phase-2 live assertion"})
        output.append(record)
    return output


def _junit(name: str, assertions: list[dict[str, Any]], error: str | None) -> str:
    failed = [row for row in assertions if row.get("status") == "failed"]
    skipped = [row for row in assertions if row.get("status") in {"skipped", "copied_from_server"}]
    cases = []
    for row in assertions or [{"type": "run", "status": "failed" if error else "passed"}]:
        body = ""
        if row.get("status") == "failed": body = f'<failure message="{escape(str(row), {"\"": "&quot;"})}"/>'
        elif row.get("status") in {"skipped", "copied_from_server"}: body = "<skipped/>"
        cases.append(f'<testcase name="{escape(str(row.get("type", "run")))}">{body}</testcase>')
    return f'<testsuite name="{escape(name, {"\"": "&quot;"})}" tests="{len(cases)}" failures="{len(failed)}" skipped="{len(skipped)}">' + "".join(cases) + "</testsuite>\n"


def run_live_scenario(path: str | Path, output_root: str | Path = DEFAULT_OUTPUT_ROOT, *, renderer: str = "gpu",
                      allow_fallback: bool = False, timeout: float = 60.0,
                      build_dir: str | Path = DEFAULT_BUILD_DIR,
                      client_cvars: dict[str, str] | None = None) -> dict[str, Any]:
    """Validate, launch, drive, and collect one real client/server scenario."""
    started = time.monotonic()
    scenario_path, build = Path(path).resolve(), Path(build_dir).resolve()
    run_token = uuid.uuid4().hex
    run_dir = Path(output_root).resolve() / f"live-{int(time.time() * 1000)}-{run_token[:8]}"
    scenario_dir = run_dir / "scenarios" / scenario_path.stem
    scenario_dir.mkdir(parents=True, exist_ok=True)
    session: dict[str, Any] | None = None
    scenario: dict[str, Any] = {}
    stage, error = "validate", None
    screenshots: list[dict[str, Any]] = []
    acknowledgements: list[dict[str, Any]] = []
    client_samples: list[dict[str, Any]] = []
    client: dict[str, Any] = {}
    checkpoint: dict[str, Any] = {}
    cleanup: dict[str, Any] = {"stopped": [], "failures": []}
    requested_client_cvars = dict(client_cvars or {})
    invalid_client_cvars = {
        name: value
        for name, value in requested_client_cvars.items()
        if (
            name not in CLIENT_CVAR_OVERRIDE_RULES
            or value not in CLIENT_CVAR_OVERRIDE_RULES[name]
        )
    }
    client_cvar_attestation: dict[str, Any] = {
        "requested": requested_client_cvars,
        "applied": {},
        "records": [],
        "applied_before_scenario_start": False,
        "restore": "owned client exits during runner cleanup",
    }
    try:
        # Keep the supplied source even when C++ validation rejects it.
        _copy(scenario_path, scenario_dir / "scenario.json")
        scenario = validate_live_scenario(scenario_path, build, timeout)
        if invalid_client_cvars:
            raise LiveScenarioStageError(
                "client_cvars",
                f"client cvar overrides are not allowlisted: {invalid_client_cvars}",
            )
        _json(scenario_dir / "scenario.json", scenario)
        runtime_scenario = json.loads(json.dumps(scenario))
        capture_count = sum(
            1
            for capture in scenario.get("captures", [])
            if isinstance(capture, dict)
        )
        if capture_count:
            runtime_scenario["execution"]["max_ticks"] += (
                capture_count * CAPTURE_RUNTIME_PADDING_TICKS
            )
        runtime_scenario_path = scenario_dir / "runtime-scenario.json"
        _json(runtime_scenario_path, runtime_scenario)
        # Probe with the protocol each process will bind. A free TCP port can
        # still be occupied by UDP, and vice versa.
        server_port = _free_port(socket.SOCK_DGRAM)
        control_port = _free_port(socket.SOCK_STREAM)
        while control_port == server_port:
            control_port = _free_port(socket.SOCK_STREAM)
        stage = "launch"
        session = lg_launch.launch_scenario_session(
            runtime_scenario_path, run_dir, run_token, server_port, control_port, renderer,
            allow_fallback, timeout, build,
        )
        ready_path = run_dir / "ready.json"
        deadline = time.monotonic() + timeout
        ready = _wait_json(ready_path, stage="ready", deadline=deadline)
        if ready.get("token") != run_token or ready.get("scenario") != scenario.get("name"):
            raise LiveScenarioStageError("ready", "server ready record does not attest this scenario session")
        expected_map = scenario["world"]["map"].removesuffix(".map")
        if str(ready.get("map", "")).removesuffix(".map") != expected_map:
            raise LiveScenarioStageError("ready", f"server map is {ready.get('map')!r}, expected {expected_map!r}")
        if not isinstance(ready.get("map_revision"), int):
            raise LiveScenarioStageError("ready", "server ready record has no map revision")
        stage = "client_cvars"
        client_cvar_attestation = _apply_client_cvar_overrides(
            session,
            timeout,
            requested_client_cvars,
            client_cvar_attestation,
        )
        initial_client = _request("get_client_state", session, timeout)
        client_tick_base = int(initial_client.get("client_tick", 0))
        stage = "network"
        network = scenario.get("network")
        if isinstance(network, dict):
            _request("set_network_simulation", session, timeout, **network)
        start_path = run_dir / "start.request.json"
        temporary_start = run_dir / f".{start_path.name}.{run_token}.tmp"
        _json(
            temporary_start,
            {"schema_version": 1, "token": run_token},
        )
        os.replace(temporary_start, start_path)
        initial_checkpoint = _checkpoint(
            run_dir,
            0,
            time.monotonic() + timeout,
        )
        state = initial_checkpoint.get("state")
        if not isinstance(state, dict) or not isinstance(state.get("map_revision"), int):
            raise LiveScenarioStageError("ready", "initial server checkpoint has no map revision")
        client_samples.append(
            {"server_relative_tick": 0, "state": initial_client}
        )
        captured: set[str] = set()
        capture_pause_ticks = 0
        capture_source_names = {
            str(capture["name"]): _capture_source_name(
                str(scenario["name"]),
                str(capture["name"]),
                run_token,
                index,
            )
            for index, capture in enumerate(
                (
                    capture
                    for capture in scenario.get("captures", [])
                    if isinstance(capture, dict)
                ),
                start=1,
            )
        }
        tick_captures = sorted(
            (
                capture
                for capture in scenario.get("captures", [])
                if isinstance(capture, dict) and "at_server_tick" in capture
            ),
            key=lambda capture: int(capture["at_server_tick"]),
        )

        def capture_through(relative_tick: int) -> None:
            nonlocal capture_pause_ticks, stage
            for capture in tick_captures:
                if (
                    capture["name"] in captured
                    or int(capture["at_server_tick"]) > relative_tick
                ):
                    continue
                requested_tick = int(capture["at_server_tick"])
                capture_checkpoint = _checkpoint(
                    run_dir,
                    requested_tick + capture_pause_ticks,
                    time.monotonic() + timeout,
                )
                absolute_tick = capture_checkpoint.get("absolute_server_tick")
                if not isinstance(absolute_tick, int):
                    raise LiveScenarioStageError(
                        "capture_checkpoint",
                        "capture checkpoint has no absolute_server_tick",
                    )
                # A tick capture must reach the client before a rendered frame
                # can attest that state.
                _request(
                    "wait_snapshot_tick",
                    session,
                    timeout,
                    min_tick=absolute_tick,
                )
                stage = "capture"
                _capture(
                    capture,
                    capture_source_names[capture["name"]],
                    session,
                    timeout,
                    screenshots,
                )
                frame_checkpoint = _capture_frame_checkpoint(
                    run_dir,
                    screenshots[-1],
                    time.monotonic() + timeout,
                )
                capture_finished = _latest_checkpoint(run_dir)
                finished_tick = capture_finished.get("relative_tick")
                started_tick = capture_checkpoint.get("relative_tick")
                captured_pause = (
                    max(0, finished_tick - started_tick)
                    if isinstance(finished_tick, int) and isinstance(started_tick, int)
                    else 0
                )
                capture_pause_ticks += captured_pause
                screenshots[-1]["trigger"] = {
                    "requested_server_relative_tick": requested_tick,
                    "runtime_capture_pause_ticks": captured_pause,
                    "runtime_total_capture_pause_ticks": capture_pause_ticks,
                    "trigger_checkpoint_relative_tick": capture_checkpoint.get(
                        "relative_tick"
                    ),
                    "trigger_checkpoint_absolute_tick": absolute_tick,
                    "capture_frame_checkpoint_relative_tick":
                        frame_checkpoint.get("relative_tick"),
                    "capture_frame_checkpoint_absolute_tick":
                        frame_checkpoint.get("absolute_server_tick"),
                    "events": _events(frame_checkpoint)[:256],
                }
                _validate_capture_phase(screenshots[-1])
                captured.add(capture["name"])

        def capture_ready_events(checkpoint: dict[str, Any]) -> None:
            """Capture reached event phases before another control frame can pass."""
            nonlocal capture_pause_ticks, stage
            for capture in scenario.get("captures", []):
                wanted = (
                    capture.get("after_event")
                    if isinstance(capture, dict)
                    else None
                )
                if (
                    not wanted
                    or capture["name"] in captured
                    or not _event_occurrence_reached(
                        _events(checkpoint),
                        wanted,
                    )
                ):
                    continue
                stage = "capture"
                absolute_tick = checkpoint.get("absolute_server_tick")
                if not isinstance(absolute_tick, int):
                    raise LiveScenarioStageError(
                        "capture_checkpoint",
                        "event capture checkpoint has no absolute_server_tick",
                    )
                _request(
                    "wait_snapshot_tick",
                    session,
                    timeout,
                    min_tick=absolute_tick,
                )
                capture_started = _latest_checkpoint(run_dir)
                _capture(
                    capture,
                    capture_source_names[capture["name"]],
                    session,
                    timeout,
                    screenshots,
                )
                frame_checkpoint = _capture_frame_checkpoint(
                    run_dir,
                    screenshots[-1],
                    time.monotonic() + timeout,
                )
                matching_frame_events = [
                    event
                    for event in _events(frame_checkpoint)
                    if _event_matches(event, wanted)
                ]
                capture_finished = _latest_checkpoint(run_dir)
                started_tick = capture_started.get("relative_tick")
                finished_tick = capture_finished.get("relative_tick")
                captured_pause = (
                    max(0, finished_tick - started_tick)
                    if isinstance(finished_tick, int)
                    and isinstance(started_tick, int)
                    else 0
                )
                capture_pause_ticks += captured_pause
                screenshots[-1]["trigger"] = {
                    "after_event": wanted,
                    "runtime_capture_pause_ticks": captured_pause,
                    "runtime_total_capture_pause_ticks": capture_pause_ticks,
                    "trigger_checkpoint_relative_tick": checkpoint.get(
                        "relative_tick"
                    ),
                    "trigger_checkpoint_absolute_tick": absolute_tick,
                    "capture_frame_checkpoint_relative_tick":
                        frame_checkpoint.get("relative_tick"),
                    "capture_frame_checkpoint_absolute_tick":
                        frame_checkpoint.get("absolute_server_tick"),
                    "matched_events": matching_frame_events[:256],
                    "events": _events(frame_checkpoint)[:256],
                }
                _validate_capture_phase(screenshots[-1])
                captured.add(capture["name"])

        scheduled_entries = sorted(
            enumerate(scenario["timeline"]),
            key=lambda item: int(item[1]["at_tick"]),
        )
        rocket_attack_occurrence = 0
        for index, entry in scheduled_entries:
            target = int(entry["at_tick"])
            end_tick = target + int(entry["duration_ticks"])
            capture_through(target)
            for part in _input_parts(entry):
                stage = "schedule"
                runtime_target = target + capture_pause_ticks
                checkpoint = _checkpoint(
                    run_dir,
                    runtime_target,
                    time.monotonic() + timeout,
                )
                absolute_server_tick = checkpoint.get("absolute_server_tick")
                if not isinstance(absolute_server_tick, int):
                    raise LiveScenarioStageError(
                        "server_checkpoint",
                        "checkpoint has no absolute_server_tick",
                    )
                _request(
                  "wait_client_tick",
                  session,
                  timeout,
                  min_tick=client_tick_base + runtime_target,
                )
                _request(
                    "wait_snapshot_tick",
                    session,
                    timeout,
                    min_tick=absolute_server_tick,
                )
                before_input = _request("get_client_state", session, timeout)
                # Read the server clock after all other control calls. This is
                # the last step before send_input, so the bound measures the
                # real dispatch point rather than an earlier wait.
                dispatch_checkpoint = _latest_checkpoint(run_dir)
                dispatch_tick = dispatch_checkpoint.get("relative_tick")
                if not isinstance(dispatch_tick, int):
                    raise LiveScenarioStageError(
                        "server_checkpoint",
                        "dispatch checkpoint has no relative_tick",
                    )
                schedule_late_ticks = max(0, dispatch_tick - runtime_target)
                schedule_late_limit = _schedule_late_limit(capture_pause_ticks)
                if schedule_late_ticks > schedule_late_limit:
                    raise LiveScenarioStageError(
                        "schedule",
                        f"timeline[{index}] started {schedule_late_ticks} ticks late; "
                        f"limit is {schedule_late_limit}",
                    )
                armed_capture: dict[str, Any] | None = None
                armed_phase: str | None = None
                if (
                    bool(part.get("attack"))
                    and part.get("weapon") == "rocketlauncher"
                    and int(entry.get("player", 0)) == 0
                ):
                    rocket_attack_occurrence += 1
                    phase_capture = _phase_capture_for_rocket_attack(
                        scenario.get("captures", []),
                        captured,
                        rocket_attack_occurrence,
                    )
                    if phase_capture is not None:
                        armed_capture, armed_phase = phase_capture
                if armed_capture is not None:
                    stage = "capture_arm"
                    _request(
                        "arm_phase_capture",
                        session,
                        timeout,
                        name=capture_source_names[armed_capture["name"]],
                        phase=armed_phase,
                        hide_hud=True,
                        hide_overlays=True,
                    )
                    stage = "schedule"
                response = _request("send_input", session, timeout, **part)
                sequence = _sequence(response)
                record = {"timeline_index": index, "input": part, "sequence": sequence,
                          "scheduled_server_tick": target,
                          "scheduled_runtime_server_tick": runtime_target,
                          "capture_pause_ticks_before_dispatch": capture_pause_ticks,
                          "dispatch_server_tick": dispatch_tick,
                          "schedule_late_ticks": schedule_late_ticks,
                          "response": response}
                if armed_capture is not None:
                    stage = "capture"
                    armed_result = _request(
                        "collect_phase_capture",
                        session,
                        timeout,
                        name=capture_source_names[armed_capture["name"]],
                    )
                    _record_capture_result(
                        armed_capture,
                        capture_source_names[armed_capture["name"]],
                        armed_result,
                        screenshots,
                    )
                    frame_checkpoint = _capture_frame_checkpoint(
                        run_dir,
                        screenshots[-1],
                        time.monotonic() + timeout,
                    )
                    capture_finished = _latest_checkpoint(run_dir)
                    frame_tick = frame_checkpoint.get("relative_tick")
                    finished_tick = capture_finished.get("relative_tick")
                    captured_pause = (
                        max(0, finished_tick - frame_tick)
                        if isinstance(frame_tick, int)
                        and isinstance(finished_tick, int)
                        else 0
                    )
                    capture_pause_ticks += captured_pause
                    wanted = armed_capture["after_event"]
                    matching_events = [
                        event
                        for event in _events(frame_checkpoint)
                        if _event_matches(event, wanted)
                    ]
                    screenshots[-1]["trigger"] = {
                        "after_event": wanted,
                        "capture_mode": "prearmed_exact_render_phase",
                        "runtime_capture_pause_ticks": captured_pause,
                        "runtime_total_capture_pause_ticks":
                            capture_pause_ticks,
                        "trigger_checkpoint_relative_tick": frame_checkpoint.get(
                            "relative_tick"
                        ),
                        "trigger_checkpoint_absolute_tick": frame_checkpoint.get(
                            "absolute_server_tick"
                        ),
                        "capture_frame_checkpoint_relative_tick":
                            frame_checkpoint.get("relative_tick"),
                        "capture_frame_checkpoint_absolute_tick":
                            frame_checkpoint.get("absolute_server_tick"),
                        "matched_events": matching_events[:256],
                        "events": _events(frame_checkpoint)[:256],
                    }
                    _validate_capture_phase(screenshots[-1])
                    captured.add(armed_capture["name"])
                    stage = "schedule"
                capture_through(end_tick)
                # send_input itself waits for the server to acknowledge its
                # neutral release. Capture any event that ack proves before
                # issuing another client control call.
                capture_ready_events(_latest_checkpoint(run_dir))
                if sequence is not None:
                    record["ack"] = _request("wait_command_ack", session, timeout, sequence=sequence)
                after_input = _request("get_client_state", session, timeout)
                before_tick = before_input.get("client_tick")
                after_tick = after_input.get("client_tick")
                if isinstance(before_tick, int) and isinstance(after_tick, int):
                    record["ack_delay_ticks"] = max(
                        0,
                        after_tick - before_tick - int(part["ticks"]),
                    )
                latest = _latest_checkpoint(run_dir)
                record["ack_server_relative_tick"] = latest.get(
                    "relative_tick"
                )
                client_samples.append(
                    {
                        "server_relative_tick": latest.get("relative_tick"),
                        "state": after_input,
                    }
                )
                acknowledgements.append(record)
            checkpoint = _checkpoint(
                run_dir,
                end_tick + capture_pause_ticks,
                time.monotonic() + timeout,
            )
            relative_tick = checkpoint.get("relative_tick")
            for record in acknowledgements:
                if record["timeline_index"] == index:
                    record["server_relative_tick"] = relative_tick
            capture_ready_events(_latest_checkpoint(run_dir))
        capture_through(int(scenario["execution"]["max_ticks"]))
        convergence_start_tick = max(
            (
                int(entry["at_tick"]) + int(entry["duration_ticks"])
                for entry in scenario.get("timeline", [])
            ),
            default=0,
        )
        for assertion in scenario.get("assertions", []):
            if not isinstance(assertion, dict) or assertion.get("type") != "client_converged":
                continue
            convergence_deadline_tick = (
                convergence_start_tick + int(assertion["within_ticks"])
            )
            while True:
                sample_state = _request("get_client_state", session, timeout)
                sample_checkpoint = _latest_checkpoint(run_dir)
                sample_tick = sample_checkpoint.get("relative_tick")
                client_samples.append(
                    {
                        "server_relative_tick": sample_tick,
                        "state": sample_state,
                    }
                )
                distance = _client_authority_distance(sample_state)
                if (
                    distance is not None
                    and distance <= float(assertion["tolerance"])
                ):
                    break
                if (
                    not isinstance(sample_tick, int)
                    or sample_tick >= convergence_deadline_tick
                ):
                    break
                client_tick = sample_state.get("client_tick")
                if not isinstance(client_tick, int):
                    break
                _request(
                    "wait_client_tick",
                    session,
                    timeout,
                    min_tick=client_tick + 1,
                )
        stage = "finish"
        for capture in scenario.get("captures", []):
            if not isinstance(capture, dict) or capture["name"] in captured:
                continue
            if "after_event" in capture:
                checkpoint = _checkpoint_for_event(
                    run_dir,
                    capture["after_event"],
                    time.monotonic() + timeout,
                )
                absolute_tick = checkpoint.get("absolute_server_tick")
                if not isinstance(absolute_tick, int):
                    raise LiveScenarioStageError(
                        "capture_checkpoint",
                        "event capture checkpoint has no absolute_server_tick",
                    )
                _request(
                    "wait_snapshot_tick",
                    session,
                    timeout,
                    min_tick=absolute_tick,
                )
                stage = "capture"
                _capture(
                    capture,
                    capture_source_names[capture["name"]],
                    session,
                    timeout,
                    screenshots,
                )
                frame_checkpoint = _capture_frame_checkpoint(
                    run_dir,
                    screenshots[-1],
                    time.monotonic() + timeout,
                )
                matching_events = [
                    event
                    for event in _events(frame_checkpoint)
                    if _event_matches(event, capture["after_event"])
                ]
                screenshots[-1]["trigger"] = {
                    "after_event": capture["after_event"],
                    "trigger_checkpoint_relative_tick": checkpoint.get(
                        "relative_tick"
                    ),
                    "trigger_checkpoint_absolute_tick": checkpoint.get(
                        "absolute_server_tick"
                    ),
                    "capture_frame_checkpoint_relative_tick":
                        frame_checkpoint.get("relative_tick"),
                    "capture_frame_checkpoint_absolute_tick":
                        frame_checkpoint.get("absolute_server_tick"),
                    "matched_events": matching_events[:256],
                    "events": _events(frame_checkpoint)[:256],
                }
                _validate_capture_phase(screenshots[-1])
                captured.add(capture["name"])
        stage = "finish"
        minimum_sequence = max((row["sequence"] for row in acknowledgements if row.get("sequence") is not None), default=0)
        finish_path = run_dir / "finish.request.json"
        temporary_finish = run_dir / f".{finish_path.name}.{run_token}.tmp"
        _json(temporary_finish, {"schema_version": 1, "token": run_token, "minimum_command_sequence": minimum_sequence})
        os.replace(temporary_finish, finish_path)
        deadline = time.monotonic() + timeout
        result_path = run_dir / "result.json"
        while not result_path.is_file() and time.monotonic() < deadline:
            time.sleep(0.02)
        if not result_path.is_file():
            raise LiveScenarioStageError("finish", "server did not write result.json before the deadline")
        result = _wait_json(
            result_path,
            stage="finish",
            deadline=time.monotonic() + timeout,
        )
        client = _request("get_client_state", session, timeout)
        client_samples.append(
            {
                "server_relative_tick": result.get("relative_tick"),
                "state": client,
            }
        )
        if finish_path.is_file():
            _json(
                scenario_dir / "finish-request.json",
                _wait_json(
                    finish_path,
                    stage="finish",
                    deadline=time.monotonic() + timeout,
                ),
            )
        if result_path.is_file(): _json(scenario_dir / "result.json", result)
        else: raise LiveScenarioStageError("finish", "server did not write result.json")
    except Exception as caught:  # Preserve partial evidence before reporting the failure.
        error = str(caught)
    finally:
        if session is not None:
            try:
                cleanup = lg_launch.cleanup_scenario_session(session)
            except Exception as caught:
                cleanup = {"stopped": [], "failures": [str(caught)]}
        result = _read_json(scenario_dir / "result.json", stage="result") if (scenario_dir / "result.json").is_file() else {}
        server_assertions = result.get("assertions", []) if isinstance(result.get("assertions", []), list) else []
        phase_two = _live_assertions(
            scenario,
            client,
            session or {},
            acknowledgements,
            screenshots,
            result,
            client_samples,
        ) if scenario else []
        assertions = {"server": server_assertions, "live": phase_two}
        _json(scenario_dir / "assertions.json", assertions)
        _json(scenario_dir / "authoritative-events.json", result.get("events_since_setup", result.get("events", _events(checkpoint))))
        _json(scenario_dir / "client-events.json", [row.get("ack", {}) for row in acknowledgements])
        _json(scenario_dir / "screenshots.json", screenshots)
        _json(scenario_dir / "client-samples.json", client_samples)
        _json(scenario_dir / "server-state.json", checkpoint)
        _json(scenario_dir / "client-state.json", client)
        _json(scenario_dir / "network-decisions.json", client.get("network_simulation", {}))
        _json(scenario_dir / "reconciliation.json", {key: client.get(key) for key in ("pending_command_count", "correction_count", "last_correction_distance", "interpolation")})
        checkpoint_hashes = {}
        for item in run_dir.glob("checkpoint-*.json"):
            try:
                recorded = _read_json(item, stage="hashes")
                checkpoint_hashes[item.stem] = recorded.get("state_hash")
            except LiveScenarioError:
                continue
        _json(scenario_dir / "hashes.json", {"result_state_hash": result.get("state_hash"), "checkpoints": checkpoint_hashes, "reported": result.get("hashes", {})})
        _json(scenario_dir / "process-log.json", {"session": session or {}, "cleanup": cleanup, "stage": stage})
        for key, name in (("server_stdout", "server.stdout.log"), ("server_stderr", "server.stderr.log"), ("client_stdout", "client.stdout.log"), ("client_stderr", "client.stderr.log")):
            _copy((session or {}).get("logs", {}).get(key) or run_dir / name, scenario_dir / name)
        for shot in screenshots:
            _copy(shot.get("result", {}).get("path"), scenario_dir / "screenshots" / f"{shot['name']}.png")
        failed = [row for row in phase_two if row.get("status") == "failed"]
        failed_server = [
            row for row in server_assertions
            if isinstance(row, dict) and row.get("passed") is False
        ]
        server_result_failed = bool(result) and result.get("passed") is False
        junit_assertions = list(phase_two) + [
            {
                "type": row.get("type", "authoritative"),
                "status": "passed" if row.get("passed") else "failed",
                "message": row.get("message", ""),
            }
            for row in server_assertions
            if isinstance(row, dict)
        ]
        if server_result_failed and not failed_server:
            junit_assertions.append(
                {
                    "type": "authoritative_server_result",
                    "status": "failed",
                    "message": str(
                        result.get("completion_reason", "server reported failure")
                    ),
                }
            )
        status = (
            "failed"
            if (
                error
                or failed
                or failed_server
                or server_result_failed
                or cleanup.get("failures")
            )
            else "passed"
        )
        summary = {"scenario": scenario.get("name", scenario_path.stem), "status": status, "stage": stage,
                   "error": error, "runtime_seconds": time.monotonic() - started, "artifact_path": str(run_dir),
                   "live_failed": len(failed),
                   "authoritative_failed": len(failed_server) + int(server_result_failed and not failed_server),
                   "cleanup": cleanup}
        _json(run_dir / "manifest.json", {"schema_version": 1, "run_token": run_token, "scenario_path": str(scenario_path), "session": session or {}})
        _json(run_dir / "client-cvar-overrides.json", client_cvar_attestation)
        _json(run_dir / "environment.json", {"renderer": renderer, "allow_fallback": allow_fallback, "build_dir": str(build), "session": session or {}, "client_cvar_overrides": client_cvar_attestation})
        _json(run_dir / "summary.json", summary)
        (run_dir / "junit.xml").write_text(
            _junit(summary["scenario"], junit_assertions, error),
            encoding="utf-8",
        )
    if summary["status"] != "passed":
        raise LiveScenarioError(f"{summary['stage']}: {summary['error'] or 'live assertion or cleanup failed'}; artifacts: {run_dir}")
    return summary


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Run one LG Duel live client/server scenario")
    parser.add_argument("scenario")
    parser.add_argument("--output-root", default=str(DEFAULT_OUTPUT_ROOT))
    parser.add_argument("--renderer", choices=("gpu", "fallback"), default="gpu")
    parser.add_argument("--allow-fallback", action="store_true")
    parser.add_argument("--timeout", type=float, default=60.0)
    parser.add_argument("--build-dir", default=str(DEFAULT_BUILD_DIR))
    parser.add_argument(
        "--client-cvar",
        action="append",
        default=[],
        type=_parse_client_cvar_override,
        metavar="NAME=VALUE",
        help="owned-client presentation override; repeat for r_combat_effects and r_bloom",
    )
    arguments = parser.parse_args(argv)
    if arguments.renderer == "fallback" and not arguments.allow_fallback:
        parser.error("--renderer fallback requires --allow-fallback")
    try:
        client_cvars = _client_cvar_overrides(arguments.client_cvar)
        result = run_live_scenario(
            arguments.scenario, arguments.output_root, renderer=arguments.renderer,
            allow_fallback=arguments.allow_fallback, timeout=arguments.timeout,
            build_dir=arguments.build_dir, client_cvars=client_cvars,
        )
    except LiveScenarioError as error:
        print(json.dumps({"ok": False, "error": str(error)}))
        return 1
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
