#!/usr/bin/env python3
"""Write small, portable evidence records for PR verification jobs."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any, Iterable


SCHEMA_VERSION = 1
CATEGORIES = ("build", "unit", "determinism", "live", "protocol", "performance", "gpu")
PLATFORMS = ("linux", "windows", "gpu")
INPUT_STATUSES = (
    "success", "failure", "cancelled", "skipped", "pass", "warn", "fail",
    "inconclusive", "not_comparable", "unavailable", "error",
)
STATUS_MAP = {
    "success": "PASS", "pass": "PASS", "failure": "FAIL", "fail": "FAIL",
    "cancelled": "CANCELLED", "skipped": "SKIPPED", "warn": "WARN",
    "inconclusive": "INCONCLUSIVE", "unavailable": "UNAVAILABLE",
    "not_comparable": "NOT_COMPARABLE", "error": "ERROR",
}
ROOT = Path(__file__).resolve().parent.parent


def normalize_status(value: str) -> str:
    """Return the one stable spelling used by evidence records."""
    try:
        return STATUS_MAP[value.strip().lower()]
    except KeyError as error:
        raise ValueError("unsupported status: " + value) from error


def _portable_path(path: Path, root: Path) -> str:
    return path.relative_to(root).as_posix()


def _atomic_json(path: Path, value: Any) -> None:
    _atomic_text(path, json.dumps(value, indent=2, sort_keys=True) + "\n")


def _atomic_text(path: Path, value: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(
        "w", encoding="utf-8", dir=path.parent, prefix="." + path.name + ".", delete=False
    ) as handle:
        handle.write(value)
        temporary = Path(handle.name)
    os.replace(temporary, path)


def _read_json(path: Path) -> dict[str, Any]:
    if not path.is_file():
        return {}
    try:
        loaded = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return {}
    return loaded if isinstance(loaded, dict) else {}


def _run_text(arguments: list[str]) -> str | None:
    try:
        completed = subprocess.run(
            arguments, cwd=ROOT, capture_output=True, text=True, check=False, timeout=10
        )
    except (OSError, subprocess.SubprocessError):
        return None
    return completed.stdout.strip() if completed.returncode == 0 else None


def _protocol_version() -> int | None:
    candidates = sorted(ROOT.glob("src/**/*.hpp")) + sorted(ROOT.glob("src/**/*.h"))
    pattern = re.compile(r"kProtocolVersion\s*=\s*(\d+)")
    for candidate in candidates:
        try:
            found = pattern.search(candidate.read_text(encoding="utf-8"))
        except OSError:
            continue
        if found:
            return int(found.group(1))
    return None


def _env_value(*names: str) -> str | None:
    for name in names:
        value = os.environ.get(name)
        if value:
            return value
    return None


def evidence_identity() -> dict[str, Any]:
    """Collect only portable values that identify a CI run and its revisions."""
    return {
        "baseline": _env_value("LG_BASELINE_COMMIT", "GITHUB_BASE_SHA"),
        "candidate": _env_value("LG_CANDIDATE_COMMIT", "GITHUB_SHA") or _run_text(["git", "rev-parse", "HEAD"]),
        "repository": _env_value("GITHUB_REPOSITORY") or _run_text(["git", "config", "--get", "remote.origin.url"]),
        "workflow": {
            "job": _env_value("GITHUB_JOB"),
            "name": _env_value("GITHUB_WORKFLOW"),
            "run_attempt": _env_value("GITHUB_RUN_ATTEMPT"),
            "run_id": _env_value("GITHUB_RUN_ID"),
        },
    }


def discovered_versions() -> dict[str, Any]:
    return {
        "benchmark": _env_value("LG_BENCHMARK_VERSION"),
        "build": _env_value("LG_BUILD_VERSION", "CMAKE_BUILD_TYPE"),
        "platform": _env_value("LG_PLATFORM_VERSION"),
        "protocol": _protocol_version(),
        "scenario": _env_value("LG_SCENARIO_VERSION"),
    }


def renderer_identity() -> dict[str, Any]:
    return {
        "backend": _env_value("LG_RENDERER", "LG_RENDERER_BACKEND"),
        "driver": _env_value("LG_GRAPHICS_DRIVER", "LG_GPU_DRIVER"),
        "gpu": _env_value("LG_GPU_NAME", "GPU_NAME"),
    }


def artifact_index(root: Path) -> list[dict[str, Any]]:
    """Index evidence inputs without hashing files whose contents contain this index."""
    excluded = {"manifest.json", "summary.json"}
    entries: list[dict[str, Any]] = []
    for path in sorted(root.rglob("*"), key=lambda item: item.as_posix()):
        relative = path.relative_to(root)
        if (
            not path.is_file()
            or _portable_path(path, root) in excluded
            or "temp" in relative.parts
        ):
            continue
        digest = hashlib.sha256()
        with path.open("rb") as handle:
            for block in iter(lambda: handle.read(1024 * 1024), b""):
                digest.update(block)
        entries.append({
            "path": _portable_path(path, root),
            "sha256": digest.hexdigest(),
            "size_bytes": path.stat().st_size,
        })
    return entries


def _safe_evidence_root(value: str) -> Path:
    root = Path(value).expanduser().resolve()
    if str(root) == root.anchor:
        raise ValueError("evidence root must not be a filesystem root")
    root.mkdir(parents=True, exist_ok=True)
    return root


def _summary_status(categories: dict[str, dict[str, str]]) -> str:
    statuses = {entry["status"] for entry in categories.values()}
    for status in (
        "ERROR", "FAIL", "NOT_COMPARABLE", "WARN", "INCONCLUSIVE",
        "UNAVAILABLE", "CANCELLED", "PASS", "SKIPPED",
    ):
        if status in statuses:
            return status
    return "UNAVAILABLE"


def collect_ci(evidence_root: Path, platform: str, category: str, status: str) -> dict[str, Any]:
    manifest_path = evidence_root / "manifest.json"
    previous = _read_json(manifest_path)
    categories = previous.get("categories", {})
    categories = categories if isinstance(categories, dict) else {}
    categories[category] = {"platform": platform, "status": normalize_status(status)}
    categories = {name: categories[name] for name in sorted(categories)}
    unavailable = [name for name, entry in categories.items()
                   if isinstance(entry, dict) and entry.get("status") == "UNAVAILABLE"]
    manifest = {
        "artifacts": artifact_index(evidence_root),
        "categories": categories,
        "identity": evidence_identity(),
        "missing_categories": [name for name in CATEGORIES if name not in categories],
        "renderer": renderer_identity(),
        "schema_version": SCHEMA_VERSION,
        "unavailable_categories": unavailable,
        "versions": discovered_versions(),
    }
    _atomic_json(manifest_path, manifest)
    summary = {
        "categories": categories,
        "missing_categories": manifest["missing_categories"],
        "schema_version": SCHEMA_VERSION,
        "status": _summary_status(categories),
        "unavailable_categories": unavailable,
    }
    _atomic_json(evidence_root / "summary.json", summary)
    return manifest


def _find_protocol_binary(build_dir: Path) -> Path | None:
    names = ("lg_duel_protocol_tests.exe", "lg_duel_protocol_tests")
    candidates: list[Path] = []
    for name in names:
        candidates.extend(build_dir.rglob(name)) if build_dir.is_dir() else None
    for candidate in sorted(candidates, key=lambda item: item.as_posix()):
        if candidate.is_file():
            return candidate
    return None


def _source_budget() -> int | None:
    source = ROOT / "src" / "net" / "NetCodec.hpp"
    try:
        text = source.read_text(encoding="utf-8")
    except OSError:
        return None
    matched = re.search(r"kMaxUdpApplicationDatagramBytes\s*=\s*(\d+)", text)
    return int(matched.group(1)) if matched else None


def parse_packet_facts(log: str) -> list[dict[str, Any]]:
    """Read byte facts emitted by protocol tests, keeping their printed labels."""
    facts: list[dict[str, Any]] = []
    seen: set[tuple[str, int]] = set()
    for line in log.splitlines():
        lower = line.lower()
        if "byte" not in lower and "datagram" not in lower:
            continue
        for label, number in re.findall(r"([a-zA-Z][a-zA-Z0-9_-]*)\s*=\s*(\d+)", line):
            if label.lower() == "bytes":
                continue
            item = (label, int(number))
            if item not in seen:
                facts.append({"bytes": item[1], "label": item[0]})
                seen.add(item)
        matched = re.search(r"([a-zA-Z][a-zA-Z0-9 _-]*(?:packet|snapshot|datagram)[a-zA-Z0-9 _-]*)\s+bytes\s*[:=]\s*(\d+)", line, re.I)
        if not matched:
            matched = re.search(
                r"([a-zA-Z][a-zA-Z0-9 _-]+?)\s+bytes\s*[:=]\s*(\d+)",
                line,
                re.I,
            )
        if matched:
            item = (" ".join(matched.group(1).split()).lower(), int(matched.group(2)))
            if item not in seen:
                facts.append({"bytes": item[1], "label": item[0]})
                seen.add(item)
    return sorted(facts, key=lambda item: (item["label"], item["bytes"]))


def protocol_budget(evidence_root: Path, build_dir: Path, ceiling: int) -> tuple[dict[str, Any], int]:
    protocol_dir = evidence_root / "protocol"
    protocol_dir.mkdir(parents=True, exist_ok=True)
    binary = _find_protocol_binary(build_dir)
    log = ""
    test_exit: int | None = None
    if binary is None:
        log = "Protocol test binary is unavailable.\n"
    else:
        try:
            completed = subprocess.run(
                [str(binary)], cwd=binary.parent, text=True, stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT, timeout=120, check=False,
            )
            test_exit = completed.returncode
            log = completed.stdout or ""
        except subprocess.TimeoutExpired as error:
            test_exit = 124
            log = (error.stdout or "") + "\nProtocol test timed out after 120 seconds.\n"
        except OSError as error:
            test_exit = 127
            log = "Could not run protocol test: " + str(error) + "\n"
    log_path = protocol_dir / "protocol-test.log"
    _atomic_text(log_path, log)
    facts = parse_packet_facts(log)
    observed = max((fact["bytes"] for fact in facts), default=None)
    source_budget = _source_budget()
    limit_ok = observed is not None and observed <= ceiling
    constant_ok = source_budget == ceiling
    if binary is None:
        status = "UNAVAILABLE"
    elif test_exit != 0 or not constant_ok or not limit_ok:
        status = "FAIL"
    else:
        status = "PASS"
    result = {
        "artifact_log": _portable_path(log_path, evidence_root),
        "authoritative_encode_test_exit": test_exit,
        "configured_datagram_ceiling_bytes": ceiling,
        "hard_limit_status": "PASS" if limit_ok else "FAIL",
        "max_observed_datagram_bytes": observed,
        "packet_facts": facts,
        "protocol_test_binary": _portable_path(binary, build_dir) if binary else None,
        "schema_version": SCHEMA_VERSION,
        "source_constant_bytes": source_budget,
        "source_constant_status": "PASS" if constant_ok else "FAIL",
        "status": status,
    }
    _atomic_json(protocol_dir / "packet-budget.json", result)
    # Keep this category in the common manifest even if the test could not start.
    collect_ci(evidence_root, "windows" if os.name == "nt" else "linux", "protocol", status.lower())
    return result, 0 if status == "PASS" else 1


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    collect = commands.add_parser("collect-ci", help="record one CI category")
    collect.add_argument("--evidence-root", required=True)
    collect.add_argument("--platform", required=True, choices=PLATFORMS)
    collect.add_argument("--category", required=True, choices=CATEGORIES)
    collect.add_argument("--status", required=True, choices=INPUT_STATUSES)
    budget = commands.add_parser("protocol-budget", help="run and record protocol packet limits")
    budget.add_argument("--evidence-root", required=True)
    budget.add_argument("--build-dir", default="build/default")
    budget.add_argument("--max-datagram-bytes", type=int, default=1200)
    return parser


def main(arguments: Iterable[str] | None = None) -> int:
    parsed = build_parser().parse_args(arguments)
    try:
        evidence_root = _safe_evidence_root(parsed.evidence_root)
        if parsed.command == "collect-ci":
            collect_ci(evidence_root, parsed.platform, parsed.category, parsed.status)
            return 0
        if parsed.max_datagram_bytes <= 0:
            raise ValueError("max datagram bytes must be positive")
        _, exit_code = protocol_budget(evidence_root, Path(parsed.build_dir).resolve(), parsed.max_datagram_bytes)
        return exit_code
    except ValueError as error:
        print("lg_verification: " + str(error), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
