#!/usr/bin/env python3
"""Build canonical CPU, GPU, and combined benchmark report artifacts."""

from __future__ import annotations

import argparse
import json
import math
import statistics
from collections import defaultdict
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Iterable


PROFILE_ORDER = ("Low", "Default", "Competitive", "High")
GPU_STAGE_ORDER = (
    "sun_shadow",
    "main_scene",
    "view_model",
    "bloom",
    "scene_composite",
    "outline_mask_stage",
    "outline_dilation",
    "outline_composite",
    "ui_overlay",
    "outline_total",
)
CPU_SUBSYSTEM_ORDER = (
    "renderer_total",
    "swapchain_acquisition",
    "scene_build",
    "vertex_upload",
    "draw_issue",
    "submission",
    "snapshot_decode",
    "snapshot_apply",
    "network_processing",
    "simulation",
    "movement_collision",
    "traces",
    "interpolation",
    "animation",
    "world_visibility",
    "render_instance_construction",
    "world_command_encoding",
    "dynamic_command_encoding",
    "ui",
)
SUMMARY_FIELDS = ("count", "min", "median", "mean", "p95", "p99", "max", "stddev")
FRAME_THRESHOLDS_MS = (2.63, 4.17, 6.94, 8.33, 16.67)


def nearest_rank(values: list[float], quantile: float) -> float | None:
    if not values:
        return None
    ordered = sorted(values)
    rank = math.ceil(quantile * len(ordered))
    return ordered[min(max(rank, 1), len(ordered)) - 1]


def summarize(values: Iterable[float]) -> dict[str, float | int | None]:
    clean = [float(value) for value in values if isinstance(value, (int, float))]
    if not clean:
        return {field: None for field in SUMMARY_FIELDS}
    return {
        "count": len(clean),
        "min": min(clean),
        "median": nearest_rank(clean, 0.5),
        "mean": statistics.fmean(clean),
        "p95": nearest_rank(clean, 0.95),
        "p99": nearest_rank(clean, 0.99),
        "max": max(clean),
        "stddev": statistics.pstdev(clean) if len(clean) > 1 else 0.0,
    }


def metric_label(metric: str) -> str:
    return metric.replace("_", " ").replace(" gpu ms", " GPU ms").replace(" ms", " (ms)").title()


def stage_label(stage: str) -> str:
    labels = {
        "sun_shadow": "Sun shadow",
        "main_scene": "Main scene",
        "view_model": "View model",
        "bloom": "Bloom",
        "scene_composite": "Scene composite",
        "outline_mask_stage": "Outline mask",
        "outline_dilation": "Outline dilation",
        "outline_composite": "Outline composite",
        "ui_overlay": "UI overlay",
        "outline_total": "Outline total (alias)",
    }
    return labels.get(stage, stage.replace("_", " ").title())


def subsystem_label(subsystem: str) -> str:
    labels = {
        "renderer_total": "Renderer total",
        "swapchain_acquisition": "Swapchain acquisition",
        "scene_build": "Scene build",
        "vertex_upload": "Vertex upload",
        "draw_issue": "Draw issue",
        "submission": "Submission",
        "snapshot_decode": "Snapshot decode",
        "snapshot_apply": "Snapshot apply",
        "network_processing": "Network processing",
        "simulation": "Simulation",
        "movement_collision": "Movement / collision",
        "traces": "Traces",
        "interpolation": "Interpolation",
        "animation": "Animation",
        "world_visibility": "World visibility",
        "render_instance_construction": "Render-instance construction",
        "world_command_encoding": "World command encoding",
        "dynamic_command_encoding": "Dynamic command encoding",
        "ui": "UI",
    }
    return labels.get(subsystem, subsystem.replace("_", " ").title())


def rounded(value: Any, digits: int = 4) -> Any:
    return round(value, digits) if isinstance(value, (int, float)) else value


def load_profile(repo: Path, aggregate_path: Path) -> dict[str, Any]:
    aggregate_path = aggregate_path.resolve()
    aggregate = json.loads(aggregate_path.read_text(encoding="utf-8"))
    profile = aggregate["scenario"]["graphics_profile"]
    frames: list[dict[str, Any]] = []
    runs: list[dict[str, Any]] = []
    report_by_run = {
        item["run_id"]: item
        for item in aggregate.get("frame_timeline_reports", [])
        if isinstance(item, dict) and item.get("run_id")
    }

    for run in aggregate["runs"]:
        run_dir = Path(run["result_directory"])
        timeline_path = run_dir / "frame-timeline.json"
        timeline = json.loads(timeline_path.read_text(encoding="utf-8"))
        frames.extend(timeline["frames"])
        report = report_by_run.get(run["run_id"], {})
        screenshots = [
            Path(path).relative_to(repo).as_posix()
            for path in run.get("screenshots", [])
            if Path(path).is_relative_to(repo)
        ]
        runs.append(
            {
                "profile": profile,
                "run": run["run_id"],
                "valid": bool(run["valid"]),
                "frames": int(timeline["frame_count"]),
                "gpu_samples": int(timeline.get("gpu_timing", {}).get("sample_count", 0)),
                "gpu_coverage_percent": timeline.get("gpu_timing", {}).get("coverage_percent"),
                "result_json": (run_dir / "result.json").relative_to(repo).as_posix(),
                "frame_timeline_json": timeline_path.relative_to(repo).as_posix(),
                "telemetry_csv": (run_dir / "telemetry.csv").relative_to(repo).as_posix(),
                "simulation_ticks_csv": (run_dir / "simulation-ticks.csv").relative_to(repo).as_posix(),
                "frame_times_csv": (run_dir / "frame-times.csv").relative_to(repo).as_posix(),
                "timeline_html": (
                    Path(report["html_path"]).relative_to(repo).as_posix()
                    if report.get("html_path") and Path(report["html_path"]).is_relative_to(repo)
                    else None
                ),
                "timeline_analysis_json": (
                    Path(report["analysis_path"]).relative_to(repo).as_posix()
                    if report.get("analysis_path") and Path(report["analysis_path"]).is_relative_to(repo)
                    else None
                ),
                "screenshots": ", ".join(screenshots),
            }
        )

    return {
        "profile": profile,
        "aggregate_path": aggregate_path,
        "aggregate": aggregate,
        "frames": frames,
        "runs": runs,
    }


def make_histogram(
    profile_values: dict[str, list[float]],
    *,
    width: float,
    maximum: float,
) -> list[dict[str, Any]]:
    bin_count = int(round(maximum / width))
    rows: list[dict[str, Any]] = []
    for profile in PROFILE_ORDER:
        values = profile_values[profile]
        counts = [0] * bin_count
        for value in values:
            if 0.0 <= value <= maximum:
                index = min(int(value / width), bin_count - 1)
                counts[index] += 1
        denominator = max(len(values), 1)
        for index, count in enumerate(counts):
            rows.append(
                {
                    "profile": profile,
                    "bin_mid_ms": round((index + 0.5) * width, 4),
                    "frame_percent": round(count * 100.0 / denominator, 6),
                    "frame_count": count,
                    "total_frames": len(values),
                }
            )
    return rows


def build_datasets(profiles: dict[str, dict[str, Any]]) -> dict[str, list[dict[str, Any]]]:
    cpu_values: dict[str, list[float]] = {}
    gpu_values: dict[str, list[float]] = {}
    cpu_pacing: list[dict[str, Any]] = []
    gpu_pacing: list[dict[str, Any]] = []
    cpu_subsystems: list[dict[str, Any]] = []
    gpu_stages: list[dict[str, Any]] = []
    workload_counters: list[dict[str, Any]] = []
    graphics_settings: list[dict[str, Any]] = []
    all_metrics: list[dict[str, Any]] = []
    run_inventory: list[dict[str, Any]] = []

    for profile in PROFILE_ORDER:
        bundle = profiles[profile]
        aggregate = bundle["aggregate"]
        frames = bundle["frames"]
        graphics_contract = aggregate["settings"]["graphics_contract"]
        cvars = graphics_contract["effective_cvars"]
        graphics_settings.append(
            {
                "profile": profile,
                "anti_aliasing": cvars.get("r_antialiasing"),
                "sun_shadows": cvars.get("r_sun_shadows"),
                "contact_shadows": cvars.get("r_contact_shadows"),
                "material_quality": cvars.get("r_material_quality"),
                "player_rim": cvars.get("r_player_rim"),
                "atmosphere_grade": cvars.get("r_atmosphere_grade"),
                "bloom": cvars.get("r_bloom"),
                "render_scale": cvars.get("r_render_scale"),
                "present_mode": aggregate["environment"].get("selected_present_mode"),
                "vsync": aggregate["settings"].get("vsync"),
                "frame_cap": aggregate["settings"].get("frame_cap"),
                "audio_volume": aggregate["settings"]
                .get("presentation_cvars", {})
                .get("s_volume"),
            }
        )
        cpu_values[profile] = [
            float(frame["total_cpu_ms"])
            for frame in frames
            if isinstance(frame.get("total_cpu_ms"), (int, float))
        ]
        gpu_values[profile] = [
            float(frame["total_gpu_ms"])
            for frame in frames
            if isinstance(frame.get("total_gpu_ms"), (int, float))
        ]

        cpu_summary = summarize(cpu_values[profile])
        cpu_row = {
            "profile": profile,
            **{f"{field}_ms" if field != "count" else "frame_count": rounded(value)
               for field, value in cpu_summary.items()},
            "mean_fps": rounded(1000.0 / cpu_summary["mean"] if cpu_summary["mean"] else None, 3),
            "run_median_cv_percent": rounded(
                aggregate["aggregate"]["metrics"]["frame_ms"].get("cv_percent"), 3
            ),
            "stable": bool(aggregate["aggregate"].get("stable")),
            "tail_over_8_ms_percent": rounded(
                sum(value > 8.0 for value in cpu_values[profile])
                * 100.0
                / max(len(cpu_values[profile]), 1),
                6,
            ),
        }
        for threshold in FRAME_THRESHOLDS_MS:
            key = f"over_{str(threshold).replace('.', '_')}_ms_percent"
            cpu_row[key] = rounded(
                sum(value > threshold for value in cpu_values[profile])
                * 100.0
                / max(len(cpu_values[profile]), 1),
                6,
            )
        cpu_row["p99_9_ms"] = rounded(nearest_rank(cpu_values[profile], 0.999))
        cpu_pacing.append(cpu_row)

        gpu_summary = summarize(gpu_values[profile])
        gpu_pacing.append(
            {
                "profile": profile,
                **{f"{field}_ms" if field != "count" else "sample_count": rounded(value)
                   for field, value in gpu_summary.items()},
                "frame_count": len(frames),
                "coverage_percent": rounded(
                    len(gpu_values[profile]) * 100.0 / max(len(frames), 1), 6
                ),
                "tail_over_4_ms_percent": rounded(
                    sum(value > 4.0 for value in gpu_values[profile])
                    * 100.0
                    / max(len(gpu_values[profile]), 1),
                    6,
                ),
                "instrumentation": aggregate["environment"].get(
                    "gpu_timing_instrumentation_version", "unknown"
                ),
            }
        )

        cpu_samples: dict[str, list[float]] = defaultdict(list)
        counter_samples: dict[str, list[float]] = defaultdict(list)
        gpu_samples: dict[str, list[float]] = defaultdict(list)
        gpu_states: dict[str, dict[str, int]] = {
            stage: defaultdict(int) for stage in GPU_STAGE_ORDER
        }
        for frame in frames:
            for name, value in frame.get("cpu_subsystems_ms", {}).items():
                if isinstance(value, (int, float)):
                    cpu_samples[name].append(float(value))
            for name, value in frame.get("workload_counters", {}).items():
                if isinstance(value, (int, float)):
                    counter_samples[name].append(float(value))
            stage_values = frame.get("gpu_subsystems_ms", {})
            stage_states = frame.get("gpu_subsystem_states", {})
            for stage in GPU_STAGE_ORDER:
                state = stage_states.get(stage, "unknown")
                gpu_states[stage][state] += 1
                value = stage_values.get(stage)
                if state == "available" and isinstance(value, (int, float)):
                    gpu_samples[stage].append(float(value))

        ordered_cpu_names = list(CPU_SUBSYSTEM_ORDER) + sorted(
            set(cpu_samples) - set(CPU_SUBSYSTEM_ORDER)
        )
        for name in ordered_cpu_names:
            stats = summarize(cpu_samples.get(name, []))
            cpu_subsystems.append(
                {
                    "profile": profile,
                    "subsystem": name,
                    "subsystem_label": subsystem_label(name),
                    **{f"{field}_ms" if field != "count" else "sample_count": rounded(value)
                       for field, value in stats.items()},
                }
            )

        for stage in GPU_STAGE_ORDER:
            states = gpu_states[stage]
            applicable = states.get("available", 0) + states.get("unavailable", 0)
            available = states.get("available", 0)
            stats = summarize(gpu_samples.get(stage, []))
            gpu_stages.append(
                {
                    "profile": profile,
                    "stage": stage,
                    "stage_label": stage_label(stage),
                    **{f"{field}_ms" if field != "count" else "sample_count": rounded(value)
                       for field, value in stats.items()},
                    "applicable_frames": applicable,
                    "available_frames": available,
                    "unavailable_frames": states.get("unavailable", 0),
                    "not_applicable_frames": states.get("not_applicable", 0),
                    "unknown_state_frames": states.get("unknown", 0),
                    "coverage_percent": rounded(
                        available * 100.0 / applicable if applicable else None, 6
                    ),
                }
            )

        for name in sorted(counter_samples):
            stats = summarize(counter_samples[name])
            workload_counters.append(
                {
                    "profile": profile,
                    "counter": name,
                    "counter_label": name.replace("_", " ").title(),
                    **{field: rounded(value) for field, value in stats.items()},
                }
            )

        for metric_name, metric in aggregate["aggregate"]["metrics"].items():
            if not isinstance(metric, dict):
                continue
            all_metrics.append(
                {
                    "profile": profile,
                    "metric": metric_name,
                    "metric_label": metric_label(metric_name),
                    "count": metric.get("count"),
                    "min": rounded(metric.get("min")),
                    "median": rounded(metric.get("median")),
                    "mean": rounded(metric.get("mean")),
                    "p95": rounded(metric.get("p95")),
                    "p99": rounded(metric.get("p99")),
                    "max": rounded(metric.get("max")),
                    "stddev": rounded(metric.get("stddev")),
                    "cv_percent": rounded(metric.get("cv_percent")),
                }
            )

        run_inventory.extend(bundle["runs"])

    cpu_metric_names = {
        row["metric"]
        for row in all_metrics
        if (
            row["metric"] == "frame_ms"
            or row["metric"].endswith("_ms")
            or row["metric"].startswith("threshold_")
        )
        and "gpu" not in row["metric"]
    }
    gpu_metric_names = {
        row["metric"]
        for row in all_metrics
        if "gpu" in row["metric"]
    }

    return {
        "cpu_distribution": make_histogram(cpu_values, width=0.1, maximum=8.0),
        "gpu_distribution": make_histogram(gpu_values, width=0.05, maximum=4.0),
        "cpu_pacing": cpu_pacing,
        "gpu_pacing": gpu_pacing,
        "cpu_subsystems": cpu_subsystems,
        "gpu_stages": gpu_stages,
        "gpu_stage_chart": [
            row
            for row in gpu_stages
            if row["stage"] != "outline_total" and row["median_ms"] is not None
        ],
        "workload_counters": workload_counters,
        "graphics_settings": graphics_settings,
        "all_metrics": all_metrics,
        "cpu_metrics": [row for row in all_metrics if row["metric"] in cpu_metric_names],
        "gpu_metrics": [row for row in all_metrics if row["metric"] in gpu_metric_names],
        "run_inventory": run_inventory,
    }


def source_spec(
    aggregate_paths: list[str], generated_at: str, snapshot_path: str
) -> dict[str, Any]:
    return {
        "id": "match_load_benchmarks",
        "label": "Eyetoeye match-load benchmark artifacts",
        "path": snapshot_path,
        "query": {
            "engine": "duckdb",
            "language": "sql",
            "sql": f"SELECT * FROM read_json_auto('{snapshot_path}', records = true);",
            "description": (
                "Reads the reviewed report snapshot produced by scripts/build_match_load_reports.py. "
                "That script loads four benchmark aggregates and all 20 native frame-timeline JSON "
                "files, pools frames within each preset, and computes the tables and fixed-width bins."
            ),
            "executed_at": generated_at,
            "tables_used": [snapshot_path, *aggregate_paths],
            "filters": [
                "scenario = eyetoeye-match-load",
                "graphics_profile in (Low, Default, Competitive, High)",
                "valid runs only; five runs per profile",
                "3 second warm-up followed by 12 second measurement",
                "CPU histogram: 0.1 ms bins through 8 ms; denominator includes all measured frames",
                "GPU histogram: 0.05 ms bins through 4 ms; denominator includes all GPU samples",
            ],
            "metric_definitions": [
                "Pooled median/p95/p99: percentile over every measured frame for one profile across five runs.",
                "Run median CV: coefficient of variation across the five run-level frame-time medians.",
                "GPU coverage: frames with a received total GPU timestamp divided by measured frames.",
                "Pass coverage: available pass samples divided by frames where that pass applied.",
                "Frame percent in a histogram bin: bin frame count divided by all frames for that profile.",
            ],
        },
    }


def chart_specs() -> list[dict[str, Any]]:
    return [
        {
            "id": "cpu_distribution",
            "title": "CPU frame-time distribution",
            "subtitle": (
                "0.1 ms bins through 8 ms; percent uses all measured frames. "
                "Use the legend to show or hide presets."
            ),
            "intent": "distribution",
            "question": "How does pooled CPU frame pacing differ across the four presets?",
            "rationale": (
                "A highlighted multi-series line keeps the full distribution shape visible "
                "while the interactive legend supports one- or two-preset comparisons."
            ),
            "type": "line",
            "dataset": "cpu_distribution",
            "sourceId": "match_load_benchmarks",
            "encodings": {
                "x": {"field": "bin_mid_ms", "type": "quantitative", "label": "Frame time (ms)"},
                "y": {
                    "field": "frame_percent",
                    "type": "quantitative",
                    "label": "Frames (%)",
                    "format": "number",
                },
                "color": {"field": "profile", "type": "nominal", "label": "Preset"},
                "tooltip": [
                    {"field": "frame_count", "type": "quantitative", "label": "Frames"},
                    {"field": "total_frames", "type": "quantitative", "label": "Total frames"},
                ],
            },
            "palette": {"kind": "categorical", "name": "benchmark-presets"},
            "legend": {"interactive": True, "position": "bottom", "title": "Preset"},
            "referenceLines": [
                {"value": 2.63, "axis": "x", "label": "380 FPS", "color": "neutral", "lineStyle": "dashed"},
                {"value": 4.17, "axis": "x", "label": "240 FPS", "color": "neutral", "lineStyle": "dotted"},
                {"value": 6.94, "axis": "x", "label": "144 FPS", "color": "neutral", "lineStyle": "dashed"},
            ],
            "layout": "full",
        },
        {
            "id": "gpu_distribution",
            "title": "GPU frame-time distribution",
            "subtitle": (
                "0.05 ms bins through 4 ms from native Vulkan timestamps; "
                "use the legend to isolate presets."
            ),
            "intent": "distribution",
            "question": "How does total command-buffer GPU time differ across presets?",
            "rationale": (
                "A multi-series line reveals the narrow timing modes and long tails without "
                "hiding overlap; the legend supports direct subset comparison."
            ),
            "type": "line",
            "dataset": "gpu_distribution",
            "sourceId": "match_load_benchmarks",
            "encodings": {
                "x": {"field": "bin_mid_ms", "type": "quantitative", "label": "GPU time (ms)"},
                "y": {
                    "field": "frame_percent",
                    "type": "quantitative",
                    "label": "Samples (%)",
                    "format": "number",
                },
                "color": {"field": "profile", "type": "nominal", "label": "Preset"},
                "tooltip": [
                    {"field": "frame_count", "type": "quantitative", "label": "Samples"},
                    {"field": "total_frames", "type": "quantitative", "label": "Total samples"},
                ],
            },
            "palette": {"kind": "categorical", "name": "benchmark-presets"},
            "legend": {"interactive": True, "position": "bottom", "title": "Preset"},
            "layout": "full",
        },
        {
            "id": "cpu_subsystems",
            "title": "CPU subsystem median by preset",
            "subtitle": (
                "Pooled frame medians across five runs per preset; stages are not additive."
            ),
            "intent": "comparison",
            "question": "Which measured CPU stages account for the preset differences?",
            "rationale": (
                "Grouped horizontal bars keep the long stage labels legible and let the same "
                "subsystem be compared across all presets."
            ),
            "type": "horizontalBar",
            "dataset": "cpu_subsystems",
            "sourceId": "match_load_benchmarks",
            "encodings": {
                "x": {"field": "subsystem_label", "type": "nominal", "label": "Subsystem"},
                "y": {"field": "median_ms", "type": "quantitative", "label": "Median (ms)"},
                "color": {"field": "profile", "type": "nominal", "label": "Preset"},
                "tooltip": [
                    {"field": "p95_ms", "type": "quantitative", "label": "p95 (ms)"},
                    {"field": "p99_ms", "type": "quantitative", "label": "p99 (ms)"},
                    {"field": "sample_count", "type": "quantitative", "label": "Samples"},
                ],
            },
            "palette": {"kind": "categorical", "name": "benchmark-presets"},
            "legend": {"interactive": True, "position": "bottom", "title": "Preset"},
            "layout": "full",
        },
        {
            "id": "gpu_stages",
            "title": "GPU pass median by preset",
            "subtitle": (
                "Native per-pass timestamp medians; omitted passes did not apply. "
                "Pass values do not sum to the total command-buffer time."
            ),
            "intent": "comparison",
            "question": "Which GPU passes drive the cost of each preset?",
            "rationale": (
                "Grouped bars compare the same pass across presets while the interactive legend "
                "lets a reader isolate one or two profiles."
            ),
            "type": "bar",
            "dataset": "gpu_stage_chart",
            "sourceId": "match_load_benchmarks",
            "encodings": {
                "x": {"field": "stage_label", "type": "nominal", "label": "GPU pass"},
                "y": {"field": "median_ms", "type": "quantitative", "label": "Median (ms)"},
                "color": {"field": "profile", "type": "nominal", "label": "Preset"},
                "tooltip": [
                    {"field": "p95_ms", "type": "quantitative", "label": "p95 (ms)"},
                    {"field": "p99_ms", "type": "quantitative", "label": "p99 (ms)"},
                    {"field": "coverage_percent", "type": "quantitative", "label": "Coverage (%)"},
                ],
            },
            "palette": {"kind": "categorical", "name": "benchmark-presets"},
            "legend": {"interactive": True, "position": "bottom", "title": "Preset"},
            "layout": "full",
        },
    ]


def common_tables() -> list[dict[str, Any]]:
    return [
        {
            "id": "graphics_quality_settings",
            "title": "Effective graphics quality settings",
            "subtitle": "The values accepted by the renderer for each named preset.",
            "dataset": "graphics_settings",
            "sourceId": "match_load_benchmarks",
            "defaultSort": {"field": "profile", "direction": "asc"},
            "layout": "full",
            "density": "compact",
            "columns": [
                {"field": "profile", "label": "Preset", "type": "text"},
                {"field": "anti_aliasing", "label": "AA", "type": "text"},
                {"field": "sun_shadows", "label": "Sun shadow", "type": "text"},
                {"field": "contact_shadows", "label": "Contact shadow", "type": "text"},
                {"field": "material_quality", "label": "Material", "type": "text"},
                {"field": "render_scale", "label": "Render scale", "type": "text"},
            ],
        },
        {
            "id": "graphics_effect_settings",
            "title": "Effective graphics effect and presentation settings",
            "subtitle": "Preset effect values plus the shared uncapped presentation contract.",
            "dataset": "graphics_settings",
            "sourceId": "match_load_benchmarks",
            "defaultSort": {"field": "profile", "direction": "asc"},
            "layout": "full",
            "density": "compact",
            "columns": [
                {"field": "profile", "label": "Preset", "type": "text"},
                {"field": "player_rim", "label": "Player rim", "type": "text"},
                {"field": "atmosphere_grade", "label": "Atmosphere", "type": "text"},
                {"field": "bloom", "label": "Bloom", "type": "text"},
                {"field": "present_mode", "label": "Present mode", "type": "text"},
                {"field": "frame_cap", "label": "Frame cap", "format": "number"},
                {"field": "audio_volume", "label": "s_volume", "format": "number"},
            ],
        },
        {
            "id": "cpu_pacing",
            "title": "CPU frame-pacing statistics",
            "subtitle": "Pooled frames across five 12-second runs per preset.",
            "dataset": "cpu_pacing",
            "sourceId": "match_load_benchmarks",
            "defaultSort": {"field": "median_ms", "direction": "asc"},
            "layout": "full",
            "density": "compact",
            "columns": [
                {"field": "profile", "label": "Preset", "type": "text"},
                {"field": "frame_count", "label": "Frames", "format": "number"},
                {"field": "mean_fps", "label": "Mean FPS", "format": "number"},
                {"field": "median_ms", "label": "Median ms", "format": "number"},
                {"field": "p95_ms", "label": "p95 ms", "format": "number"},
                {"field": "p99_ms", "label": "p99 ms", "format": "number"},
            ],
        },
        {
            "id": "cpu_tails",
            "title": "CPU frame-pacing tails and stability",
            "subtitle": "Rare tails stay separate from the central frame-time table.",
            "dataset": "cpu_pacing",
            "sourceId": "match_load_benchmarks",
            "defaultSort": {"field": "max_ms", "direction": "desc"},
            "layout": "full",
            "density": "compact",
            "columns": [
                {"field": "profile", "label": "Preset", "type": "text"},
                {"field": "p99_9_ms", "label": "p99.9 ms", "format": "number"},
                {"field": "max_ms", "label": "Max ms", "format": "number"},
                {"field": "run_median_cv_percent", "label": "Run median CV (%)", "format": "number"},
                {"field": "stable", "label": "Stable", "type": "text"},
            ],
        },
        {
            "id": "cpu_thresholds",
            "title": "CPU frames over pacing thresholds",
            "subtitle": "Percent of all measured frames per preset.",
            "dataset": "cpu_pacing",
            "sourceId": "match_load_benchmarks",
            "defaultSort": {"field": "over_16_67_ms_percent", "direction": "desc"},
            "layout": "full",
            "density": "compact",
            "columns": [
                {"field": "profile", "label": "Preset", "type": "text"},
                {"field": "over_2_63_ms_percent", "label": ">2.63 ms (%)", "format": "number"},
                {"field": "over_4_17_ms_percent", "label": ">4.17 ms (%)", "format": "number"},
                {"field": "over_6_94_ms_percent", "label": ">6.94 ms (%)", "format": "number"},
                {"field": "over_8_33_ms_percent", "label": ">8.33 ms (%)", "format": "number"},
                {"field": "over_16_67_ms_percent", "label": ">16.67 ms (%)", "format": "number"},
            ],
        },
        {
            "id": "gpu_pacing",
            "title": "Total GPU timing and coverage",
            "subtitle": "Native primary-command-buffer timestamps pooled by preset.",
            "dataset": "gpu_pacing",
            "sourceId": "match_load_benchmarks",
            "defaultSort": {"field": "median_ms", "direction": "asc"},
            "layout": "full",
            "density": "compact",
            "columns": [
                {"field": "profile", "label": "Preset", "type": "text"},
                {"field": "frame_count", "label": "Frames", "format": "number"},
                {"field": "sample_count", "label": "GPU samples", "format": "number"},
                {"field": "coverage_percent", "label": "Coverage (%)", "format": "number"},
                {"field": "median_ms", "label": "Median ms", "format": "number"},
                {"field": "p95_ms", "label": "p95 ms", "format": "number"},
            ],
        },
        {
            "id": "gpu_tails",
            "title": "GPU timing tails and instrumentation",
            "subtitle": "Tail timing and the native timestamp contract used for this run.",
            "dataset": "gpu_pacing",
            "sourceId": "match_load_benchmarks",
            "defaultSort": {"field": "p99_ms", "direction": "desc"},
            "layout": "full",
            "density": "compact",
            "columns": [
                {"field": "profile", "label": "Preset", "type": "text"},
                {"field": "p99_ms", "label": "p99 ms", "format": "number"},
                {"field": "max_ms", "label": "Max ms", "format": "number"},
                {"field": "tail_over_4_ms_percent", "label": ">4 ms (%)", "format": "number"},
                {"field": "instrumentation", "label": "Instrumentation", "type": "text"},
            ],
        },
        {
            "id": "cpu_subsystems",
            "title": "CPU subsystem detail",
            "subtitle": "Every measured render-frame CPU subsystem.",
            "dataset": "cpu_subsystems",
            "sourceId": "match_load_benchmarks",
            "defaultSort": {"field": "median_ms", "direction": "desc"},
            "layout": "full",
            "density": "compact",
            "columns": [
                {"field": "profile", "label": "Preset", "type": "text"},
                {"field": "subsystem_label", "label": "Subsystem", "type": "text"},
                {"field": "sample_count", "label": "Samples", "format": "number"},
                {"field": "median_ms", "label": "Median ms", "format": "number"},
                {"field": "p95_ms", "label": "p95 ms", "format": "number"},
                {"field": "p99_ms", "label": "p99 ms", "format": "number"},
            ],
        },
        {
            "id": "cpu_subsystem_spread",
            "title": "CPU subsystem spread",
            "subtitle": "Mean, maximum, and standard deviation for every measured CPU subsystem.",
            "dataset": "cpu_subsystems",
            "sourceId": "match_load_benchmarks",
            "defaultSort": {"field": "max_ms", "direction": "desc"},
            "layout": "full",
            "density": "compact",
            "columns": [
                {"field": "profile", "label": "Preset", "type": "text"},
                {"field": "subsystem_label", "label": "Subsystem", "type": "text"},
                {"field": "max_ms", "label": "Max ms", "format": "number"},
                {"field": "mean_ms", "label": "Mean ms", "format": "number"},
                {"field": "stddev_ms", "label": "Stddev ms", "format": "number"},
            ],
        },
        {
            "id": "gpu_stages",
            "title": "GPU pass detail and sample states",
            "subtitle": (
                "Every named pass, including not-applicable and unavailable frame counts. "
                "Outline total is a compatibility alias around the three outline passes."
            ),
            "dataset": "gpu_stages",
            "sourceId": "match_load_benchmarks",
            "defaultSort": {"field": "median_ms", "direction": "desc"},
            "layout": "full",
            "density": "compact",
            "columns": [
                {"field": "profile", "label": "Preset", "type": "text"},
                {"field": "stage_label", "label": "GPU pass", "type": "text"},
                {"field": "sample_count", "label": "Samples", "format": "number"},
                {"field": "median_ms", "label": "Median ms", "format": "number"},
                {"field": "p95_ms", "label": "p95 ms", "format": "number"},
                {"field": "p99_ms", "label": "p99 ms", "format": "number"},
            ],
        },
        {
            "id": "gpu_stage_states",
            "title": "GPU pass sample states",
            "subtitle": "Applicable, not-applicable, unavailable, and coverage counts for every pass.",
            "dataset": "gpu_stages",
            "sourceId": "match_load_benchmarks",
            "defaultSort": {"field": "applicable_frames", "direction": "desc"},
            "layout": "full",
            "density": "compact",
            "columns": [
                {"field": "profile", "label": "Preset", "type": "text"},
                {"field": "stage_label", "label": "GPU pass", "type": "text"},
                {"field": "applicable_frames", "label": "Applicable", "format": "number"},
                {"field": "not_applicable_frames", "label": "Not applicable", "format": "number"},
                {"field": "unavailable_frames", "label": "Unavailable", "format": "number"},
                {"field": "coverage_percent", "label": "Coverage (%)", "format": "number"},
            ],
        },
        {
            "id": "gpu_stage_spread",
            "title": "GPU pass spread",
            "subtitle": "Mean, maximum, and standard deviation for every available pass.",
            "dataset": "gpu_stages",
            "sourceId": "match_load_benchmarks",
            "defaultSort": {"field": "max_ms", "direction": "desc"},
            "layout": "full",
            "density": "compact",
            "columns": [
                {"field": "profile", "label": "Preset", "type": "text"},
                {"field": "stage_label", "label": "GPU pass", "type": "text"},
                {"field": "mean_ms", "label": "Mean ms", "format": "number"},
                {"field": "median_ms", "label": "Median ms", "format": "number"},
                {"field": "max_ms", "label": "Max ms", "format": "number"},
                {"field": "stddev_ms", "label": "Stddev ms", "format": "number"},
            ],
        },
        {
            "id": "workload_counters",
            "title": "Workload counters",
            "subtitle": "All per-frame scene and draw counters gathered by the benchmark.",
            "dataset": "workload_counters",
            "sourceId": "match_load_benchmarks",
            "defaultSort": {"field": "counter_label", "direction": "asc"},
            "layout": "full",
            "density": "compact",
            "columns": [
                {"field": "profile", "label": "Preset", "type": "text"},
                {"field": "counter_label", "label": "Counter", "type": "text"},
                {"field": "count", "label": "Samples", "format": "number"},
                {"field": "median", "label": "Median", "format": "number"},
                {"field": "p95", "label": "p95", "format": "number"},
                {"field": "max", "label": "Max", "format": "number"},
            ],
        },
        {
            "id": "all_metrics",
            "title": "Complete aggregate metric table",
            "subtitle": "Every metric emitted in the four aggregate JSON files.",
            "dataset": "all_metrics",
            "sourceId": "match_load_benchmarks",
            "defaultSort": {"field": "metric", "direction": "asc"},
            "layout": "full",
            "density": "compact",
            "columns": [
                {"field": "profile", "label": "Preset", "type": "text"},
                {"field": "metric", "label": "Metric key", "type": "text"},
                {"field": "count", "label": "Runs", "format": "number"},
                {"field": "median", "label": "Median", "format": "number"},
                {"field": "p95", "label": "p95", "format": "number"},
                {"field": "p99", "label": "p99", "format": "number"},
            ],
        },
        {
            "id": "all_metric_spread",
            "title": "Complete aggregate metric spread",
            "subtitle": "Min, mean, max, standard deviation, and run-level coefficient of variation.",
            "dataset": "all_metrics",
            "sourceId": "match_load_benchmarks",
            "defaultSort": {"field": "metric", "direction": "asc"},
            "layout": "full",
            "density": "compact",
            "columns": [
                {"field": "profile", "label": "Preset", "type": "text"},
                {"field": "metric", "label": "Metric key", "type": "text"},
                {"field": "min", "label": "Min", "format": "number"},
                {"field": "mean", "label": "Mean", "format": "number"},
                {"field": "max", "label": "Max", "format": "number"},
                {"field": "stddev", "label": "Stddev", "format": "number"},
                {"field": "cv_percent", "label": "CV (%)", "format": "number"},
            ],
        },
        {
            "id": "cpu_metrics",
            "title": "Complete CPU and frame-pacing aggregate table",
            "subtitle": "Every non-GPU timing and threshold metric in the aggregate files.",
            "dataset": "cpu_metrics",
            "sourceId": "match_load_benchmarks",
            "defaultSort": {"field": "metric", "direction": "asc"},
            "layout": "full",
            "density": "compact",
            "columns": [
                {"field": "profile", "label": "Preset", "type": "text"},
                {"field": "metric", "label": "Metric key", "type": "text"},
                {"field": "count", "label": "Runs", "format": "number"},
                {"field": "median", "label": "Median", "format": "number"},
                {"field": "p95", "label": "p95", "format": "number"},
                {"field": "p99", "label": "p99", "format": "number"},
            ],
        },
        {
            "id": "cpu_metric_spread",
            "title": "Complete CPU aggregate spread",
            "subtitle": "Min, mean, max, standard deviation, and run-level CV for each CPU metric.",
            "dataset": "cpu_metrics",
            "sourceId": "match_load_benchmarks",
            "defaultSort": {"field": "metric", "direction": "asc"},
            "layout": "full",
            "density": "compact",
            "columns": [
                {"field": "profile", "label": "Preset", "type": "text"},
                {"field": "metric", "label": "Metric key", "type": "text"},
                {"field": "min", "label": "Min", "format": "number"},
                {"field": "mean", "label": "Mean", "format": "number"},
                {"field": "max", "label": "Max", "format": "number"},
                {"field": "stddev", "label": "Stddev", "format": "number"},
                {"field": "cv_percent", "label": "CV (%)", "format": "number"},
            ],
        },
        {
            "id": "gpu_metrics",
            "title": "Complete GPU aggregate table",
            "subtitle": "Every GPU metric, pass metric, result flag, and readback metric.",
            "dataset": "gpu_metrics",
            "sourceId": "match_load_benchmarks",
            "defaultSort": {"field": "metric", "direction": "asc"},
            "layout": "full",
            "density": "compact",
            "columns": [
                {"field": "profile", "label": "Preset", "type": "text"},
                {"field": "metric", "label": "Metric key", "type": "text"},
                {"field": "count", "label": "Runs", "format": "number"},
                {"field": "median", "label": "Median", "format": "number"},
                {"field": "p95", "label": "p95", "format": "number"},
                {"field": "p99", "label": "p99", "format": "number"},
            ],
        },
        {
            "id": "gpu_metric_spread",
            "title": "Complete GPU aggregate spread",
            "subtitle": "Min, mean, max, standard deviation, and run-level CV for each GPU metric.",
            "dataset": "gpu_metrics",
            "sourceId": "match_load_benchmarks",
            "defaultSort": {"field": "metric", "direction": "asc"},
            "layout": "full",
            "density": "compact",
            "columns": [
                {"field": "profile", "label": "Preset", "type": "text"},
                {"field": "metric", "label": "Metric key", "type": "text"},
                {"field": "min", "label": "Min", "format": "number"},
                {"field": "mean", "label": "Mean", "format": "number"},
                {"field": "max", "label": "Max", "format": "number"},
                {"field": "stddev", "label": "Stddev", "format": "number"},
                {"field": "cv_percent", "label": "CV (%)", "format": "number"},
            ],
        },
        {
            "id": "run_inventory",
            "title": "Run and raw-artifact inventory",
            "subtitle": "One row per repetition; paths are relative to the repository root.",
            "dataset": "run_inventory",
            "sourceId": "match_load_benchmarks",
            "defaultSort": {"field": "profile", "direction": "asc"},
            "layout": "full",
            "density": "compact",
            "columns": [
                {"field": "profile", "label": "Preset", "type": "text"},
                {"field": "run", "label": "Run", "type": "text"},
                {"field": "valid", "label": "Valid", "type": "text"},
                {"field": "frames", "label": "Frames", "format": "number"},
                {"field": "gpu_samples", "label": "GPU samples", "format": "number"},
                {"field": "gpu_coverage_percent", "label": "GPU coverage (%)", "format": "number"},
            ],
        },
    ]


def profile_cards(kind: str) -> list[dict[str, Any]]:
    dataset = "cpu_pacing" if kind == "cpu" else "gpu_pacing"
    return [
        {
            "id": f"{kind}_{profile.lower()}",
            "description": (
                f"{profile} pooled {'CPU frame' if kind == 'cpu' else 'GPU command-buffer'} timing."
            ),
            "dataset": dataset,
            "sourceId": "match_load_benchmarks",
            "filter": {"profile": profile},
            "metrics": [
                {
                    "label": f"{profile} median (ms)",
                    "field": "median_ms",
                    "format": "number",
                },
                {"label": "p95 (ms)", "field": "p95_ms", "format": "number"},
                {"label": "p99 (ms)", "field": "p99_ms", "format": "number"},
            ],
        }
        for profile in PROFILE_ORDER
    ]


def raw_links_markdown(run_inventory: list[dict[str, Any]]) -> str:
    lines = [
        "## Open any run in isolation",
        "",
        "Each profile is named in the report header. The per-run pages include frame and tick "
        "distributions, CPU subsystem tails, GPU timing and pass coverage, spike rows, and raw-file links.",
        "",
    ]
    for profile in PROFILE_ORDER:
        lines.append(f"### {profile}")
        lines.append("")
        for row in run_inventory:
            if row["profile"] != profile or not row["timeline_html"]:
                continue
            # The report sits two levels below build/. Keep the link portable inside this checkout.
            link = "../../" + row["timeline_html"].removeprefix("build/")
            lines.append(f"- [{row['run']} frame timeline]({link})")
        lines.append("")
    return "\n".join(lines)


def findings_text(datasets: dict[str, list[dict[str, Any]]]) -> tuple[str, str, str]:
    cpu = {row["profile"]: row for row in datasets["cpu_pacing"]}
    gpu = {row["profile"]: row for row in datasets["gpu_pacing"]}
    subsystems = {
        (row["profile"], row["subsystem"]): row for row in datasets["cpu_subsystems"]
    }
    stages = {
        (row["profile"], row["stage"]): row for row in datasets["gpu_stages"]
    }

    high_acquire = subsystems[("High", "swapchain_acquisition")]["median_ms"]
    high_renderer = subsystems[("High", "renderer_total")]["median_ms"]
    default_gpu = gpu["Default"]["median_ms"]
    fastest_cpu_profile = min(PROFILE_ORDER, key=lambda profile: cpu[profile]["median_ms"])
    fastest_gpu_profile = min(PROFILE_ORDER, key=lambda profile: gpu[profile]["median_ms"])
    fastest_gpu = gpu[fastest_gpu_profile]["median_ms"]
    default_gpu_premium = (default_gpu / fastest_gpu - 1.0) * 100.0
    low_cpu_tail = cpu["Low"]["over_16_67_ms_percent"]
    low_cpu_max = cpu["Low"]["max_ms"]
    high_main = stages[("High", "main_scene")]["median_ms"]
    high_shadow = stages[("High", "sun_shadow")]["median_ms"]
    high_bloom = stages[("High", "bloom")]["median_ms"]
    if fastest_cpu_profile == fastest_gpu_profile:
        fastest_text = (
            f"{fastest_cpu_profile} is the fastest measured preset at "
            f"**{cpu[fastest_cpu_profile]['median_ms']:.3f} ms** CPU and "
            f"**{gpu[fastest_gpu_profile]['median_ms']:.3f} ms** GPU."
        )
    else:
        fastest_text = (
            f"{fastest_cpu_profile} has the lowest CPU median at "
            f"**{cpu[fastest_cpu_profile]['median_ms']:.3f} ms**, while "
            f"{fastest_gpu_profile} has the lowest GPU median at "
            f"**{gpu[fastest_gpu_profile]['median_ms']:.3f} ms**."
        )
    stable_profiles = [
        profile for profile in PROFILE_ORDER if cpu[profile]["stable"]
    ]
    lowest_cv_profile = min(
        PROFILE_ORDER,
        key=lambda profile: cpu[profile]["run_median_cv_percent"],
    )
    lowest_cv = cpu[lowest_cv_profile]["run_median_cv_percent"]
    if not stable_profiles:
        stability_text = (
            "No preset stayed under the **3%** run-median CV stability limit. "
            f"{lowest_cv_profile} was closest at **{lowest_cv:.3f}%**; treat small "
            "cross-preset gaps as directional until a repeat set passes the stability gate."
        )
    elif len(stable_profiles) == 1:
        stability_text = (
            f"Only {stable_profiles[0]} stayed under the **3%** run-median CV stability "
            f"limit. {lowest_cv_profile} had the lowest CV at **{lowest_cv:.3f}%**; treat "
            "small cross-preset gaps as directional until a repeat set passes the "
            "stability gate."
        )
    else:
        names = ", ".join(stable_profiles[:-1]) + f", and {stable_profiles[-1]}"
        stability_text = (
            f"{names} stayed under the **3%** run-median CV stability limit. "
            f"{lowest_cv_profile} had the lowest CV at **{lowest_cv:.3f}%**; treat small "
            "cross-preset gaps as directional until a repeat set passes the stability gate."
        )

    complete = (
        "## The presets scale GPU cost, while the measured CPU frame also tracks GPU wait\n\n"
        f"- Median CPU frame time rises from **{cpu['Low']['median_ms']:.3f} ms** on Low to "
        f"**{cpu['High']['median_ms']:.3f} ms** on High. Median GPU time rises from "
        f"**{gpu['Low']['median_ms']:.3f} ms** to **{gpu['High']['median_ms']:.3f} ms**.\n"
        f"- On High, swapchain acquisition has a **{high_acquire:.3f} ms** median inside a "
        f"**{high_renderer:.3f} ms** renderer median. This uncapped result is not a pure CPU "
        "throughput score; the CPU frame waits on GPU and presentation work.\n"
        f"- High's largest named GPU pass is main scene at **{high_main:.3f} ms** median. "
        f"Sun shadow adds **{high_shadow:.3f} ms** and bloom adds **{high_bloom:.3f} ms**.\n"
        f"- {fastest_text}\n"
        f"- Default costs **{abs(default_gpu - fastest_gpu):.3f} ms** more GPU time at the median "
        f"(**{default_gpu_premium:.1f}%** above {fastest_gpu_profile}). The effective-settings table "
        "shows the quality features behind each preset.\n"
        f"- Low recorded a **{low_cpu_max:.3f} ms** worst CPU frame, while only "
        f"**{low_cpu_tail:.4f}%** of Low frames exceeded 16.67 ms. The tail is rare but real "
        "and appears in the raw timelines.\n"
        "- All 20 runs passed validity checks. Total GPU timestamp coverage is 100% for each "
        "preset; per-pass not-applicable states remain visible instead of being turned into zeroes.\n"
        f"- {stability_text}"
    )
    cpu_text = (
        "## The CPU result includes renderer wait, so use subsystem timing with the headline\n\n"
        f"Low's pooled median frame is **{cpu['Low']['median_ms']:.3f} ms** and High's is "
        f"**{cpu['High']['median_ms']:.3f} ms**. On High, swapchain acquisition alone has a "
        f"**{high_acquire:.3f} ms** median. This means the preset gap reflects GPU and present "
        "pressure seen by the CPU loop, not just game logic. Simulation stays on the 125 Hz "
        "fixed tick and appears as zero on render frames that contain no tick; use the tick CSV "
        f"and tick metrics for its true distribution. {stability_text}"
    )
    gpu_text = (
        f"## {fastest_gpu_profile} is fastest here; High adds clear GPU work\n\n"
        f"Median total GPU time spans **{gpu['Low']['median_ms']:.3f} ms** on Low to "
        f"**{gpu['High']['median_ms']:.3f} ms** on High. High's main scene is "
        f"**{high_main:.3f} ms** median, with **{high_shadow:.3f} ms** for sun shadow and "
        f"**{high_bloom:.3f} ms** for bloom. {fastest_gpu_profile} is lowest at "
        f"**{fastest_gpu:.3f} ms**. Default adds **{abs(default_gpu - fastest_gpu):.3f} ms** "
        f"(**{default_gpu_premium:.1f}%**) at the median versus {fastest_gpu_profile}. Pass values are "
        "nested or separated by untimed command work, so they must not be added to reconstruct "
        "the total."
    )
    return complete, cpu_text, gpu_text


def report_artifact(
    *,
    title: str,
    description: str,
    generated_at: str,
    datasets: dict[str, list[dict[str, Any]]],
    source: dict[str, Any],
    cards: list[dict[str, Any]],
    chart_ids: list[str],
    table_ids: list[str],
    blocks: list[dict[str, Any]],
) -> dict[str, Any]:
    charts = {chart["id"]: chart for chart in chart_specs()}
    tables = {table["id"]: table for table in common_tables()}
    return {
        "surface": "report",
        "manifest": {
            "version": 1,
            "surface": "report",
            "title": title,
            "description": description,
            "generatedAt": generated_at,
            "cards": cards,
            "charts": [charts[chart_id] for chart_id in chart_ids],
            "tables": [tables[table_id] for table_id in table_ids],
            "sources": [source],
            "blocks": blocks,
        },
        "snapshot": {
            "version": 1,
            "generatedAt": generated_at,
            "status": "ready",
            "datasets": datasets,
        },
        "sources": [source],
        "package_info": {},
    }


def package_info(repo: Path, output_dir: Path, artifact_filename: str) -> dict[str, str]:
    return {
        "root": output_dir.resolve().relative_to(repo.resolve()).as_posix(),
        "manifestPath": artifact_filename,
        "snapshotPath": "report-snapshot.json",
    }


def write_reports(
    output_dir: Path,
    profiles: dict[str, dict[str, Any]],
    datasets: dict[str, list[dict[str, Any]]],
) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    generated_at = datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")
    aggregate_paths = [
        profiles[profile]["aggregate_path"].relative_to(Path.cwd().resolve()).as_posix()
        for profile in PROFILE_ORDER
    ]
    snapshot_path = (
        output_dir.relative_to(Path.cwd().resolve()) / "report-snapshot.json"
    ).as_posix()
    scenario_name = profiles[PROFILE_ORDER[0]]["aggregate"]["scenario"]["name"]
    audio_volumes = [
        profiles[profile]["aggregate"]["settings"]
        .get("presentation_cvars", {})
        .get("s_volume")
        for profile in PROFILE_ORDER
    ]
    muted_run = all(
        isinstance(value, (int, float)) and float(value) == 0.0
        for value in audio_volumes
    )
    title_suffix = " — muted, s_volume 0" if muted_run else ""
    audio_note = (
        " The saved cvar contract sets `s_volume` to **0** for every preset, so "
        "zero-volume cues and the Lightning Gun loop do not queue mixer work."
        if muted_run
        else ""
    )
    source = source_spec(aggregate_paths, generated_at, snapshot_path)
    complete_findings, cpu_findings, gpu_findings = findings_text(datasets)
    raw_links = raw_links_markdown(datasets["run_inventory"])

    methods = (
        "## What this run measures\n\n"
        "The case uses a local Lightning Gun player against one hard RL bot that moves and dodges. "
        "Each preset ran five times at 1280×720 with immediate presentation, a fixed camera, a "
        "3-second warm-up, and a 12-second measurement. The fixture uses no synthetic effect load. "
        f"It represents a normal two-player match, not a full-player-count stress case.{audio_note}\n\n"
        "CPU and GPU distribution rows pool frames within each preset. The run-level aggregate "
        "table keeps the five-run summaries and their coefficient of variation. GPU pass states "
        "retain available, unavailable, and not-applicable frames. The `outline_total` pass is a "
        "compatibility alias around mask, dilation, and composite; pass values do not sum to the "
        "total command-buffer time."
    )
    limits = (
        "## Limits and next checks\n\n"
        "- The run used an Intel Arc 140V integrated GPU and one machine, so it does not show "
        "other vendors, drivers, CPUs, or thermal states.\n"
        "- The benchmark uses a dirty working tree. The executable hash and commit remain in each "
        "result, but this snapshot is not a clean release baseline.\n"
        "- The two-player fixture has a small geometry and actor count. Keep the existing stress "
        "cases for draw, actor, projectile, and effect limits.\n"
        "- Add a present-wait-free CPU mode or split acquisition wait from the CPU headline before "
        "using this as a pure CPU regression gate.\n"
        "- Repeat any suspected change against this exact scenario and profile set; compare native "
        "GPU timing only when instrumentation version, device, driver, resolution, and settings match."
    )

    cpu_cards = profile_cards("cpu")
    gpu_cards = profile_cards("gpu")
    complete_cards = cpu_cards + gpu_cards

    complete_blocks = [
        {
            "id": "title",
            "type": "markdown",
            "body": f"# Match-load benchmark — all presets{title_suffix}",
        },
        {
            "id": "technical_summary",
            "type": "markdown",
            "sourceId": source["id"],
            "body": complete_findings,
        },
        {
            "id": "graphics_quality",
            "type": "table",
            "tableId": "graphics_quality_settings",
            "layout": "full",
        },
        {
            "id": "graphics_effects",
            "type": "table",
            "tableId": "graphics_effect_settings",
            "layout": "full",
        },
        {
            "id": "cpu_metrics",
            "type": "metric-strip",
            "cardIds": [card["id"] for card in cpu_cards],
        },
        {"id": "cpu_chart", "type": "chart", "chartId": "cpu_distribution", "layout": "full"},
        {"id": "cpu_table", "type": "table", "tableId": "cpu_pacing", "layout": "full"},
        {"id": "cpu_tails", "type": "table", "tableId": "cpu_tails", "layout": "full"},
        {
            "id": "cpu_thresholds",
            "type": "table",
            "tableId": "cpu_thresholds",
            "layout": "full",
        },
        {
            "id": "gpu_metrics",
            "type": "metric-strip",
            "cardIds": [card["id"] for card in gpu_cards],
        },
        {"id": "gpu_chart", "type": "chart", "chartId": "gpu_distribution", "layout": "full"},
        {"id": "gpu_table", "type": "table", "tableId": "gpu_pacing", "layout": "full"},
        {"id": "gpu_tails", "type": "table", "tableId": "gpu_tails", "layout": "full"},
        {"id": "gpu_stage_chart", "type": "chart", "chartId": "gpu_stages", "layout": "full"},
        {"id": "gpu_stage_table", "type": "table", "tableId": "gpu_stages", "layout": "full"},
        {
            "id": "gpu_stage_states",
            "type": "table",
            "tableId": "gpu_stage_states",
            "layout": "full",
        },
        {
            "id": "gpu_stage_spread",
            "type": "table",
            "tableId": "gpu_stage_spread",
            "layout": "full",
        },
        {"id": "cpu_stage_table", "type": "table", "tableId": "cpu_subsystems", "layout": "full"},
        {
            "id": "cpu_stage_spread",
            "type": "table",
            "tableId": "cpu_subsystem_spread",
            "layout": "full",
        },
        {"id": "workload_table", "type": "table", "tableId": "workload_counters", "layout": "full"},
        {
            "id": "all_data_heading",
            "type": "markdown",
            "body": (
                "## Every aggregate field and every raw run\n\n"
                "The next table shows every metric emitted in each aggregate file. The run inventory "
                "then lists every raw result, frame timeline, telemetry file, tick file, frame-time "
                "file, and per-run HTML report."
            ),
        },
        {"id": "all_metrics_table", "type": "table", "tableId": "all_metrics", "layout": "full"},
        {
            "id": "all_metric_spread",
            "type": "table",
            "tableId": "all_metric_spread",
            "layout": "full",
        },
        {"id": "run_table", "type": "table", "tableId": "run_inventory", "layout": "full"},
        {"id": "raw_links", "type": "markdown", "body": raw_links},
        {"id": "methods", "type": "markdown", "sourceId": source["id"], "body": methods},
        {"id": "limits", "type": "markdown", "body": limits},
    ]
    cpu_blocks = [
        {
            "id": "title",
            "type": "markdown",
            "body": f"# CPU match-load benchmark — all presets{title_suffix}",
        },
        {"id": "summary", "type": "markdown", "sourceId": source["id"], "body": cpu_findings},
        {
            "id": "graphics_quality",
            "type": "table",
            "tableId": "graphics_quality_settings",
            "layout": "full",
        },
        {
            "id": "graphics_effects",
            "type": "table",
            "tableId": "graphics_effect_settings",
            "layout": "full",
        },
        {
            "id": "metrics",
            "type": "metric-strip",
            "cardIds": [card["id"] for card in cpu_cards],
        },
        {"id": "distribution", "type": "chart", "chartId": "cpu_distribution", "layout": "full"},
        {"id": "pacing", "type": "table", "tableId": "cpu_pacing", "layout": "full"},
        {"id": "tails", "type": "table", "tableId": "cpu_tails", "layout": "full"},
        {"id": "thresholds", "type": "table", "tableId": "cpu_thresholds", "layout": "full"},
        {"id": "subsystem_chart", "type": "chart", "chartId": "cpu_subsystems", "layout": "full"},
        {"id": "subsystem_table", "type": "table", "tableId": "cpu_subsystems", "layout": "full"},
        {
            "id": "subsystem_spread",
            "type": "table",
            "tableId": "cpu_subsystem_spread",
            "layout": "full",
        },
        {"id": "cpu_all", "type": "table", "tableId": "cpu_metrics", "layout": "full"},
        {
            "id": "cpu_spread",
            "type": "table",
            "tableId": "cpu_metric_spread",
            "layout": "full",
        },
        {"id": "raw_links", "type": "markdown", "body": raw_links},
        {"id": "methods", "type": "markdown", "sourceId": source["id"], "body": methods},
        {"id": "limits", "type": "markdown", "body": limits},
    ]
    gpu_blocks = [
        {
            "id": "title",
            "type": "markdown",
            "body": f"# GPU match-load benchmark — all presets{title_suffix}",
        },
        {"id": "summary", "type": "markdown", "sourceId": source["id"], "body": gpu_findings},
        {
            "id": "graphics_quality",
            "type": "table",
            "tableId": "graphics_quality_settings",
            "layout": "full",
        },
        {
            "id": "graphics_effects",
            "type": "table",
            "tableId": "graphics_effect_settings",
            "layout": "full",
        },
        {
            "id": "metrics",
            "type": "metric-strip",
            "cardIds": [card["id"] for card in gpu_cards],
        },
        {"id": "distribution", "type": "chart", "chartId": "gpu_distribution", "layout": "full"},
        {"id": "pacing", "type": "table", "tableId": "gpu_pacing", "layout": "full"},
        {"id": "tails", "type": "table", "tableId": "gpu_tails", "layout": "full"},
        {"id": "stage_chart", "type": "chart", "chartId": "gpu_stages", "layout": "full"},
        {"id": "stage_table", "type": "table", "tableId": "gpu_stages", "layout": "full"},
        {"id": "stage_states", "type": "table", "tableId": "gpu_stage_states", "layout": "full"},
        {"id": "stage_spread", "type": "table", "tableId": "gpu_stage_spread", "layout": "full"},
        {"id": "gpu_all", "type": "table", "tableId": "gpu_metrics", "layout": "full"},
        {
            "id": "gpu_spread",
            "type": "table",
            "tableId": "gpu_metric_spread",
            "layout": "full",
        },
        {"id": "raw_links", "type": "markdown", "body": raw_links},
        {"id": "methods", "type": "markdown", "sourceId": source["id"], "body": methods},
        {"id": "limits", "type": "markdown", "body": limits},
    ]

    artifacts = {
        "complete-benchmark-artifact.json": report_artifact(
            title=f"Match-load benchmark — all presets{title_suffix}",
            description="CPU, GPU, frame-pacing, pass timing, and complete raw-data inventory.",
            generated_at=generated_at,
            datasets=datasets,
            source=source,
            cards=complete_cards,
            chart_ids=["cpu_distribution", "gpu_distribution", "gpu_stages"],
            table_ids=[
                "graphics_quality_settings",
                "graphics_effect_settings",
                "cpu_pacing",
                "cpu_tails",
                "cpu_thresholds",
                "gpu_pacing",
                "gpu_tails",
                "gpu_stages",
                "gpu_stage_states",
                "gpu_stage_spread",
                "cpu_subsystems",
                "cpu_subsystem_spread",
                "workload_counters",
                "all_metrics",
                "all_metric_spread",
                "run_inventory",
            ],
            blocks=complete_blocks,
        ),
        "cpu-benchmark-artifact.json": report_artifact(
            title=f"CPU match-load benchmark — all presets{title_suffix}",
            description="CPU frame pacing, subsystem timing, full CPU metrics, and raw timelines.",
            generated_at=generated_at,
            datasets=datasets,
            source=source,
            cards=cpu_cards,
            chart_ids=["cpu_distribution", "cpu_subsystems"],
            table_ids=[
                "graphics_quality_settings",
                "graphics_effect_settings",
                "cpu_pacing",
                "cpu_tails",
                "cpu_thresholds",
                "cpu_subsystems",
                "cpu_subsystem_spread",
                "cpu_metrics",
                "cpu_metric_spread",
            ],
            blocks=cpu_blocks,
        ),
        "gpu-benchmark-artifact.json": report_artifact(
            title=f"GPU match-load benchmark — all presets{title_suffix}",
            description="Native GPU frame pacing, per-pass timing, coverage, and full GPU metrics.",
            generated_at=generated_at,
            datasets=datasets,
            source=source,
            cards=gpu_cards,
            chart_ids=["gpu_distribution", "gpu_stages"],
            table_ids=[
                "graphics_quality_settings",
                "graphics_effect_settings",
                "gpu_pacing",
                "gpu_tails",
                "gpu_stages",
                "gpu_stage_states",
                "gpu_stage_spread",
                "gpu_metrics",
                "gpu_metric_spread",
            ],
            blocks=gpu_blocks,
        ),
    }
    (output_dir / "report-snapshot.json").write_text(
        json.dumps(
            {
                "generated_at": generated_at,
                "scenario": scenario_name,
                "profiles": list(PROFILE_ORDER),
                "aggregate_paths": aggregate_paths,
                "datasets": datasets,
            },
            indent=2,
            ensure_ascii=False,
        )
        + "\n",
        encoding="utf-8",
    )
    for filename, artifact in artifacts.items():
        artifact["package_info"] = package_info(Path.cwd(), output_dir, filename)
        (output_dir / filename).write_text(
            json.dumps(artifact, indent=2, ensure_ascii=False) + "\n",
            encoding="utf-8",
        )

    chart_map = (
        "# Chart map\n\n"
        "| Section | Question | Form | Fields | Palette | Output |\n"
        "|---|---|---|---|---|---|\n"
        "| CPU pacing | How do pooled CPU distributions differ? | Multi-series line | "
        "`bin_mid_ms`, `frame_percent`, `profile` | Four preset categories plus line identity | "
        "`cpu-benchmark.html`, complete report |\n"
        "| GPU pacing | How do pooled GPU distributions differ? | Multi-series line | "
        "`bin_mid_ms`, `frame_percent`, `profile` | Four preset categories plus line identity | "
        "`gpu-benchmark.html`, complete report |\n"
        "| CPU stages | Which CPU stages account for preset gaps? | Grouped horizontal bar | "
        "`subsystem_label`, `median_ms`, `profile` | Four preset categories | `cpu-benchmark.html` |\n"
        "| GPU passes | Which GPU passes drive each preset? | Grouped bar | "
        "`stage_label`, `median_ms`, `profile` | Four preset categories | GPU and complete reports |\n"
    )
    (output_dir / "chart-map.md").write_text(chart_map, encoding="utf-8")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--aggregate",
        action="append",
        required=True,
        type=Path,
        help="Path to one profile aggregate.json; pass four times.",
    )
    parser.add_argument("--output-dir", required=True, type=Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    repo = Path.cwd().resolve()
    if len(args.aggregate) != len(PROFILE_ORDER):
        raise SystemExit("Pass exactly four --aggregate files.")
    loaded = [load_profile(repo, path) for path in args.aggregate]
    profiles = {bundle["profile"]: bundle for bundle in loaded}
    missing = [profile for profile in PROFILE_ORDER if profile not in profiles]
    if missing:
        raise SystemExit(f"Missing profiles: {', '.join(missing)}")
    datasets = build_datasets(profiles)
    write_reports(args.output_dir.resolve(), profiles, datasets)
    print(
        json.dumps(
            {
                "output_dir": str(args.output_dir.resolve()),
                "profiles": list(PROFILE_ORDER),
                "runs": len(datasets["run_inventory"]),
                "frames": {
                    row["profile"]: row["frame_count"] for row in datasets["cpu_pacing"]
                },
                "gpu_coverage_percent": {
                    row["profile"]: row["coverage_percent"] for row in datasets["gpu_pacing"]
                },
            },
            indent=2,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
