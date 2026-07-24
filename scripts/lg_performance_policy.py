"""Strict benchmark policy and comparison helpers.

Public entry points:

* ``load_policy(path, profile)`` loads and validates a version 1 policy.
* ``load_result_directory(path, expected_scenarios=None)`` loads aggregate manifests.
* ``compare_result_sets(baseline, candidate, policy)`` returns a stable comparison dict.
* ``render_markdown(comparison)`` renders the matching Markdown report.
* ``write_reports(comparison, output_directory)`` writes both report forms.

The module has no command-line or process code. A CLI can call these functions
without changing the current directory or benchmark artifacts.
"""

from __future__ import annotations

import json
import math
import statistics
from pathlib import Path
from typing import Any, Iterable


STATUSES = (
    "PASS", "WARN", "FAIL", "INCONCLUSIVE", "NOT_COMPARABLE",
    "UNAVAILABLE", "SKIPPED",
)


class PerformancePolicyError(ValueError):
    """A policy or result artifact is not safe to compare."""


_POLICY_KEYS = {"schema_version", "policy_version", "profiles"}
_PROFILE_KEYS = {
    "expected_scenarios", "required_repetitions", "minimum_valid_runs", "stability_cv_percent",
    "gpu_required", "comparability", "metrics", "hard_limits", "correctness",
}
_COMPARABILITY_KEYS = {"fatal", "warning", "info"}
_METRIC_KEYS = {
    "source", "statistic", "label", "unit", "direction", "required",
    "warn_relative_percent", "warn_absolute", "fail_relative_percent",
    "fail_absolute", "stability_cv_percent", "scenarios",
}
_LIMIT_KEYS = {"field", "label", "required", "maximum", "minimum", "equals"}
_FIELD_KEYS = {"path", "label", "required", "equals"}


def _object(value: Any, field: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise PerformancePolicyError(f"{field} must be an object")
    return value


def _closed(value: dict[str, Any], allowed: set[str], field: str) -> None:
    unknown = sorted(set(value) - allowed)
    if unknown:
        raise PerformancePolicyError(f"{field} has unknown field(s): {', '.join(unknown)}")


def _finite_number(value: Any, field: str, *, minimum: float | None = None) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        raise PerformancePolicyError(f"{field} must be a finite number")
    number = float(value)
    if minimum is not None and number < minimum:
        raise PerformancePolicyError(f"{field} must be at least {minimum:g}")
    return number


def _string_list(value: Any, field: str) -> list[str]:
    if not isinstance(value, list) or any(not isinstance(item, str) or not item for item in value):
        raise PerformancePolicyError(f"{field} must be a list of non-empty strings")
    if len(value) != len(set(value)):
        raise PerformancePolicyError(f"{field} contains duplicates")
    return list(value)


def load_policy(path: str | Path, profile: str) -> dict[str, Any]:
    """Load one closed version-1 profile and return a detached normalized dict."""
    source = Path(path)
    try:
        raw = json.loads(source.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise PerformancePolicyError(f"cannot read policy {source}: {error}") from error
    root = _object(raw, "policy")
    _closed(root, _POLICY_KEYS, "policy")
    if root.get("schema_version") != 1:
        raise PerformancePolicyError(f"unsupported policy schema version: {root.get('schema_version')!r}")
    if root.get("policy_version") != 1:
        raise PerformancePolicyError(f"unsupported policy version: {root.get('policy_version')!r}")
    profiles = _object(root.get("profiles"), "profiles")
    if set(profiles) != {"pr_headless", "trusted_gpu"}:
        raise PerformancePolicyError("profiles must contain exactly pr_headless and trusted_gpu")
    if profile not in profiles:
        raise PerformancePolicyError(f"unknown policy profile: {profile}")
    normalized = _validate_profile(profiles[profile], profile)
    normalized.update({"version": 1, "profile": profile, "policy_path": str(source)})
    return normalized


def _validate_profile(raw: Any, name: str) -> dict[str, Any]:
    value = _object(raw, f"profiles.{name}")
    _closed(value, _PROFILE_KEYS, f"profiles.{name}")
    required = _PROFILE_KEYS - {"hard_limits", "correctness"}
    missing = sorted(required - set(value))
    if missing:
        raise PerformancePolicyError(f"profiles.{name} missing field(s): {', '.join(missing)}")
    scenarios = _string_list(value["expected_scenarios"], f"profiles.{name}.expected_scenarios")
    repetitions = value["required_repetitions"]
    if isinstance(repetitions, bool) or not isinstance(repetitions, int) or repetitions < 1:
        raise PerformancePolicyError(f"profiles.{name}.required_repetitions must be a positive integer")
    minimum_runs = value["minimum_valid_runs"]
    if isinstance(minimum_runs, bool) or not isinstance(minimum_runs, int) or minimum_runs < 1:
        raise PerformancePolicyError(f"profiles.{name}.minimum_valid_runs must be a positive integer")
    stability = _finite_number(value["stability_cv_percent"], f"profiles.{name}.stability_cv_percent", minimum=0)
    if not isinstance(value["gpu_required"], bool):
        raise PerformancePolicyError(f"profiles.{name}.gpu_required must be a boolean")
    comp = _object(value["comparability"], f"profiles.{name}.comparability")
    _closed(comp, _COMPARABILITY_KEYS, f"profiles.{name}.comparability")
    if set(comp) != _COMPARABILITY_KEYS:
        raise PerformancePolicyError(f"profiles.{name}.comparability must define fatal, warning, and info")
    comparability = {key: _string_list(comp[key], f"comparability.{key}") for key in sorted(comp)}
    all_fields = [field for fields in comparability.values() for field in fields]
    if len(all_fields) != len(set(all_fields)):
        raise PerformancePolicyError("comparability fields may occur in only one class")
    metrics_raw = _object(value["metrics"], f"profiles.{name}.metrics")
    if not metrics_raw:
        raise PerformancePolicyError(f"profiles.{name}.metrics must not be empty")
    metrics = {metric: _validate_metric(metric, rule) for metric, rule in sorted(metrics_raw.items())}
    for metric, rule in metrics.items():
        unknown_scenarios = sorted(set(rule.get("scenarios", scenarios)) - set(scenarios))
        if unknown_scenarios:
            raise PerformancePolicyError(
                f"metrics.{metric}.scenarios names unknown scenario(s): {', '.join(unknown_scenarios)}"
            )
    if minimum_runs > repetitions:
        raise PerformancePolicyError(f"profiles.{name}.minimum_valid_runs exceeds required_repetitions")
    hard_limits = [_validate_limit(item, f"hard_limits[{index}]") for index, item in enumerate(value.get("hard_limits", []))]
    correctness = [_validate_field(item, f"correctness[{index}]") for index, item in enumerate(value.get("correctness", []))]
    return {
        "expected_scenarios": scenarios, "required_repetitions": repetitions,
        "minimum_valid_runs": minimum_runs,
        "stability_cv_percent": stability, "gpu_required": value["gpu_required"],
        "comparability": comparability, "metrics": metrics,
        "hard_limits": hard_limits, "correctness": correctness,
    }


def _validate_metric(name: str, raw: Any) -> dict[str, Any]:
    if not isinstance(name, str) or not name:
        raise PerformancePolicyError("metric names must be non-empty strings")
    value = _object(raw, f"metrics.{name}")
    _closed(value, _METRIC_KEYS, f"metrics.{name}")
    required = {
        "source", "statistic", "label", "unit", "direction", "required",
        "warn_relative_percent", "warn_absolute", "fail_relative_percent", "fail_absolute",
    }
    missing = sorted(required - set(value))
    if missing:
        raise PerformancePolicyError(f"metrics.{name} missing field(s): {', '.join(missing)}")
    for key in ("source", "statistic", "label", "unit"):
        if not isinstance(value[key], str) or not value[key]:
            raise PerformancePolicyError(f"metrics.{name}.{key} must be a non-empty string")
    if value["direction"] not in {"lower", "higher"}:
        raise PerformancePolicyError(f"metrics.{name}.direction must be lower or higher")
    if not isinstance(value["required"], bool):
        raise PerformancePolicyError(f"metrics.{name}.required must be a boolean")
    result = dict(value)
    for key in ("warn_relative_percent", "warn_absolute", "fail_relative_percent", "fail_absolute"):
        result[key] = _finite_number(value[key], f"metrics.{name}.{key}", minimum=0)
    if result["fail_relative_percent"] < result["warn_relative_percent"] or result["fail_absolute"] < result["warn_absolute"]:
        raise PerformancePolicyError(f"metrics.{name} fail thresholds must not be below warn thresholds")
    if "stability_cv_percent" in result:
        result["stability_cv_percent"] = _finite_number(result["stability_cv_percent"], f"metrics.{name}.stability_cv_percent", minimum=0)
    if "scenarios" in result:
        result["scenarios"] = _string_list(result["scenarios"], f"metrics.{name}.scenarios")
    return result


def _validate_limit(raw: Any, field: str) -> dict[str, Any]:
    value = _object(raw, field)
    _closed(value, _LIMIT_KEYS, field)
    if set(value) < {"field", "label", "required"}:
        raise PerformancePolicyError(f"{field} requires field, label, and required")
    modes = set(value) & {"maximum", "minimum", "equals"}
    if len(modes) != 1:
        raise PerformancePolicyError(f"{field} must define exactly one of maximum, minimum, or equals")
    if not isinstance(value["field"], str) or not value["field"] or not isinstance(value["label"], str) or not value["label"]:
        raise PerformancePolicyError(f"{field} field and label must be non-empty strings")
    if not isinstance(value["required"], bool):
        raise PerformancePolicyError(f"{field}.required must be a boolean")
    result = dict(value)
    for key in modes - {"equals"}:
        result[key] = _finite_number(value[key], f"{field}.{key}")
    return result


def _validate_field(raw: Any, field: str) -> dict[str, Any]:
    value = _object(raw, field)
    _closed(value, _FIELD_KEYS, field)
    if set(value) != _FIELD_KEYS:
        raise PerformancePolicyError(f"{field} requires path, label, required, and equals")
    if not isinstance(value["path"], str) or not value["path"] or not isinstance(value["label"], str) or not value["label"]:
        raise PerformancePolicyError(f"{field} path and label must be non-empty strings")
    if not isinstance(value["required"], bool):
        raise PerformancePolicyError(f"{field}.required must be a boolean")
    return dict(value)


def load_result_directory(path: str | Path, expected_scenarios: Iterable[str] | None = None) -> dict[str, Any]:
    """Load one aggregate manifest for every expected scenario."""
    root = Path(path)
    if not root.is_dir():
        raise PerformancePolicyError(f"result directory does not exist: {root}")
    manifests = sorted(root.rglob("aggregate.json"), key=lambda item: item.as_posix())
    if not manifests:
        raise PerformancePolicyError(f"no aggregate.json manifests found in {root}")
    loaded: dict[str, dict[str, Any]] = {}
    paths: dict[str, str] = {}
    for manifest_path in manifests:
        try:
            value = json.loads(manifest_path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as error:
            raise PerformancePolicyError(f"malformed aggregate manifest {manifest_path}: {error}") from error
        _validate_manifest(value, manifest_path)
        scenario = value["scenario"]["name"]
        if scenario in loaded:
            raise PerformancePolicyError(
                f"duplicate aggregate manifests for scenario {scenario}: {paths[scenario]} and {manifest_path}"
            )
        loaded[scenario] = value
        paths[scenario] = str(manifest_path)
    expected = list(expected_scenarios) if expected_scenarios is not None else sorted(loaded)
    missing = sorted(set(expected) - set(loaded))
    extra = sorted(set(loaded) - set(expected))
    if missing:
        raise PerformancePolicyError(f"missing aggregate manifest(s): {', '.join(missing)}")
    if extra and expected_scenarios is not None:
        raise PerformancePolicyError(f"unexpected aggregate manifest(s): {', '.join(extra)}")
    commits = {
        str(value.get("git", {}).get("commit"))
        for value in loaded.values()
        if value.get("git", {}).get("commit") not in {None, "", "unknown"}
    }
    if len(commits) > 1:
        raise PerformancePolicyError(
            "result set contains aggregate manifests from different commits"
        )
    return {
        "schema_version": 1, "root": str(root),
        "scenarios": {name: loaded[name] for name in sorted(loaded)},
        "artifacts": {name: paths[name] for name in sorted(paths)},
    }


def _validate_manifest(value: Any, path: Path) -> None:
    if not isinstance(value, dict):
        raise PerformancePolicyError(f"aggregate manifest must be an object: {path}")
    if value.get("schema_version") != 1:
        raise PerformancePolicyError(f"aggregate manifest has unsupported schema_version in {path}")
    scenario = value.get("scenario")
    if not isinstance(scenario, dict) or not isinstance(scenario.get("name"), str) or not scenario["name"]:
        raise PerformancePolicyError(f"aggregate manifest missing scenario.name in {path}")
    if not isinstance(value.get("scenario_hash"), str) or not value["scenario_hash"]:
        raise PerformancePolicyError(f"aggregate manifest missing scenario_hash in {path}")
    if not isinstance(value.get("aggregate"), dict):
        raise PerformancePolicyError(f"aggregate manifest missing aggregate object in {path}")
    if "runs" in value and not isinstance(value["runs"], list):
        raise PerformancePolicyError(f"aggregate manifest runs must be an array in {path}")


_MISSING = object()


def _path_get(value: Any, path: str) -> Any:
    current = value
    for part in path.split("."):
        if not isinstance(current, dict) or part not in current:
            return _MISSING
        current = current[part]
    return current


def _all_path_values(value: Any, path: str) -> list[Any]:
    """Find a dotted field both at root and in every run/native result."""
    found: list[Any] = []
    direct = _path_get(value, path)
    if direct is not _MISSING:
        found.append(direct)
    for run in value.get("runs", []) if isinstance(value, dict) else []:
        for source in (run, run.get("summary", {}), run.get("native", {})):
            item = _path_get(source, path)
            if item is not _MISSING:
                found.append(item)
    native = value.get("native_result", {}) if isinstance(value, dict) else {}
    item = _path_get(native, path)
    if item is not _MISSING:
        found.append(item)
    if not found and "." not in path:
        def visit(node: Any) -> None:
            if isinstance(node, dict):
                for key, child in node.items():
                    if key == path:
                        if isinstance(child, dict):
                            for statistic in ("max", "value", "count"):
                                if statistic in child:
                                    found.append(child[statistic])
                                    break
                        else:
                            found.append(child)
                    visit(child)
            elif isinstance(node, list):
                for child in node:
                    visit(child)
        visit(value)
    return found


def _nearest_rank(values: list[float], percentile: float) -> float:
    if not values:
        raise ValueError("empty sample")
    ordered = sorted(values)
    rank = max(1, math.ceil(percentile / 100.0 * len(ordered)))
    return ordered[rank - 1]


def _tukey(values: list[float]) -> dict[str, Any]:
    if len(values) < 4:
        return {"indices": [], "values": [], "lower_fence": None, "upper_fence": None}
    q1, q3 = _nearest_rank(values, 25), _nearest_rank(values, 75)
    spread = q3 - q1
    lower, upper = q1 - 1.5 * spread, q3 + 1.5 * spread
    indices = [index + 1 for index, value in enumerate(values) if value < lower or value > upper]
    return {
        "indices": indices, "values": [values[index - 1] for index in indices],
        "lower_fence": lower, "upper_fence": upper,
    }


def _summary(values: list[float]) -> dict[str, Any]:
    mean = statistics.fmean(values)
    deviation = statistics.stdev(values) if len(values) > 1 else 0.0
    return {
        "count": len(values), "median": statistics.median(values),
        "p95": _nearest_rank(values, 95), "p99": _nearest_rank(values, 99),
        "max": max(values), "mean": mean,
        "cv_percent": abs(deviation / mean * 100.0) if mean else (0.0 if deviation == 0 else math.inf),
        "run_values": list(values), "outliers": _tukey(values),
    }


def _metric_values(manifest: dict[str, Any], source: str, statistic: str) -> list[float]:
    aggregate_metric = manifest.get("aggregate", {}).get("metrics", {}).get(source, {})
    values = aggregate_metric.get(f"run_{statistic}_values")
    if statistic == "median":
        values = aggregate_metric.get("run_values", values)
    if isinstance(values, list):
        return [float(item) for item in values if _is_number(item)]
    result = []
    for run in manifest.get("runs", []):
        summary = run.get("summary", {}) if isinstance(run, dict) else {}
        raw = summary.get(source, _MISSING)
        if isinstance(raw, dict):
            raw = raw.get(statistic, raw.get("value", _MISSING))
        elif statistic != "median":
            raw = _MISSING
        if _is_number(raw):
            result.append(float(raw))
    if result:
        return result
    aggregate_value = aggregate_metric.get(statistic, _MISSING)
    return [float(aggregate_value)] if _is_number(aggregate_value) else []


def _is_number(value: Any) -> bool:
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value)


def _display(value: Any) -> str:
    return "<missing>" if value is _MISSING else json.dumps(value, sort_keys=True, separators=(",", ":"))


def _compare_fields(baseline: dict[str, Any], candidate: dict[str, Any], policy: dict[str, Any]) -> dict[str, list[dict[str, Any]]]:
    result: dict[str, list[dict[str, Any]]] = {"fatal": [], "warning": [], "info": []}
    for level in ("fatal", "warning", "info"):
        for path in policy["comparability"][level]:
            left, right = _path_get(baseline, path), _path_get(candidate, path)
            if left is _MISSING or right is _MISSING or left != right:
                result[level].append({
                    "field": path, "baseline": _display(left), "candidate": _display(right),
                    "reason": "required field missing" if left is _MISSING or right is _MISSING else "values differ",
                })
    if policy["gpu_required"]:
        for side, manifest in (("baseline", baseline), ("candidate", candidate)):
            backend = str(_path_get(manifest, "settings.backend")).lower()
            renderer = str(_path_get(manifest, "environment.renderer")).lower()
            verified = _path_get(manifest, "environment.gpu_verified")
            fallback = "fallback" in renderer or "sdl_renderer" in renderer or "headless" in renderer
            if verified is not True or fallback or not any(token in backend for token in ("gpu", "vulkan")):
                result["fatal"].append({
                    "field": "gpu_requirement", "baseline": side, "candidate": renderer,
                    "reason": "GPU-required benchmark used an unverified or fallback renderer",
                })
    return result


def _metric_status(delta: float, relative: float | None, rule: dict[str, Any]) -> str:
    regression = delta if rule["direction"] == "lower" else -delta
    relative_regression = relative if rule["direction"] == "lower" else (-relative if relative is not None else None)
    if regression <= 0:
        return "PASS"
    # A zero baseline has no useful ratio; absolute limits alone decide it.
    if relative_regression is None:
        if regression > rule["fail_absolute"]:
            return "FAIL"
        if regression > rule["warn_absolute"]:
            return "WARN"
        return "PASS"
    if regression > rule["fail_absolute"] and relative_regression > rule["fail_relative_percent"]:
        return "FAIL"
    if regression > rule["warn_absolute"] and relative_regression > rule["warn_relative_percent"]:
        return "WARN"
    return "PASS"


def _evaluate_limits(candidate: dict[str, Any], policy: dict[str, Any]) -> list[dict[str, Any]]:
    checks: list[dict[str, Any]] = []
    for rule in policy["hard_limits"]:
        values = _all_path_values(candidate, rule["field"])
        numeric = [float(value) for value in values if _is_number(value)]
        observed: Any = max(numeric) if "maximum" in rule and numeric else min(numeric) if "minimum" in rule and numeric else values[-1] if values else _MISSING
        if observed is _MISSING:
            status = "FAIL" if rule["required"] else "UNAVAILABLE"
            reason = "required value missing" if rule["required"] else "optional value missing"
        elif "maximum" in rule:
            status, reason = ("FAIL", "hard maximum exceeded") if observed > rule["maximum"] else ("PASS", "")
        elif "minimum" in rule:
            status, reason = ("FAIL", "hard minimum not met") if observed < rule["minimum"] else ("PASS", "")
        else:
            status, reason = ("FAIL", "required value differs") if observed != rule["equals"] else ("PASS", "")
        checks.append({
            "label": rule["label"], "field": rule["field"], "status": status,
            "observed": None if observed is _MISSING else observed,
            "limit": rule.get("maximum", rule.get("minimum", rule.get("equals"))), "reason": reason,
        })
    for rule in policy["correctness"]:
        values = _all_path_values(candidate, rule["path"])
        observed = values[-1] if values else _MISSING
        if observed is _MISSING:
            status = "FAIL" if rule["required"] else "UNAVAILABLE"
            reason = "required evidence missing" if rule["required"] else "optional evidence missing"
        else:
            failed = any(value != rule["equals"] for value in values)
            status, reason = ("FAIL", "correctness check failed") if failed else ("PASS", "")
        checks.append({
            "label": rule["label"], "field": rule["path"], "status": status,
            "observed": None if observed is _MISSING else observed, "limit": rule["equals"], "reason": reason,
        })
    return checks


def compare_result_sets(
    baseline: dict[str, Any] | str | Path,
    candidate: dict[str, Any] | str | Path,
    policy: dict[str, Any],
) -> dict[str, Any]:
    """Compare two loaded result sets (or directory paths) under one profile."""
    expected = policy["expected_scenarios"]
    if not isinstance(baseline, dict):
        baseline = load_result_directory(baseline, expected)
    if not isinstance(candidate, dict):
        candidate = load_result_directory(candidate, expected)
    output: dict[str, Any] = {
        "schema_version": 1, "policy_version": policy["version"], "profile": policy["profile"],
        "required_repetitions": policy.get("required_repetitions", policy["minimum_valid_runs"]),
        "status": "PASS", "comparable": True, "comparability": {"fatal": [], "warning": [], "info": []},
        "correctness": [], "metrics": [], "unavailable": [], "inconclusive": [],
        "artifacts": {
            "baseline": baseline.get("root", ""), "candidate": candidate.get("root", ""),
            "baseline_manifests": baseline.get("artifacts", {}),
            "candidate_manifests": candidate.get("artifacts", {}),
        },
    }
    statuses: list[str] = []
    for scenario in expected:
        left = baseline["scenarios"][scenario]
        right = candidate["scenarios"][scenario]
        compared = _compare_fields(left, right, policy)
        for level in output["comparability"]:
            output["comparability"][level].extend({"scenario": scenario, **item} for item in compared[level])
        checks = _evaluate_limits(right, policy)
        output["correctness"].extend({"scenario": scenario, **item} for item in checks)
        statuses.extend(item["status"] for item in checks)
        if compared["fatal"]:
            for metric_name, rule in policy["metrics"].items():
                output["metrics"].append({
                    "scenario": scenario, "name": metric_name, "label": rule["label"],
                    "unit": rule["unit"], "direction": rule["direction"], "status": "SKIPPED",
                    "reason": "scenario is not comparable", "baseline": None, "candidate": None,
                })
            continue
        valid_left = sum(1 for run in left.get("runs", []) if run.get("valid", True) is True)
        valid_right = sum(1 for run in right.get("runs", []) if run.get("valid", True) is True)
        for metric_name, rule in policy["metrics"].items():
            if "scenarios" in rule and scenario not in rule["scenarios"]:
                output["metrics"].append({
                    "scenario": scenario, "name": metric_name, "label": rule["label"], "unit": rule["unit"],
                    "status": "SKIPPED", "reason": "metric does not apply to this scenario",
                    "baseline": None, "candidate": None,
                })
                continue
            left_values = _metric_values(left, rule["source"], rule["statistic"])
            right_values = _metric_values(right, rule["source"], rule["statistic"])
            if not left_values or not right_values:
                status = "FAIL" if rule["required"] else "UNAVAILABLE"
                reason = "required metric missing" if rule["required"] else "optional metric missing"
                item = {"scenario": scenario, "name": metric_name, "label": rule["label"], "unit": rule["unit"],
                        "status": status, "reason": reason, "baseline": None, "candidate": None}
                output["metrics"].append(item)
                statuses.append(status)
                if status == "UNAVAILABLE":
                    output["unavailable"].append(f"{scenario}: {metric_name}")
                continue
            left_stats, right_stats = _summary(left_values), _summary(right_values)
            minimum = policy["minimum_valid_runs"]
            count_left = valid_left if left.get("runs") else left_stats["count"]
            count_right = valid_right if right.get("runs") else right_stats["count"]
            cv_limit = rule.get("stability_cv_percent", policy["stability_cv_percent"])
            unstable = (
                count_left < minimum or count_right < minimum
                or left_stats["cv_percent"] > cv_limit or right_stats["cv_percent"] > cv_limit
            )
            base_value, new_value = left_stats["median"], right_stats["median"]
            delta = new_value - base_value
            relative = None if base_value == 0 else delta / abs(base_value) * 100.0
            status = "INCONCLUSIVE" if unstable else _metric_status(delta, relative, rule)
            reason = "insufficient valid runs or unstable samples" if unstable else ""
            item = {
                "scenario": scenario, "name": metric_name, "label": rule["label"], "unit": rule["unit"],
                "direction": rule["direction"],
                "status": status, "reason": reason, "baseline": base_value, "candidate": new_value,
                "absolute_change": delta, "relative_change_percent": relative,
                "baseline_statistics": left_stats, "candidate_statistics": right_stats,
            }
            output["metrics"].append(item)
            statuses.append(status)
            if status == "INCONCLUSIVE":
                output["inconclusive"].append(f"{scenario}: {metric_name}")
    if "FAIL" in statuses:
        output["status"] = "FAIL"
        if output["comparability"]["fatal"]:
            output["comparable"] = False
            for item in output["metrics"]:
                item["status"] = "SKIPPED"
    elif output["comparability"]["fatal"]:
        output["status"], output["comparable"] = "NOT_COMPARABLE", False
        for item in output["metrics"]:
            item["status"] = "SKIPPED"
    elif "INCONCLUSIVE" in statuses:
        output["status"] = "INCONCLUSIVE"
    elif "WARN" in statuses or output["comparability"]["warning"]:
        output["status"] = "WARN"
    elif "UNAVAILABLE" in statuses:
        output["status"] = "UNAVAILABLE"
    output["summary"] = _comparison_summary(output["metrics"])
    return output


def _comparison_summary(metrics: list[dict[str, Any]]) -> dict[str, Any]:
    measured = [item for item in metrics if item.get("relative_change_percent") is not None]
    def signed(item: dict[str, Any]) -> float:
        delta = item["relative_change_percent"]
        return delta if item.get("direction", "lower") == "lower" else -delta
    regressions = [item for item in measured if signed(item) > 0]
    improvements = [item for item in measured if signed(item) < 0]
    key = lambda item: (abs(signed(item)), item["scenario"], item["name"])
    return {
        "largest_regression": max(regressions, key=key) if regressions else None,
        "largest_improvement": max(improvements, key=key) if improvements else None,
    }


def render_markdown(comparison: dict[str, Any]) -> str:
    """Render stable Markdown with correctness and hard failures first."""
    lines = ["## Performance comparison", "", f"**Status: {comparison['status']}**", "", "### Correctness and hard limits", ""]
    lines += ["| Scenario | Check | Observed | Limit | Status |", "|---|---|---:|---:|---|"]
    if comparison["correctness"]:
        for item in comparison["correctness"]:
            lines.append(f"| {item['scenario']} | {item['label']} | {_md(item['observed'])} | {_md(item['limit'])} | {item['status']} |")
    else:
        lines.append("| — | No configured checks | — | — | PASS |")
    hard_failures = [item for item in comparison["correctness"] if item["status"] == "FAIL"]
    if hard_failures:
        lines += ["", "### Hard failures", ""]
        lines += [f"- {item['scenario']}: {item['label']} ({item['reason']})" for item in hard_failures]
    lines += ["", "### Metrics", "", "| Scenario | Metric | Baseline | Candidate | Change | Status |",
              "|---|---|---:|---:|---:|---|"]
    for item in comparison["metrics"]:
        relative = item.get("relative_change_percent")
        change = "—" if item.get("absolute_change") is None else f"{item['absolute_change']:+.3g} {item['unit']}"
        if relative is not None:
            change += f" / {relative:+.2f}%"
        lines.append(
            f"| {item['scenario']} | {item['label']} | {_md(item.get('baseline'))} | "
            f"{_md(item.get('candidate'))} | {change} | {item['status']} |"
        )
    for heading, key in (("Largest regression", "largest_regression"), ("Largest improvement", "largest_improvement")):
        item = comparison["summary"][key]
        lines += ["", f"### {heading}", ""]
        lines.append("None." if item is None else f"{item['scenario']}: {item['label']} ({item['relative_change_percent']:+.2f}%).")
    lines += ["", "### Unavailable and inconclusive", ""]
    entries = comparison["unavailable"] + comparison["inconclusive"]
    lines += [f"- {item}" for item in entries] if entries else ["None."]
    lines += ["", "### Comparability", ""]
    issues = [
        (level.upper(), item) for level in ("fatal", "warning", "info")
        for item in comparison["comparability"][level]
    ]
    lines += [f"- {level}: {item['scenario']} {item['field']} — {item['reason']}" for level, item in issues] if issues else ["No issues."]
    lines += ["", "### Artifacts", "", f"- Baseline: `{comparison['artifacts']['baseline']}`",
              f"- Candidate: `{comparison['artifacts']['candidate']}`", ""]
    return "\n".join(lines)


def _md(value: Any) -> str:
    if value is None:
        return "—"
    if isinstance(value, float):
        return f"{value:.6g}"
    if isinstance(value, (list, dict)):
        return f"`{json.dumps(value, sort_keys=True, separators=(',', ':'))}`"
    return str(value).replace("|", "\\|")


def write_reports(comparison: dict[str, Any], output_directory: str | Path) -> dict[str, str]:
    """Write deterministic JSON and Markdown reports and return their paths."""
    root = Path(output_directory)
    root.mkdir(parents=True, exist_ok=True)
    json_path, markdown_path = root / "performance-comparison.json", root / "performance-comparison.md"
    json_path.write_text(json.dumps(comparison, indent=2, sort_keys=True, ensure_ascii=False) + "\n", encoding="utf-8")
    markdown_path.write_text(render_markdown(comparison), encoding="utf-8")
    return {"json": str(json_path), "markdown": str(markdown_path)}
