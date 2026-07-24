#!/usr/bin/env python3
"""Build deterministic frame timeline analysis, HTML, and SVG artifacts."""

from __future__ import annotations

import argparse
import csv
import html
import json
import math
import statistics
import sys
from pathlib import Path
from typing import Any, Iterable


FORMAT = "lg-duel-frame-timeline-analysis"
SCHEMA_VERSION = 1
CPU_TOTAL_KEYS = ("total_cpu_ms", "frame_ms", "cpu_ms")
GPU_TOTAL_KEYS = ("total_gpu_ms", "gpu_ms")
LEGACY_META = {"frame", "elapsed_seconds", "frame_ms"}
LEGACY_WORKLOAD_HINTS = (
    "vertices", "triangles", "draws", "ranges", "chunks", "nodes",
    "players", "projectiles", "effects", "bytes", "count",
)

METHOD = {
    "baseline": "median frame time and scaled median absolute deviation (MAD)",
    "confidence_scale": (
        "0 to 1 deterministic rule strength, not a statistical probability; "
        "an absent pattern means the stated rule did not pass"
    ),
    "spike": {
        "threshold": "max(median + 4 * scaled_MAD, 1.5 * median, median + 1 ms)",
        "minimum_frames": 1,
    },
    "isolated": {
        "rule": "a spike with no other spike within two frame indices",
        "confidence": "severity above the spike threshold, capped at 1",
    },
    "burst": {
        "rule": "at least two spikes, each separated by no more than two frames",
        "confidence": "spike density times group-size confidence",
    },
    "periodic": {
        "rule": "at least four spikes; interval coefficient of variation <= 0.20",
        "confidence": "interval regularity times sample-size confidence",
    },
    "sustained": {
        "threshold": "max(median + 2 * scaled_MAD, 1.2 * median, median + 0.5 ms)",
        "rule": "at least eight consecutive frames above the sustained threshold",
        "confidence": "run-length confidence times mean severity",
    },
    "sawtooth": {
        "rule": "at least 12 frames, lag-1 correlation <= -0.50, and even/odd median gap >= max(0.5 ms, 10% of median)",
        "confidence": "negative-correlation strength times alternating-gap strength",
    },
    "attribution": {
        "rule": "largest named CPU subsystem on a spike when it is positive and at least 20% of total CPU time",
        "claim": "correlation/attribution only; it does not prove cause",
    },
}


class ReportError(RuntimeError):
    """A clear input or report error."""


def _number(value: Any) -> float | None:
    if value is None or isinstance(value, bool):
        return None
    try:
        result = float(value)
    except (TypeError, ValueError):
        return None
    return result if math.isfinite(result) else None


def _mapping_numbers(value: Any) -> dict[str, float]:
    if not isinstance(value, dict):
        return {}
    return {
        str(key): number
        for key, raw in sorted(value.items())
        if (number := _number(raw)) is not None
    }


def _events(value: Any) -> list[dict[str, Any]]:
    if not isinstance(value, list):
        return []
    result: list[dict[str, Any]] = []
    for item in value:
        if isinstance(item, str):
            result.append({"name": item})
        elif isinstance(item, dict):
            clean = {
                str(key): raw
                for key, raw in sorted(item.items())
                if isinstance(raw, (str, int, float, bool, list)) or raw is None
            }
            if "name" not in clean and isinstance(clean.get("type"), str):
                clean["name"] = clean["type"]
            result.append(clean)
    return result


def _first_number(row: dict[str, Any], keys: Iterable[str]) -> float | None:
    for key in keys:
        value = _number(row.get(key))
        if value is not None:
            return value
    return None


def _normalize_frame(row: dict[str, Any], offset: int) -> dict[str, Any]:
    index = _first_number(row, ("frame", "frame_index", "index"))
    elapsed = _first_number(
        row,
        ("elapsed_time_seconds", "elapsed_seconds", "elapsed_s", "timestamp_seconds"),
    )
    cpu = _first_number(row, CPU_TOTAL_KEYS)
    gpu = _first_number(row, GPU_TOTAL_KEYS)
    cpu_parts = _mapping_numbers(
        row.get("cpu_subsystems_ms", row.get("cpu_subsystems", row.get("cpu", {})))
    )
    gpu_parts = _mapping_numbers(
        row.get("gpu_subsystems_ms", row.get("gpu_subsystems", row.get("gpu", {})))
    )
    # Some producers put total_ms beside named values in the cpu/gpu object.
    if cpu is None:
        cpu = _first_number(cpu_parts, ("total_ms", "total"))
    if gpu is None:
        gpu = _first_number(gpu_parts, ("total_ms", "total"))
    cpu_parts = {key: value for key, value in cpu_parts.items() if key not in {"total", "total_ms"}}
    gpu_parts = {key: value for key, value in gpu_parts.items() if key not in {"total", "total_ms"}}
    if cpu is None:
        raise ReportError(f"frame {offset} has no total CPU/frame time")
    workload = _mapping_numbers(row.get("workload", row.get("workload_counters", {})))
    return {
        "frame": int(index if index is not None else offset),
        "elapsed_seconds": elapsed,
        "total_cpu_ms": cpu,
        "total_gpu_ms": gpu,
        "cpu_subsystems_ms": cpu_parts,
        "gpu_subsystems_ms": gpu_parts,
        "workload": workload,
        "events": _events(row.get("events", row.get("event_markers", []))),
    }


def _load_json(path: Path) -> tuple[list[dict[str, Any]], dict[str, Any]]:
    try:
        root = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise ReportError(f"could not read {path}: {exc}") from exc
    if not isinstance(root, dict):
        raise ReportError(f"{path} must be an object with a frames array")
    root_fields = root
    if root_fields.get("format") != "lg-duel-frame-timeline":
        raise ReportError(f"{path} has unsupported timeline format")
    if root_fields.get("schema_version") != 1:
        raise ReportError(f"{path} has unsupported timeline schema version")
    raw_frames = root_fields.get("frames")
    if not isinstance(raw_frames, list):
        raise ReportError(f"{path} has no frames array")
    frames = [
        _normalize_frame(row, index)
        for index, row in enumerate(raw_frames)
        if isinstance(row, dict)
    ]
    metadata = root_fields.get("metadata", {})
    if not isinstance(metadata, dict):
        metadata = {}
    meta = {
        "format": root_fields["format"],
        "schema_version": root_fields["schema_version"],
        "gpu_execution_timing_available": bool(
            root_fields.get("gpu_execution_timing_available", any(f["total_gpu_ms"] is not None for f in frames))
        ),
        "scenario_hash": metadata.get("scenario_hash", root_fields.get("scenario_hash")),
        "renderer": metadata.get("renderer", root_fields.get("renderer")),
        "resolution": metadata.get(
            "actual_resolution",
            metadata.get("resolution", root_fields.get("actual_resolution", root_fields.get("resolution"))),
        ),
        "label": metadata.get("label"),
    }
    return frames, meta


def _load_csv(path: Path) -> tuple[list[dict[str, Any]], dict[str, Any]]:
    try:
        with path.open(newline="", encoding="utf-8-sig") as stream:
            rows = list(csv.DictReader(stream))
    except OSError as exc:
        raise ReportError(f"could not read {path}: {exc}") from exc
    frames: list[dict[str, Any]] = []
    for offset, row in enumerate(rows):
        normalized: dict[str, Any] = dict(row)
        cpu_parts: dict[str, float] = {}
        workload: dict[str, float] = {}
        for key, raw in row.items():
            number = _number(raw)
            if number is None or key in LEGACY_META:
                continue
            if key.endswith("_ms"):
                cpu_parts[key[:-3]] = number
            elif any(hint in key for hint in LEGACY_WORKLOAD_HINTS):
                workload[key] = number
        normalized["total_cpu_ms"] = row.get("frame_ms")
        normalized["cpu_subsystems_ms"] = cpu_parts
        normalized["workload"] = workload
        normalized["events"] = []
        frames.append(_normalize_frame(normalized, offset))
    return frames, {
        "format": "legacy-telemetry-csv",
        "schema_version": None,
        "gpu_execution_timing_available": False,
        "scenario_hash": None,
        "renderer": None,
        "resolution": None,
        "label": None,
    }


def load_input(value: str | Path) -> tuple[list[dict[str, Any]], dict[str, Any], Path]:
    path = Path(value).resolve()
    if path.is_dir():
        json_path = path / "frame-timeline.json"
        csv_path = path / "telemetry.csv"
        if json_path.is_file():
            path = json_path
        elif csv_path.is_file():
            path = csv_path
        else:
            raise ReportError(f"{path} has neither frame-timeline.json nor telemetry.csv")
    if not path.is_file():
        raise ReportError(f"input does not exist: {path}")
    frames, meta = _load_csv(path) if path.suffix.lower() == ".csv" else _load_json(path)
    if not frames:
        raise ReportError(f"{path} has no usable frames")
    frames.sort(key=lambda frame: (frame["frame"], frame["elapsed_seconds"] or 0.0))
    return frames, meta, path


def _median(values: list[float]) -> float:
    return float(statistics.median(values))


def _correlation(left: list[float], right: list[float]) -> float:
    if len(left) != len(right) or len(left) < 2:
        return 0.0
    lm, rm = statistics.fmean(left), statistics.fmean(right)
    numerator = sum((a - lm) * (b - rm) for a, b in zip(left, right))
    denominator = math.sqrt(
        sum((a - lm) ** 2 for a in left) * sum((b - rm) ** 2 for b in right)
    )
    return numerator / denominator if denominator else 0.0


def _likely_cpu(frame: dict[str, Any]) -> dict[str, Any] | None:
    parts = frame["cpu_subsystems_ms"]
    if not parts:
        return None
    name, value = max(parts.items(), key=lambda item: (item[1], item[0]))
    share = value / frame["total_cpu_ms"] if frame["total_cpu_ms"] > 0 else 0.0
    if value <= 0 or share < 0.20:
        return None
    return {
        "name": name,
        "value_ms": round(value, 6),
        "share_of_total_cpu": round(share, 6),
        "kind": "correlation",
        "claim": "largest recorded named CPU share; not proof of cause",
    }


def _confidence(value: float) -> float:
    return round(max(0.0, min(1.0, value)), 3)


def analyze(frames: list[dict[str, Any]]) -> dict[str, Any]:
    values = [frame["total_cpu_ms"] for frame in frames]
    median = _median(values)
    mad = _median([abs(value - median) for value in values])
    scaled_mad = 1.4826 * mad
    spike_threshold = max(median + 4 * scaled_mad, median * 1.5, median + 1.0)
    sustained_threshold = max(median + 2 * scaled_mad, median * 1.2, median + 0.5)
    spike_positions = [i for i, value in enumerate(values) if value > spike_threshold]
    spike_set = set(spike_positions)
    spike_records: list[dict[str, Any]] = []
    for pos in spike_positions:
        frame = frames[pos]
        record = {
            "frame": frame["frame"],
            "elapsed_seconds": frame["elapsed_seconds"],
            "total_cpu_ms": round(values[pos], 6),
            "severity_ratio": round(values[pos] / spike_threshold, 6),
            "events": frame["events"],
            "likely_cpu_subsystem": _likely_cpu(frame),
        }
        spike_records.append(record)

    isolated = []
    for pos in spike_positions:
        if not any(other != pos and abs(other - pos) <= 2 for other in spike_set):
            isolated.append({
                "frame": frames[pos]["frame"],
                "elapsed_seconds": frames[pos]["elapsed_seconds"],
                "total_cpu_ms": round(values[pos], 6),
                "confidence": _confidence((values[pos] / spike_threshold - 1.0) + 0.55),
                "likely_cpu_subsystem": _likely_cpu(frames[pos]),
            })

    groups: list[list[int]] = []
    for pos in spike_positions:
        if not groups or pos - groups[-1][-1] > 2:
            groups.append([pos])
        else:
            groups[-1].append(pos)
    bursts = []
    for group in groups:
        if len(group) < 2:
            continue
        span = group[-1] - group[0] + 1
        bursts.append({
            "start_frame": frames[group[0]]["frame"],
            "end_frame": frames[group[-1]]["frame"],
            "spike_count": len(group),
            "span_frames": span,
            "confidence": _confidence((len(group) / span) * min(1.0, len(group) / 4.0)),
        })

    periodic: list[dict[str, Any]] = []
    if len(spike_positions) >= 4:
        intervals = [
            frames[right]["frame"] - frames[left]["frame"]
            for left, right in zip(spike_positions, spike_positions[1:])
        ]
        mean_interval = statistics.fmean(intervals)
        interval_cv = (
            statistics.pstdev(intervals) / mean_interval if mean_interval > 0 else 1.0
        )
        if interval_cv <= 0.20:
            periodic.append({
                "first_frame": frames[spike_positions[0]]["frame"],
                "last_frame": frames[spike_positions[-1]]["frame"],
                "spike_count": len(spike_positions),
                "mean_interval_frames": round(mean_interval, 6),
                "interval_cv": round(interval_cv, 6),
                "confidence": _confidence((1.0 - interval_cv / 0.20) * min(1.0, len(intervals) / 6.0)),
            })

    sustained: list[dict[str, Any]] = []
    start: int | None = None
    for pos in range(len(values) + 1):
        above = pos < len(values) and values[pos] > sustained_threshold
        if above and start is None:
            start = pos
        if not above and start is not None:
            if pos - start >= 8:
                run = values[start:pos]
                severity = statistics.fmean(run) / sustained_threshold - 1.0
                sustained.append({
                    "start_frame": frames[start]["frame"],
                    "end_frame": frames[pos - 1]["frame"],
                    "frame_count": pos - start,
                    "mean_cpu_ms": round(statistics.fmean(run), 6),
                    "confidence": _confidence(min(1.0, (pos - start) / 16.0) * min(1.0, 0.5 + severity)),
                })
            start = None

    sawtooth: list[dict[str, Any]] = []
    lag_correlation = _correlation(values[:-1], values[1:]) if len(values) >= 12 else 0.0
    even = values[::2]
    odd = values[1::2]
    alternating_gap = abs(_median(even) - _median(odd)) if odd else 0.0
    gap_threshold = max(0.5, median * 0.10)
    if len(values) >= 12 and lag_correlation <= -0.50 and alternating_gap >= gap_threshold:
        sawtooth.append({
            "start_frame": frames[0]["frame"],
            "end_frame": frames[-1]["frame"],
            "lag1_correlation": round(lag_correlation, 6),
            "even_odd_median_gap_ms": round(alternating_gap, 6),
            "confidence": _confidence(
                ((-lag_correlation - 0.5) / 0.5 * 0.5 + 0.5)
                * min(1.0, alternating_gap / (2 * gap_threshold))
            ),
        })

    sorted_values = sorted(values)
    percentile = lambda p: sorted_values[min(len(sorted_values) - 1, math.ceil(p * len(sorted_values)) - 1)]
    pattern_status = {
        "isolated_spikes": {"status": "evaluated", "minimum_frame_count": 1},
        "bursts": {"status": "evaluated", "minimum_frame_count": 2},
        "periodic_spikes": {
            "status": "evaluated" if len(frames) >= 4 else "unavailable",
            "minimum_frame_count": 4,
            "reason": "requires at least four frames" if len(frames) < 4 else None,
        },
        "sustained_regressions": {
            "status": "evaluated" if len(frames) >= 8 else "unavailable",
            "minimum_frame_count": 8,
            "reason": "requires at least eight frames" if len(frames) < 8 else None,
        },
        "sawtooth_alternating": {
            "status": "evaluated" if len(frames) >= 12 else "unavailable",
            "minimum_frame_count": 12,
            "reason": "requires at least twelve frames" if len(frames) < 12 else None,
        },
    }
    return {
        "method": METHOD,
        "summary": {
            "frame_count": len(frames),
            "median_cpu_ms": round(median, 6),
            "p95_cpu_ms": round(percentile(0.95), 6),
            "p99_cpu_ms": round(percentile(0.99), 6),
            "max_cpu_ms": round(max(values), 6),
            "scaled_mad_ms": round(scaled_mad, 6),
            "spike_threshold_ms": round(spike_threshold, 6),
            "sustained_threshold_ms": round(sustained_threshold, 6),
            "gpu_sample_count": sum(frame["total_gpu_ms"] is not None for frame in frames),
        },
        "spikes": spike_records,
        "patterns": {
            "isolated_spikes": isolated,
            "bursts": bursts,
            "periodic_spikes": periodic,
            "sustained_regressions": sustained,
            "sawtooth_alternating": sawtooth,
        },
        "pattern_status": pattern_status,
    }


def compare(
    candidate: dict[str, Any],
    baseline: dict[str, Any],
    candidate_meta: dict[str, Any],
    baseline_meta: dict[str, Any],
) -> dict[str, Any]:
    reasons = []
    for key, label in (
        ("format", "format"),
        ("schema_version", "schema version"),
    ):
        if candidate_meta.get(key) != baseline_meta.get(key):
            reasons.append(
                f"{label} differs: baseline={baseline_meta.get(key)!r}, "
                f"candidate={candidate_meta.get(key)!r}"
            )
    for key, label in (
        ("scenario_hash", "scenario hash"),
        ("renderer", "renderer"),
        ("resolution", "resolution"),
    ):
        old, new = baseline_meta.get(key), candidate_meta.get(key)
        if old is None or new is None:
            reasons.append(f"{label} is missing from one or both sources")
        elif old != new:
            reasons.append(f"{label} differs: baseline={old!r}, candidate={new!r}")
    if reasons:
        return {
            "compatible": False,
            "reasons": reasons,
            "basis": "comparison skipped because source metadata differs",
        }
    candidate_summary = candidate["summary"]
    baseline_summary = baseline["summary"]
    metrics = {}
    for key in ("median_cpu_ms", "p95_cpu_ms", "p99_cpu_ms", "max_cpu_ms"):
        old = baseline_summary[key]
        new = candidate_summary[key]
        metrics[key] = {
            "baseline": old,
            "candidate": new,
            "delta_ms": round(new - old, 6),
            "delta_percent": round((new / old - 1.0) * 100.0, 6) if old else None,
        }
    return {
        "compatible": True,
        "reasons": [],
        "basis": "same metric names; frame counts may differ",
        "baseline_frame_count": baseline_summary["frame_count"],
        "candidate_frame_count": candidate_summary["frame_count"],
        "metrics": metrics,
    }


def _points(
    frames: list[dict[str, Any]],
    width: int,
    height: int,
    *,
    left: int,
    right: int,
    top: int,
    bottom: int,
) -> tuple[str, float]:
    values = [frame["total_cpu_ms"] for frame in frames]
    maximum = max(values) * 1.08 or 1.0
    denominator = max(1, len(values) - 1)
    points = " ".join(
        f"{left + i / denominator * (width - left - right):.2f},"
        f"{height - bottom - value / maximum * (height - top - bottom):.2f}"
        for i, value in enumerate(values)
    )
    return points, maximum


def _timeline_svg(
    frames: list[dict[str, Any]],
    analysis: dict[str, Any],
    *,
    interactive: bool,
    label: str | None = None,
) -> str:
    width, height = 1200, 420
    left, right, top, bottom = 72, 28, 62, 58
    points, maximum = _points(
        frames, width, height, left=left, right=right, top=top, bottom=bottom
    )
    threshold = analysis["summary"]["spike_threshold_ms"]
    plot_width = width - left - right
    plot_height = height - top - bottom
    threshold_y = height - bottom - threshold / maximum * plot_height
    grid = []
    for fraction in range(5):
        value = maximum * fraction / 4
        y = height - bottom - plot_height * fraction / 4
        grid.append(f'<line x1="{left}" y1="{y:.2f}" x2="{width - right}" y2="{y:.2f}" class="grid"/>')
        grid.append(f'<text x="{left - 10}" y="{y + 5:.2f}" text-anchor="end" class="axis-label">{value:.1f}</text>')
    x_ticks = []
    tick_count = min(6, len(frames))
    for tick in range(tick_count):
        position = round(tick * (len(frames) - 1) / max(1, tick_count - 1))
        x = left + position / max(1, len(frames) - 1) * plot_width
        x_ticks.append(f'<line x1="{x:.2f}" y1="{height - bottom}" x2="{x:.2f}" y2="{height - bottom + 6}" class="axis"/>')
        x_ticks.append(f'<text x="{x:.2f}" y="{height - bottom + 24}" text-anchor="middle" class="axis-label">{frames[position]["frame"]}</text>')
    dots = []
    for index, frame in enumerate(frames):
        x = left + index / max(1, len(frames) - 1) * plot_width
        y = height - bottom - frame["total_cpu_ms"] / maximum * plot_height
        events = ", ".join(str(event.get("name", "event")) for event in frame["events"]) or "none"
        title = html.escape(
            f"frame {frame['frame']}; {frame['total_cpu_ms']:.3f} ms; events: {events}"
        )
        if frame["total_cpu_ms"] > threshold:
            dots.append(f'<circle cx="{x:.2f}" cy="{y:.2f}" r="6" class="spike"><title>{title}</title></circle>')
        elif interactive:
            dots.append(f'<circle cx="{x:.2f}" cy="{y:.2f}" r="7" class="hit"><title>{title}</title></circle>')
    extra = ' id="timeline-svg" tabindex="0"' if interactive else ""
    label_text = f'<text x="{left}" y="22" class="demo-label">{html.escape(label)}</text>' if label else ""
    return f"""<svg{extra} xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {width} {height}" role="img" aria-label="CPU frame time timeline">
<style>.plot-bg{{fill:#111827}}.axis{{stroke:#94a3b8;stroke-width:1.5}}.grid{{stroke:#334155;stroke-width:1}}.line{{fill:none;stroke:#38bdf8;stroke-width:3}}.limit{{stroke:#fbbf24;stroke-width:2;stroke-dasharray:8 5}}.spike{{fill:#fb7185;stroke:#fff1f2;stroke-width:2}}.hit{{fill:transparent;stroke:transparent}}.axis-label{{fill:#e2e8f0;font:14px system-ui,sans-serif}}.legend{{fill:#f8fafc;font:14px system-ui,sans-serif}}.demo-label{{fill:#fcd34d;font:700 14px system-ui,sans-serif}}</style>
<rect width="100%" height="100%" class="plot-bg"/>{label_text}{"".join(grid)}
<line x1="{left}" y1="{height - bottom}" x2="{width - right}" y2="{height - bottom}" class="axis"/><line x1="{left}" y1="{top}" x2="{left}" y2="{height - bottom}" class="axis"/>
{"".join(x_ticks)}<line x1="{left}" y1="{threshold_y:.2f}" x2="{width - right}" y2="{threshold_y:.2f}" class="limit"/>
<rect x="{width - 284}" y="{top + 12}" width="242" height="52" rx="5" fill="#1e293b" stroke="#64748b"/><line x1="{width - 270}" y1="{top + 31}" x2="{width - 242}" y2="{top + 31}" class="line"/><text x="{width - 232}" y="{top + 36}" class="legend">CPU frame time</text><circle cx="{width - 256}" cy="{top + 52}" r="5" class="spike"/><text x="{width - 242}" y="{top + 57}" class="legend">spike &gt; {threshold:.2f} ms</text>
<text x="{left}" y="{top - 10}" class="legend">CPU frame time (ms)</text><text x="{width / 2:.2f}" y="{height - 12}" text-anchor="middle" class="legend">Measured frame index</text><polyline points="{points}" class="line"/>{"".join(dots)}</svg>"""


def _histogram_legacy(frames: list[dict[str, Any]]) -> str:
    values = [frame["total_cpu_ms"] for frame in frames]
    low, high = min(values), max(values)
    count = min(20, max(5, int(math.sqrt(len(values)))))
    width = (high - low) / count if high > low else 1.0
    bins = [0] * count
    for value in values:
        bins[min(count - 1, int((value - low) / width)) if width else 0] += 1
    maximum = max(bins)
    bars = []
    for i, value in enumerate(bins):
        x = 30 + i * (720 / count)
        bar_width = 720 / count - 2
        bar_height = value / maximum * 180
        label = f"{low + i * width:.2f}–{low + (i + 1) * width:.2f} ms: {value}"
        bars.append(f'<rect x="{x:.2f}" y="{210 - bar_height:.2f}" width="{bar_width:.2f}" height="{bar_height:.2f}"><title>{html.escape(label)}</title></rect>')
    return f'<svg viewBox="0 0 780 240" role="img" aria-label="Frame time distribution"><style>rect{{fill:#70d6a5}}text{{fill:#c9d5e5;font:13px sans-serif}}</style><rect width="100%" height="100%" fill="#111822"/>{"".join(bars)}<text x="30" y="232">{low:.2f} ms</text><text x="690" y="232">{high:.2f} ms</text></svg>'


def _histogram(frames: list[dict[str, Any]], *, label: str | None = None) -> str:
    values = [frame["total_cpu_ms"] for frame in frames]
    low, high = min(values), max(values)
    count = min(20, max(5, int(math.sqrt(len(values)))))
    bin_width = (high - low) / count if high > low else 1.0
    bins = [0] * count
    for value in values:
        bins[min(count - 1, int((value - low) / bin_width)) if bin_width else 0] += 1
    maximum = max(bins)
    svg_width, svg_height = 960, 360
    left, right, top, bottom = 70, 26, 60, 62
    plot_width, plot_height = svg_width - left - right, svg_height - top - bottom
    bars = []
    for i, value in enumerate(bins):
        x = left + i * plot_width / count + 3
        bar_width = plot_width / count - 6
        bar_height = value / maximum * plot_height
        bar_label = f"{low + i * bin_width:.2f}-{low + (i + 1) * bin_width:.2f} ms: {value} frames"
        bars.append(f'<rect x="{x:.2f}" y="{top + plot_height - bar_height:.2f}" width="{bar_width:.2f}" height="{bar_height:.2f}" rx="2" class="bar"><title>{html.escape(bar_label)}</title></rect>')
        bars.append(f'<text x="{x + bar_width / 2:.2f}" y="{top + plot_height - bar_height - 7:.2f}" text-anchor="middle" class="bar-count">{value}</text>')
    grid = []
    for tick in range(5):
        value = maximum * tick / 4
        y = top + plot_height - plot_height * tick / 4
        grid.append(f'<line x1="{left}" y1="{y:.2f}" x2="{svg_width - right}" y2="{y:.2f}" class="grid"/><text x="{left - 10}" y="{y + 5:.2f}" text-anchor="end" class="axis-label">{value:.0f}</text>')
    x_labels = []
    label_count = min(6, count + 1)
    for tick in range(label_count):
        bin_index = round(tick * count / max(1, label_count - 1))
        x = left + bin_index / count * plot_width
        value = low + bin_index * bin_width
        x_labels.append(f'<line x1="{x:.2f}" y1="{top + plot_height}" x2="{x:.2f}" y2="{top + plot_height + 6}" class="axis"/><text x="{x:.2f}" y="{svg_height - 29}" text-anchor="middle" class="axis-label">{value:.1f}</text>')
    label_text = f'<text x="{left}" y="20" class="demo-label">{html.escape(label)}</text>' if label else ""
    return f'''<svg viewBox="0 0 {svg_width} {svg_height}" role="img" aria-label="Frame time distribution histogram"><style>.plot-bg{{fill:#111827}}.bar{{fill:#22c55e;stroke:#bbf7d0;stroke-width:1}}.axis{{stroke:#94a3b8;stroke-width:1.5}}.grid{{stroke:#334155;stroke-width:1}}.axis-label{{fill:#e2e8f0;font:13px system-ui,sans-serif}}.bar-count{{fill:#f8fafc;font:700 13px system-ui,sans-serif}}.legend{{fill:#f8fafc;font:14px system-ui,sans-serif}}.demo-label{{fill:#fcd34d;font:700 14px system-ui,sans-serif}}</style><rect width="100%" height="100%" class="plot-bg"/>{label_text}{"".join(grid)}<line x1="{left}" y1="{top + plot_height}" x2="{svg_width - right}" y2="{top + plot_height}" class="axis"/><line x1="{left}" y1="{top}" x2="{left}" y2="{top + plot_height}" class="axis"/>{"".join(x_labels)}{"".join(bars)}<text x="{left}" y="{top - 9}" class="legend">Frame count per time bin</text><text x="{svg_width / 2:.2f}" y="{svg_height - 7}" text-anchor="middle" class="legend">CPU frame time (ms)</text></svg>'''


def _pattern_rows(analysis: dict[str, Any]) -> str:
    labels = {
        "isolated_spikes": "Isolated spikes",
        "bursts": "Bursts",
        "periodic_spikes": "Periodic spikes",
        "sustained_regressions": "Sustained regressions",
        "sawtooth_alternating": "Sawtooth / alternating",
    }
    rows = []
    for key, label in labels.items():
        items = analysis["patterns"][key]
        confidence = max((item.get("confidence", 0.0) for item in items), default=0.0)
        status = analysis["pattern_status"][key]
        detail = status["status"]
        if status.get("reason"):
            detail += f": {status['reason']}"
        rows.append(
            f"<tr><td>{label}</td><td>{html.escape(detail)}</td>"
            f"<td>{len(items)}</td><td>{confidence:.3f}</td></tr>"
        )
    return "".join(rows)


def _worst_rows(frames: list[dict[str, Any]], count: int = 20) -> str:
    rows = []
    for frame in sorted(frames, key=lambda item: (-item["total_cpu_ms"], item["frame"]))[:count]:
        likely = _likely_cpu(frame)
        subsystem = likely["name"] if likely else "—"
        events = ", ".join(str(event.get("name", "event")) for event in frame["events"]) or "—"
        gpu = "—" if frame["total_gpu_ms"] is None else f'{frame["total_gpu_ms"]:.3f}'
        elapsed = "—" if frame["elapsed_seconds"] is None else f'{frame["elapsed_seconds"]:.6f}'
        rows.append(
            f"<tr><td>{frame['frame']}</td><td>{elapsed}</td><td>{frame['total_cpu_ms']:.3f}</td>"
            f"<td>{gpu}</td><td>{html.escape(subsystem)}</td><td>{html.escape(events)}</td></tr>"
        )
    return "".join(rows)


def _comparison_html(value: dict[str, Any] | None) -> str:
    if value is None:
        return "<p>No baseline supplied.</p>"
    if not value.get("compatible"):
        reasons = "".join(f"<li>{html.escape(reason)}</li>" for reason in value["reasons"])
        return f"<p>No comparison: source data is not compatible.</p><ul>{reasons}</ul>"
    rows = []
    for name, metric in value["metrics"].items():
        percent = "—" if metric["delta_percent"] is None else f'{metric["delta_percent"]:+.2f}%'
        rows.append(
            f"<tr><td>{html.escape(name)}</td><td>{metric['baseline']:.3f}</td>"
            f"<td>{metric['candidate']:.3f}</td><td>{metric['delta_ms']:+.3f}</td><td>{percent}</td></tr>"
        )
    return f"<p>{html.escape(value['basis'])}</p><table><thead><tr><th>Metric</th><th>Baseline</th><th>Candidate</th><th>Δ ms</th><th>Δ %</th></tr></thead><tbody>{''.join(rows)}</tbody></table>"


def render_html(
    frames: list[dict[str, Any]],
    analysis: dict[str, Any],
    comparison: dict[str, Any] | None,
    source_label: str | None = None,
) -> str:
    summary = analysis["summary"]
    timeline = _timeline_svg(frames, analysis, interactive=True, label=source_label)
    notice = (
        f'<p class="notice"><strong>{html.escape(source_label)}</strong></p>'
        if source_label else ""
    )
    return f"""<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width">
<title>LG Duel frame timeline</title><style>
:root{{color-scheme:dark}}body{{font:16px/1.5 system-ui,sans-serif;background:#020617;color:#f1f5f9;margin:0 auto;max-width:1280px;padding:28px}}
h1,h2{{color:#ffffff;letter-spacing:.01em}}.cards{{display:flex;gap:12px;flex-wrap:wrap}}.card{{min-width:120px;background:#172554;border:1px solid #475569;padding:12px 18px;border-radius:8px;color:#e0f2fe}}
.chart{{overflow:hidden;border:1px solid #64748b;border-radius:8px;background:#111827;box-shadow:0 2px 12px #0008}}svg{{display:block;width:100%;height:auto}}table{{border-collapse:collapse;width:100%;background:#0f172a}}
th,td{{padding:8px 10px;border-bottom:1px solid #334155;text-align:right}}th{{background:#1e293b;color:#f8fafc}}th:first-child,td:first-child{{text-align:left}}
code{{color:#fcd34d}}button{{margin:8px 0;padding:8px 12px;border:1px solid #93c5fd;border-radius:5px;background:#1d4ed8;color:#fff;font-weight:700;cursor:pointer}}.note{{color:#cbd5e1}}.notice{{border:1px solid #fbbf24;border-radius:6px;background:#78350f;color:#fef3c7;padding:10px 12px}}
</style></head><body>{notice}<h1>LG Duel frame timeline</h1>
<p class="note">Raw frame data remains the source of truth. Hover points to inspect them. Wheel or drag the timeline to zoom and pan; use Reset to restore it.</p>
<div class="cards"><div class="card">Frames<br><strong>{summary['frame_count']}</strong></div>
<div class="card">Median CPU<br><strong>{summary['median_cpu_ms']:.3f} ms</strong></div>
<div class="card">P95 CPU<br><strong>{summary['p95_cpu_ms']:.3f} ms</strong></div>
<div class="card">Worst CPU<br><strong>{summary['max_cpu_ms']:.3f} ms</strong></div>
<div class="card">GPU samples<br><strong>{summary['gpu_sample_count']}</strong></div></div>
<h2>Timeline</h2><button id="reset">Reset timeline</button><div class="chart">{timeline}</div>
<h2>Frame-time distribution</h2><div class="chart">{_histogram(frames, label=source_label)}</div>
<h2>Pattern summary</h2><table><thead><tr><th>Pattern</th><th>Status</th><th>Groups</th><th>Top confidence</th></tr></thead><tbody>{_pattern_rows(analysis)}</tbody></table>
<p class="note">The report records all cutoffs and rules in <code>timeline-analysis.json</code>. Named subsystem values show correlation only, not cause.</p>
<h2>Worst frames</h2><table><thead><tr><th>Frame</th><th>Elapsed s</th><th>CPU ms</th><th>GPU ms</th><th>Largest CPU share</th><th>Events</th></tr></thead><tbody>{_worst_rows(frames)}</tbody></table>
<h2>Baseline comparison</h2>{_comparison_html(comparison)}
<script>
(() => {{ const svg=document.getElementById("timeline-svg"), original=[0,0,1200,420]; let box=original.slice(), drag=null;
const apply=()=>svg.setAttribute("viewBox",box.join(" "));
svg.addEventListener("wheel",e=>{{e.preventDefault();const k=e.deltaY<0?.8:1.25,r=svg.getBoundingClientRect(),x=box[0]+e.offsetX/r.width*box[2];let w=Math.max(80,Math.min(1200,box[2]*k));box[0]=Math.max(0,Math.min(1200-w,x-(x-box[0])*w/box[2]));box[2]=w;apply();}},{{passive:false}});
svg.addEventListener("pointerdown",e=>{{drag=[e.clientX,box[0]];svg.setPointerCapture(e.pointerId)}});
svg.addEventListener("pointermove",e=>{{if(!drag)return;let d=(e.clientX-drag[0])/svg.getBoundingClientRect().width*box[2];box[0]=Math.max(0,Math.min(1200-box[2],drag[1]-d));apply()}});
svg.addEventListener("pointerup",()=>drag=null);document.getElementById("reset").onclick=()=>{{box=original.slice();apply()}};
}})();
</script></body></html>"""


def write_report(
    input_value: str | Path,
    output: str | Path,
    baseline_value: str | Path | None = None,
) -> dict[str, Any]:
    frames, source_meta, source_path = load_input(input_value)
    analysis = analyze(frames)
    baseline_analysis = None
    comparison = None
    baseline_source = None
    if baseline_value is not None:
        baseline_frames, baseline_meta, baseline_path = load_input(baseline_value)
        baseline_analysis = analyze(baseline_frames)
        comparison = compare(analysis, baseline_analysis, source_meta, baseline_meta)
        baseline_source = baseline_path.name
    artifact = {
        "format": FORMAT,
        "schema_version": SCHEMA_VERSION,
        "source": {
            "file": source_path.name,
            "format": source_meta["format"],
            "schema_version": source_meta["schema_version"],
            "gpu_execution_timing_available": source_meta["gpu_execution_timing_available"],
        },
        "analysis": analysis,
        "comparison": comparison,
    }
    if baseline_source is not None:
        artifact["baseline_source"] = {"file": baseline_source}
    output_path = Path(output)
    output_path.mkdir(parents=True, exist_ok=True)
    (output_path / "timeline-analysis.json").write_text(
        json.dumps(artifact, indent=2, sort_keys=True, allow_nan=False) + "\n",
        encoding="utf-8",
    )
    (output_path / "frame-timeline.svg").write_text(
        _timeline_svg(frames, analysis, interactive=False, label=source_meta.get("label")) + "\n", encoding="utf-8"
    )
    (output_path / "frame-timeline.html").write_text(
        render_html(frames, analysis, comparison, source_meta.get("label")), encoding="utf-8"
    )
    return artifact


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", help="native run directory, frame-timeline.json, or telemetry.csv")
    parser.add_argument("--output", required=True, help="directory for report artifacts")
    parser.add_argument("--baseline", help="optional compatible baseline directory or file")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    try:
        write_report(args.input, args.output, args.baseline)
    except ReportError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
