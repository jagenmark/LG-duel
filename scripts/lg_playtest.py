#!/usr/bin/env python3
"""Explicitly attached, observable playtesting through the real game input path.

No launcher, opponent-state reads, aiming automation, or gameplay mutations live here.
Screenshots and local state are consecutive observations, not an atomic snapshot.
"""
from __future__ import annotations

import argparse
import html
import json
import math
import shutil
import sys
import time
import uuid
from pathlib import Path
from typing import Any

import lg_control
from lg_launch import LaunchError, attach_client

DEFAULT_OUTPUT = Path(__file__).resolve().parents[1] / "build" / "playtests"
INPUT_NUMBERS = {"forward": (-1, 1), "right": (-1, 1), "up": (-1, 1),
                 "yaw": (-1000000, 1000000), "pitch": (-89.9, 89.9)}
INPUT_FLAGS = {"attack", "jump", "dash", "crouch", "sneak", "zoom"}
CHECKS = {"min_health", "max_health", "max_pending_commands", "max_correction_distance",
          "min_planar_speed", "max_planar_speed", "min_displacement", "max_displacement"}


def number(value: Any, low: float, high: float, label: str) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or not low <= value <= high:
        raise lg_control.ControlError(f"{label} must be a finite number in {low}..{high}")
    return float(value)


def validate_step(step: Any) -> dict[str, Any]:
    if not isinstance(step, dict) or set(step) - {"label", "input", "capture", "expect"}:
        raise lg_control.ControlError("a step accepts only label, input, capture, and expect")
    label = step.get("label", "step")
    if not isinstance(label, str) or not 1 <= len(label) <= 120:
        raise lg_control.ControlError("step label must be 1..120 characters")
    capture = step.get("capture", True)
    if not isinstance(capture, bool):
        raise lg_control.ControlError("capture must be boolean")
    action = step.get("input")
    if isinstance(action, list):
        if not 1 <= len(action) <= 64:
            raise lg_control.ControlError("input sequence must have 1..64 segments")
        for segment in action:
            if not isinstance(segment, dict):
                raise lg_control.ControlError("input segments must be objects")
            validate_step({"input": segment, "capture": False})
        if input_ticks(action) > 1250:
            raise lg_control.ControlError("input sequence exceeds 1250 ticks (ten seconds)")
    elif action is not None:
        if not isinstance(action, dict) or set(action) - (set(INPUT_NUMBERS) | INPUT_FLAGS | {"ticks", "weapon", "one_tick_edges"}):
            raise lg_control.ControlError("unknown input field")
        ticks = action.get("ticks")
        if isinstance(ticks, bool) or not isinstance(ticks, int) or not 1 <= ticks <= 250:
            raise lg_control.ControlError("input ticks must be an integer in 1..250 (at most two seconds)")
        for field, (low, high) in INPUT_NUMBERS.items():
            if field in action:
                number(action[field], low, high, field)
        for field in INPUT_FLAGS:
            if field in action and not isinstance(action[field], bool):
                raise lg_control.ControlError(f"{field} must be boolean")
        if ("yaw" in action) != ("pitch" in action):
            raise lg_control.ControlError("yaw and pitch must be supplied together")
        if "one_tick_edges" in action:
            edges = action["one_tick_edges"]
            if not isinstance(edges, list) or any(not isinstance(edge, str) or edge not in INPUT_FLAGS for edge in edges) or len(edges) != len(set(edges)):
                raise lg_control.ControlError("one_tick_edges must contain unique action names")
        if "weapon" in action and (not isinstance(action["weapon"], str) or
                                  not action["weapon"].isalnum() or len(action["weapon"]) > 32):
            raise lg_control.ControlError("weapon must be a short alphanumeric token")
    checks = step.get("expect", {})
    if not isinstance(checks, dict) or set(checks) - CHECKS:
        raise lg_control.ControlError("unknown playtest expectation")
    for key, value in checks.items():
        number(value, 0, 1000000, key)
    return {"label": label, "input": action, "capture": capture, "expect": checks}


def input_ticks(action: dict[str, Any] | list[dict[str, Any]] | None) -> int:
    return sum(segment["ticks"] for segment in action) if isinstance(action, list) else (action or {}).get("ticks", 0)


def validate_plan(plan: Any) -> dict[str, Any]:
    if not isinstance(plan, dict) or set(plan) - {"version", "expected_map", "expected_phase", "steps"}:
        raise lg_control.ControlError("plan accepts only version, expected_map, expected_phase, and steps")
    if type(plan.get("version")) is not int or plan["version"] != 1:
        raise lg_control.ControlError("plan version must be 1")
    for field in ("expected_map", "expected_phase"):
        if field in plan and (not isinstance(plan[field], str) or not 1 <= len(plan[field]) <= 120):
            raise lg_control.ControlError(f"{field} must be a nonempty string")
    steps = plan.get("steps")
    if not isinstance(steps, list) or not 1 <= len(steps) <= 64:
        raise lg_control.ControlError("a plan needs 1..64 steps")
    normalized = [validate_step(step) for step in steps]
    if sum(input_ticks(step["input"]) for step in normalized) > 7500:
        raise lg_control.ControlError("a plan may contain at most 60 seconds of input")
    return {**plan, "steps": normalized}


def _position(state: dict[str, Any]) -> list[float] | None:
    value = (state.get("predicted_local_player") or {}).get("position")
    return value if isinstance(value, list) and len(value) == 3 else None


def assess(before: dict[str, Any], after: dict[str, Any], checks: dict[str, float]) -> tuple[dict[str, Any], list[str]]:
    player = after.get("predicted_local_player") or {}
    velocity = player.get("velocity")
    start, end = _position(before), _position(after)
    metrics = {
        "health": player.get("health"),
        "pending_commands": after.get("pending_command_count"),
        "correction_distance": after.get("last_correction_distance"),
        "planar_speed": math.hypot(*velocity[:2]) if isinstance(velocity, list) and len(velocity) == 3 else None,
        "displacement": math.dist(start, end) if start is not None and end is not None else None,
    }
    failures = []
    for check, limit in checks.items():
        direction, metric = check.split("_", 1)
        value = metrics[metric]
        if value is None or not isinstance(value, (int, float)) or not math.isfinite(value):
            failures.append(f"{check}: measurement unavailable")
        elif (direction == "min" and value < limit) or (direction == "max" and value > limit):
            failures.append(f"{check}: {value:.4g}, expected {'>=' if direction == 'min' else '<='} {limit}")
    return metrics, failures


class PlaytestSession:
    """A small reusable Python API; each request is pinned before game execution."""
    def __init__(self, port: int, *, session_id: str | None = None, timeout: float = 8,
                 allow_fallback: bool = False, expected_map: str | None = None,
                 expected_phase: str | None = None):
        number(port, 1, 65535, "port")
        if not isinstance(port, int):
            raise lg_control.ControlError("port must be an integer")
        number(timeout, 1, 30, "timeout")
        self.port, self.timeout = port, timeout
        self.initial_status = attach_client(
            control_port=port, timeout=timeout,
            renderer="fallback" if allow_fallback else "gpu", allow_fallback=allow_fallback,
            expected_control_session=session_id,
        )
        self.session_id = self.initial_status.get("control_session_id")
        if not isinstance(self.session_id, str) or not self.session_id:
            raise lg_control.ControlError("client lacks session pinning; rebuild the client before playtesting")
        self.expected_map = expected_map or self.initial_status.get("map")
        self.expected_phase = expected_phase
        self.broken = False
        self._check_status(self.initial_status)

    def _check_status(self, status: dict[str, Any]) -> None:
        if status.get("connected") is not True or status.get("spectator") is True:
            raise lg_control.ControlError("playtest requires a connected player, not a spectator")
        if status.get("map") != self.expected_map:
            raise lg_control.ControlError(f"map changed: expected {self.expected_map}, got {status.get('map')}")
        if self.expected_phase and status.get("match_state") != self.expected_phase:
            raise lg_control.ControlError(f"phase changed: expected {self.expected_phase}, got {status.get('match_state')}")

    def request(self, operation: str, **parameters: Any) -> dict[str, Any]:
        if self.broken:
            raise lg_control.ControlError("session stopped after an uncertain request; inspect and attach again")
        try:
            return lg_control.send_request(operation, port=self.port, timeout=self.timeout,
                                           expected_control_session=self.session_id, **parameters)
        except lg_control.ControlError:
            # A timed-out action may still finish. Never retry or send a release
            # into a possibly replaced session; bounded send_input releases itself.
            self.broken = True
            raise

    def step(self, step: dict[str, Any]) -> dict[str, Any]:
        spec = validate_step(step)
        started = time.monotonic()
        status = self.request("status")
        self._check_status(status)
        before = self.request("get_client_state")
        action_result = None
        action_started = time.monotonic()
        if spec["input"] is not None:
            if self.timeout < input_ticks(spec["input"]) / 125 + 1:
                raise lg_control.ControlError("timeout must allow the input duration plus one second for acknowledgement")
            if isinstance(spec["input"], list):
                action_result = self.request("send_input_sequence", segments=spec["input"])
            else:
                action_result = self.request("send_input", **spec["input"])
        action_finished = time.monotonic()
        # Measure immediately after release, before expensive image readback.
        after = self.request("get_client_state")
        capture = None
        if spec["capture"]:
            capture = self.request("capture_screenshot", name="playtest-" + uuid.uuid4().hex,
                                   hide_hud=False, hide_overlays=False)
        final_status = self.request("status")
        self._check_status(final_status)
        finished = time.monotonic()
        metrics, failures = assess(before, after, spec["expect"])
        capture_tick = (capture or {}).get("frame_state", {}).get("latest_snapshot_tick")
        state_tick = after.get("latest_snapshot_tick")
        return {
            "label": spec["label"], "control_session_id": self.session_id,
            "control_port": self.port, "input": spec["input"], "input_result": action_result,
            "status": final_status, "before": before, "after": after, "capture": capture,
            "metrics": metrics, "checks": spec["expect"], "failures": failures,
            "outcome": "failed" if failures else "passed" if spec["expect"] else "recorded",
            "timing": {
                "total_ms": (finished - started) * 1000,
                "action_and_ack_ms": (action_finished - action_started) * 1000,
                "observation_ms": (finished - action_finished) * 1000,
                "capture_state_skew_ticks": capture_tick - state_tick
                    if isinstance(capture_tick, (int, float)) and isinstance(state_tick, (int, float)) else None,
            },
            "observation_note": "Local state precedes the HUD capture; the server continues running between actions. This is not a latency benchmark.",
        }


def write_report(directory: Path, result: dict[str, Any]) -> None:
    rows = []
    for index, step in enumerate(result["steps"]):
        capture = step.get("capture") or {}
        picture = ""
        if capture.get("path"):
            source = Path(capture["path"])
            if source.is_file():
                filename = f"{index:03d}.png"
                shutil.copyfile(source, directory / filename)
                capture["source_path"] = str(source)
                capture["path"] = str((directory / filename).resolve())
                picture = f'<a href="{filename}"><img src="{filename}" alt="HUD capture"></a>'
        detail = html.escape(json.dumps({key: step.get(key) for key in ("input", "metrics", "timing", "failures")}, indent=2))
        rows.append(f'<article><h2>{html.escape(step["label"])} — {step["outcome"]}</h2>{picture}<pre>{detail}</pre></article>')
    (directory / "result.json").write_text(json.dumps(result, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    error = html.escape(result.get("error", ""))
    (directory / "index.html").write_text('''<!doctype html><meta charset="utf-8"><title>LG Duel playtest</title>
<style>body{font:16px system-ui;background:#111923;color:#e9eff8;max-width:1100px;margin:32px auto;padding:0 24px}img{width:100%}article{margin:32px 0}pre{white-space:pre-wrap;background:#1c2938;padding:16px}</style>
<h1>LG Duel playtest evidence</h1><p>Action endpoints with the real HUD. State and screenshots are sampled consecutively. Gaps include rendering, readback and tool overhead; this is not a continuous video or a frame-time benchmark.</p>
''' + f'<p>Outcome: {result["outcome"]}. {error}</p>' + "".join(rows), encoding="utf-8")


def run_plan(plan: dict[str, Any], session: PlaytestSession, directory: Path) -> dict[str, Any]:
    plan = validate_plan(plan)  # Validate everything before the first input.
    if plan.get("expected_map") and plan["expected_map"] != session.expected_map:
        raise lg_control.ControlError("plan expected_map does not match the attached map")
    if plan.get("expected_phase"):
        session.expected_phase = plan["expected_phase"]
    session._check_status(session.initial_status)
    directory.mkdir(parents=True, exist_ok=False)
    result: dict[str, Any] = {"version": 1, "outcome": "recorded", "steps": [], "plan": plan,
                              "control_port": session.port, "control_session_id": session.session_id}
    result["initial_status"] = session.initial_status
    try:
        with (directory / "events.jsonl").open("x", encoding="utf-8") as log:
            for spec in plan["steps"]:
                log.write(json.dumps({"event": "step_started", "step": spec}) + "\n")
                log.flush()
                row = session.step(spec)
                result["steps"].append(row)
                log.write(json.dumps(row, allow_nan=False) + "\n")
                log.flush()
                if row["failures"]:
                    result["outcome"] = "failed"
                    break
            else:
                if any(step["expect"] for step in plan["steps"]):
                    result["outcome"] = "passed"
    except (lg_control.ControlError, LaunchError) as error:
        result["outcome"] = "incomplete"
        result["error"] = str(error)
    finally:
        write_report(directory, result)
    return {"outcome": result["outcome"], "steps_completed": len(result["steps"]),
            "report_path": str((directory / "index.html").resolve()),
            "result_path": str((directory / "result.json").resolve()), "error": result.get("error")}


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--session-id")
    parser.add_argument("--timeout", type=float, default=8)
    parser.add_argument("--allow-fallback", action="store_true")
    parser.add_argument("--expected-map")
    parser.add_argument("--expected-phase")
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("observe")
    commands.add_parser("doctor")
    commands.add_parser("interactive", help="one JSON step per stdin line; one observation per stdout line")
    step = commands.add_parser("step")
    step.add_argument("input_json", help='e.g. \'{"ticks":25,"forward":1}\'')
    run = commands.add_parser("run")
    run.add_argument("plan", type=Path)
    run.add_argument("--output", type=Path)
    args = parser.parse_args(argv)
    try:
        plan = validate_plan(json.loads(args.plan.read_text())) if args.command == "run" else None
        spec = validate_step({"input": json.loads(args.input_json)}) if args.command == "step" else None
        session = PlaytestSession(args.port, session_id=args.session_id, timeout=args.timeout,
            allow_fallback=args.allow_fallback,
            expected_map=args.expected_map or (plan or {}).get("expected_map"),
            expected_phase=args.expected_phase or (plan or {}).get("expected_phase"))
        if args.command == "interactive":
            print(json.dumps({"attached": True, "control_session_id": session.session_id}), flush=True)
            for line in sys.stdin:
                if not line.strip(): continue
                row = session.step(json.loads(line))
                print(json.dumps(row, allow_nan=False), flush=True)
            return 0
        if args.command == "run":
            directory = args.output or DEFAULT_OUTPUT / (time.strftime("%Y%m%d-%H%M%S-") + uuid.uuid4().hex[:8])
            result = run_plan(plan, session, directory)
        elif args.command == "doctor":
            samples = []
            for _ in range(5):
                started = time.monotonic()
                session.request("get_client_state")
                samples.append((time.monotonic() - started) * 1000)
            result = {"attached": True, "control_session_id": session.session_id,
                      "status": session.initial_status, "state_roundtrip_ms": samples,
                      "state_roundtrip_mean_ms": sum(samples) / len(samples),
                      "capture_tested": False, "automatic_launch": False}
        else:
            result = session.step(spec or {"label": "observe"})
        print(json.dumps(result, allow_nan=False))
        return 1 if result.get("outcome") in {"failed", "incomplete"} else 0
    except (lg_control.ControlError, LaunchError, OSError, ValueError) as error:
        print(json.dumps({"outcome": "incomplete", "error": str(error)}), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
