#!/usr/bin/env python3
"""Build sealed review jobs for the Quaternius modular men pack."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[2]
RECIPES_ROOT = REPO_ROOT / "asset_pipeline" / "recipes"
POLICY_PATH = REPO_ROOT / "asset_pipeline" / "policy.json"
SAFE_NAME = re.compile(r"[A-Za-z0-9][A-Za-z0-9_-]{0,63}")


class BatchError(RuntimeError):
    pass


def _json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise BatchError(f"cannot read JSON file {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise BatchError(f"JSON file must contain an object: {path}")
    return value


def _write_json(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _repo_path(value: str, field: str) -> Path:
    if not isinstance(value, str) or not value:
        raise BatchError(f"{field} must be a non-empty path")
    path = (REPO_ROOT / value).resolve()
    try:
        path.relative_to(REPO_ROOT)
    except ValueError as exc:
        raise BatchError(f"{field} escapes the repository") from exc
    return path


def _checked_staged_input(path: Path, staging_root: Path) -> dict[str, Any]:
    if not path.is_file() or path.is_symlink():
        raise BatchError(f"staged input is missing or is not a regular file: {path}")
    try:
        relative = path.resolve().relative_to(staging_root.resolve())
    except ValueError as exc:
        raise BatchError(f"input is outside the checked staging root: {path}") from exc
    if len(relative.parts) < 3:
        raise BatchError(f"input does not belong to a staging package: {path}")
    package = staging_root / relative.parts[0]
    state = _json(package / "state.json")
    if state.get("stage") not in {"inspected", "processed", "validated", "imported"}:
        raise BatchError(f"input package has not passed inspection: {package.name}")
    provenance_path = package / "provenance.json"
    provenance = _json(provenance_path)
    snapshot = package / str(provenance.get("license_snapshot", ""))
    if not provenance.get("license_id") or not snapshot.is_file():
        raise BatchError(f"input package lacks license proof: {package.name}")
    if _sha256(snapshot) != provenance.get("license_snapshot_sha256"):
        raise BatchError(f"input package license proof changed: {package.name}")
    extracted = package / "work" / "extracted"
    inspect_root = extracted if extracted.exists() else package / "original"
    try:
        inspected_name = path.resolve().relative_to(inspect_root.resolve()).as_posix()
    except ValueError as exc:
        raise BatchError(f"input is outside its inspected tree: {path}") from exc
    digest = _sha256(path)
    inspection = _json(package / "reports" / "inspection.json")
    rows = inspection.get("files")
    if not isinstance(rows, list) or not any(
        isinstance(row, dict) and row.get("path") == inspected_name and row.get("sha256") == digest
        for row in rows
    ):
        raise BatchError(f"input does not match its inspection report: {path}")
    return {
        "path": str(path.resolve()),
        "sha256": digest,
        "source_package": package.name,
        "source_provenance_sha256": _sha256(provenance_path),
        "license_id": provenance["license_id"],
    }


def build_job(recipe_path: Path) -> tuple[dict[str, Any], dict[str, Any]]:
    recipe = _json(recipe_path.resolve())
    allowed = {"version", "profile", "character", "output_name"}
    unknown = sorted(set(recipe) - allowed)
    if unknown:
        raise BatchError("character recipe has unknown fields: " + ", ".join(unknown))
    if recipe.get("version") != 1:
        raise BatchError("character recipe version must be 1")
    profile_name = recipe.get("profile")
    if not isinstance(profile_name, str) or not SAFE_NAME.fullmatch(profile_name):
        raise BatchError("profile must be one safe file name")
    profile = _json(RECIPES_ROOT / f"{profile_name}.json")
    if profile.get("version") != 1 or profile.get("kind") != "character_batch_profile":
        raise BatchError("profile version or kind is not supported")
    character = recipe.get("character")
    output_name = recipe.get("output_name")
    if not isinstance(character, str) or not SAFE_NAME.fullmatch(character):
        raise BatchError("character must contain only letters, digits, '_' or '-'")
    if not isinstance(output_name, str) or not SAFE_NAME.fullmatch(output_name):
        raise BatchError("output_name must contain only letters, digits, '_' or '-'")
    template = profile.get("source_path_template")
    if not isinstance(template, str) or template.count("{character}") != 1:
        raise BatchError("profile source_path_template needs one {character} field")
    request = profile.get("request")
    if not isinstance(request, dict) or not isinstance(request.get("options"), dict):
        raise BatchError("profile request and request.options must be objects")
    # Copy through JSON so one job cannot change the shared settings in memory.
    job = json.loads(json.dumps(request))
    source = _repo_path(template.format(character=character), "source_path_template")
    reference = _repo_path(str(profile.get("rig_reference_path", "")), "rig_reference_path")
    animation = _repo_path(str(job["options"].get("animation_source_path", "")), "animation_source_path")
    policy = _json(POLICY_PATH)
    staging_root = _repo_path(str(policy.get("staging_root", "")), "policy.staging_root")
    bindings = [_checked_staged_input(path, staging_root) for path in (source, reference, animation)]
    bindings = sorted({item["path"]: item for item in bindings}.values(), key=lambda item: item["path"])
    budget = policy.get("budgets", {}).get(job.get("asset_type"))
    if not isinstance(budget, dict):
        raise BatchError("profile request names an unknown asset type")
    fingerprint_data = {
        "recipe": recipe,
        "profile": profile,
        "inputs": [{"path": item["path"], "sha256": item["sha256"]} for item in bindings],
        "tools": {
            name: _sha256(REPO_ROOT / "tools" / "asset_pipeline" / name)
            for name in ("check_modular_rig.py", "run_modular_job.py")
        },
    }
    fingerprint = hashlib.sha256(json.dumps(fingerprint_data, sort_keys=True).encode()).hexdigest()
    output_root = _repo_path(str(profile.get("output_root", "")), "profile.output_root")
    try:
        output_root.relative_to(staging_root)
    except ValueError as exc:
        raise BatchError("profile.output_root must stay under the staging root") from exc
    run_root = output_root / f"{output_name}-{fingerprint[:12]}"
    job.update({
        "input_path": str(source),
        "output_dir": str(run_root / "work" / "processed"),
        "output_name": output_name,
        "budgets": budget,
        "input_bindings": bindings,
        "rig_reference_path": str(reference),
        "rig_matrix_tolerance": float(profile.get("rig_matrix_tolerance", 0.0001)),
    })
    metadata = {
        "character": character,
        "profile": profile_name,
        "recipe": str(recipe_path.resolve()),
        "fingerprint": fingerprint,
        "run_root": str(run_root),
    }
    return job, metadata


def _run(command: list[str], timeout: int) -> subprocess.CompletedProcess[str]:
    allowed_env = {key: value for key, value in os.environ.items() if key.upper() in {"PATH", "SYSTEMROOT", "WINDIR", "TEMP", "TMP"}}
    allowed_env.update({"PYTHONHASHSEED": "0", "TZ": "UTC"})
    return subprocess.run(command, cwd=REPO_ROOT, env=allowed_env, text=True, encoding="utf-8",
                          errors="replace", capture_output=True, timeout=timeout, check=False, shell=False)


def run_recipe(recipe_path: Path, blender: Path, timeout: int, prepare_only: bool = False) -> dict[str, Any]:
    job, metadata = build_job(recipe_path)
    run_root = Path(metadata["run_root"])
    request_path = run_root / "work" / "process-request.json"
    reports = run_root / "reports"
    _write_json(request_path, job)
    _write_json(reports / "input-bindings.json", {"inputs": job["input_bindings"]})
    if prepare_only:
        report = {"status": "prepared", **metadata, "request": str(request_path)}
        _write_json(reports / "batch.json", report)
        return report
    blender = blender.resolve()
    if not blender.is_file() or blender.name.lower() not in {"blender", "blender.exe"}:
        raise BatchError("--blender must name a Blender executable")
    rig_result = reports / "rig-check.json"
    rig_command = [str(blender), "--background", "--factory-startup", "--disable-autoexec", "--python",
                   str((REPO_ROOT / "tools/asset_pipeline/check_modular_rig.py").resolve()), "--",
                   str(request_path), "--result", str(rig_result)]
    rig_process = _run(rig_command, timeout)
    _write_json(reports / "rig-check-process.json", {
        "command": rig_command, "returncode": rig_process.returncode,
        "stdout": rig_process.stdout, "stderr": rig_process.stderr,
    })
    rig_report = _json(rig_result) if rig_result.is_file() else {}
    if rig_process.returncode or rig_report.get("status") != "ok" or rig_report.get("compatible") is not True:
        raise BatchError("Worker rig check failed; see reports/rig-check.json")
    tool_result = reports / "process-tool.json"
    process_command = [str(blender), "--background", "--factory-startup", "--disable-autoexec", "--python",
                       str((REPO_ROOT / "tools/asset_pipeline/run_modular_job.py").resolve()), "--",
                       str(request_path), "--result", str(tool_result)]
    process = _run(process_command, timeout)
    _write_json(reports / "process.json", {
        "command": process_command, "returncode": process.returncode,
        "stdout": process.stdout, "stderr": process.stderr,
    })
    tool_report = _json(tool_result) if tool_result.is_file() else {}
    processing = tool_report.get("processing", {}) if isinstance(tool_report, dict) else {}
    transfer = processing.get("animation_transfer", {}) if isinstance(processing, dict) else {}
    required_ok = (
        process.returncode == 0 and tool_report.get("status") == "ok"
        and tool_report.get("input_bindings") == job["input_bindings"]
        and transfer.get("mode") == "retarget"
        and "weapon_socket" in processing.get("attachment_points", [])
        and isinstance(processing.get("two_handed_idle"), dict)
    )
    if not required_ok:
        raise BatchError("character processing did not confirm the shared animation and weapon setup")
    source = Path(job["input_path"])
    source_package = Path(job["input_bindings"][0]["path"])
    # Find the matching source binding instead of relying on sort order.
    source_binding = next(item for item in job["input_bindings"] if Path(item["path"]) == source)
    staging_root = _repo_path(str(_json(POLICY_PATH)["staging_root"]), "policy.staging_root")
    intake = staging_root / source_binding["source_package"]
    intake_provenance = _json(intake / "provenance.json")
    original = run_root / "original" / source.name
    original.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, original)
    license_source = intake / str(intake_provenance["license_snapshot"])
    license_copy = run_root / "license" / "LICENSE.txt"
    license_copy.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(license_source, license_copy)
    _write_json(reports / "source-provenance.json", intake_provenance)
    provenance = dict(intake_provenance)
    provenance.update({
        "asset_name": f"{metadata['character']} from Ultimate Modular Men Pack",
        "original_filename": source.name,
        "original_sha256": _sha256(original),
        "original_size": original.stat().st_size,
        "license_snapshot": "license/LICENSE.txt",
        "license_snapshot_sha256": _sha256(license_copy),
        "batch_profile": metadata["profile"],
        "batch_fingerprint": metadata["fingerprint"],
    })
    _write_json(run_root / "provenance.json", provenance)
    _write_json(run_root / "state.json", {"stage": "processed", "pipeline_version": 1})
    review_root = _repo_path(str(_json(POLICY_PATH)["review_import_root"]), "policy.review_import_root")
    review_destination = review_root / run_root.name
    report = {
        "status": "review_ready", **metadata,
        "rig_check": str(rig_result), "process_report": str(tool_result),
        "outputs": tool_report.get("outputs", {}), "visual_review_required": True,
        "review_import": str(review_destination),
    }
    _write_json(reports / "batch.json", report)
    summary = (
        f"# {metadata['character']} batch review\n\n"
        f"- Status: review ready; not approved for game use\n"
        f"- Worker rig and bind pose: passed\n"
        f"- Shared animations and two-handed idle: passed\n"
        f"- Weapon socket: passed\n"
        f"- Job fingerprint: `{metadata['fingerprint']}`\n"
        f"- Visual review: required\n"
    )
    (reports / "REVIEW.md").write_text(summary, encoding="utf-8")
    validator = _repo_path(str(_json(POLICY_PATH)["engine_validator"]), "policy.engine_validator")
    if os.name == "nt" and validator.suffix.lower() != ".exe":
        validator = validator.with_suffix(".exe")
    validate_command = [sys.executable, str(REPO_ROOT / "scripts/asset.py"), "validate", str(run_root),
                        "--asset-class", "player_model", "--engine-validator", str(validator)]
    validated = _run(validate_command, timeout)
    if validated.returncode:
        raise BatchError(f"Farmer validation failed: {validated.stderr.strip()}")
    import_command = [sys.executable, str(REPO_ROOT / "scripts/asset.py"), "import", str(run_root)]
    imported = _run(import_command, timeout)
    if imported.returncode:
        raise BatchError(f"Farmer review import failed: {imported.stderr.strip()}")
    if not review_destination.is_dir():
        raise BatchError("asset pipeline did not create the expected sealed review import")
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("recipe", type=Path)
    parser.add_argument("--blender", type=Path)
    parser.add_argument("--timeout", type=int, default=900)
    parser.add_argument("--prepare-only", action="store_true")
    args = parser.parse_args()
    try:
        if not args.prepare_only and args.blender is None:
            raise BatchError("--blender is required unless --prepare-only is set")
        report = run_recipe(args.recipe, args.blender or Path("blender"), args.timeout, args.prepare_only)
        print(json.dumps(report, indent=2, sort_keys=True))
        return 0
    except (BatchError, OSError, subprocess.TimeoutExpired, ValueError) as exc:
        print(f"batch failed: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
