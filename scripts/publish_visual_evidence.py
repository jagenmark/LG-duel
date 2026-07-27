#!/usr/bin/env python3
"""Safely stage one image for the private LG Duel Sites gallery."""

from __future__ import annotations

import argparse
import hashlib
import json
import mimetypes
import os
import re
import shutil
import sys
import tempfile
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parent.parent
CONFIG_PATH = ROOT / "config" / "visual-evidence-gallery.json"
STAGE_ROOT = ROOT / "deploy" / "visual-evidence-gallery" / "public" / "evidence"
CAPTURE_ID = re.compile(r"^\d{8}T\d{6}Z-[a-z0-9]+(?:-[a-z0-9]+)*-\d{2}$")
TASK_ID = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{1,79}$")
SHA256 = re.compile(r"^[0-9a-f]{64}$")
ALLOWED_SUFFIXES = {".png", ".jpg", ".jpeg", ".webp"}
ALLOWED_TYPES = {"image/png", "image/jpeg", "image/webp"}
REVIEW_VERDICTS = {"pass", "fail", "needs_changes"}


class ValidationError(ValueError):
    """An image or record is not safe to stage."""


def _read_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except OSError as error:
        raise ValidationError(f"cannot read {path}: {error}") from error
    except json.JSONDecodeError as error:
        raise ValidationError(f"invalid JSON in {path}: {error}") from error
    if not isinstance(value, dict):
        raise ValidationError("metadata must be a JSON object")
    return value


def _required_text(record: dict[str, Any], key: str, where: str = "metadata") -> str:
    value = record.get(key)
    if not isinstance(value, str) or not value.strip():
        raise ValidationError(f"{where}.{key} must be non-empty text")
    return value.strip()


def _utc_timestamp(value: str, field: str) -> str:
    try:
        parsed = datetime.fromisoformat(value.replace("Z", "+00:00"))
    except ValueError as error:
        raise ValidationError(f"{field} must be an ISO 8601 time") from error
    if parsed.tzinfo is None or parsed.utcoffset() != timezone.utc.utcoffset(parsed):
        raise ValidationError(f"{field} must use UTC")
    return parsed.isoformat().replace("+00:00", "Z")


def _atomic_json(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(
        "w", encoding="utf-8", dir=path.parent, prefix="." + path.name + ".", delete=False
    ) as handle:
        handle.write(json.dumps(value, indent=2, sort_keys=True) + "\n")
        temporary = Path(handle.name)
    os.replace(temporary, path)


def _atomic_copy(source: Path, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(
        "wb", dir=destination.parent, prefix="." + destination.name + ".", delete=False
    ) as handle:
        with source.open("rb") as source_handle:
            shutil.copyfileobj(source_handle, handle)
        temporary = Path(handle.name)
    os.replace(temporary, destination)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def validate_metadata(
    metadata_path: Path, config_path: Path = CONFIG_PATH
) -> tuple[dict[str, Any], Path, dict[str, Any]]:
    record = _read_json(metadata_path)
    config = _read_json(config_path)
    if record.get("schema_version") != 1:
        raise ValidationError("metadata.schema_version must be 1")
    task_id = _required_text(record, "task_id")
    capture_id = _required_text(record, "capture_id")
    captured_by = _required_text(record, "captured_by")
    _required_text(record, "title")
    _required_text(record, "description")
    if not TASK_ID.fullmatch(task_id):
        raise ValidationError("metadata.task_id has an unsafe format")
    if not CAPTURE_ID.fullmatch(capture_id):
        raise ValidationError("metadata.capture_id must match YYYYMMDDTHHMMSSZ-<words>-NN")
    record["captured_at"] = _utc_timestamp(
        _required_text(record, "captured_at"), "metadata.captured_at"
    )
    if record.get("contains_sensitive_data") is not False:
        raise ValidationError("metadata.contains_sensitive_data must be false")

    image_value = _required_text(record, "image")
    image_path = (metadata_path.parent / image_value).resolve()
    try:
        image_path.relative_to(ROOT.resolve())
    except ValueError as error:
        raise ValidationError("metadata.image must stay inside the project") from error
    if not image_path.is_file():
        raise ValidationError(f"capture image does not exist: {image_path}")
    suffix = image_path.suffix.lower()
    if suffix not in ALLOWED_SUFFIXES:
        raise ValidationError("capture image must be PNG, JPEG, or WebP")
    mime_type = mimetypes.types_map.get(suffix)
    if mime_type not in ALLOWED_TYPES:
        raise ValidationError("capture image type is not allowed")
    max_bytes = config.get("max_bytes")
    if not isinstance(max_bytes, int) or max_bytes <= 0:
        raise ValidationError("gallery config max_bytes must be a positive integer")
    size_bytes = image_path.stat().st_size
    if size_bytes == 0 or size_bytes > max_bytes:
        raise ValidationError(f"capture image must be 1..{max_bytes} bytes")

    review_status = "not_reviewed"
    review = record.get("review")
    if review is not None:
        if not isinstance(review, dict):
            raise ValidationError("metadata.review must be an object when present")
        reviewer = _required_text(review, "reviewer", "metadata.review")
        verdict = _required_text(review, "verdict", "metadata.review").lower()
        _required_text(review, "notes", "metadata.review")
        review["reviewed_at"] = _utc_timestamp(
            _required_text(review, "reviewed_at", "metadata.review"),
            "metadata.review.reviewed_at",
        )
        if verdict not in REVIEW_VERDICTS:
            raise ValidationError("metadata.review.verdict is not valid")
        if verdict == "pass" and reviewer.casefold() == captured_by.casefold():
            raise ValidationError("a passing evidence review must be independent")
        review["verdict"] = verdict
        review_status = verdict

    digest = sha256_file(image_path)
    supplied_digest = record.get("sha256")
    if supplied_digest is not None:
        if not isinstance(supplied_digest, str) or not SHA256.fullmatch(supplied_digest):
            raise ValidationError("metadata.sha256 must be a lower-case SHA-256 value")
        if supplied_digest != digest:
            raise ValidationError("metadata.sha256 does not match the image")

    checked = dict(record)
    checked["image"] = image_path.name
    checked["sha256"] = digest
    checked["size_bytes"] = size_bytes
    checked["content_type"] = mime_type
    checked["review_status"] = review_status
    if review is not None:
        checked["review"] = dict(review)
    return checked, image_path, config


def stage_capture(
    metadata: dict[str, Any],
    image_path: Path,
    stage_root: Path = STAGE_ROOT,
) -> dict[str, Any]:
    task_id = metadata["task_id"]
    capture_id = metadata["capture_id"]
    asset_path = stage_root / "assets" / task_id / f"{capture_id}{image_path.suffix.lower()}"
    record_path = stage_root / "records" / task_id / f"{capture_id}.json"
    manifest_path = stage_root / "manifest.json"
    if asset_path.exists() and sha256_file(asset_path) != metadata["sha256"]:
        raise ValidationError("the staged capture id already has different image bytes")
    if record_path.exists():
        prior = _read_json(record_path)
        if prior.get("sha256") != metadata["sha256"]:
            raise ValidationError("the staged capture id already has different metadata")

    _atomic_copy(image_path, asset_path)
    _atomic_json(record_path, metadata)
    manifest = _read_json(manifest_path) if manifest_path.exists() else {
        "schema_version": 1,
        "captures": [],
    }
    if manifest.get("schema_version") != 1 or not isinstance(manifest.get("captures"), list):
        raise ValidationError("staged gallery manifest is invalid")
    relative_asset = "/" + asset_path.relative_to(stage_root.parent).as_posix()
    relative_record = "/" + record_path.relative_to(stage_root.parent).as_posix()
    review = metadata.get("review") if isinstance(metadata.get("review"), dict) else {}
    entry = {
        "capture_id": capture_id,
        "task_id": task_id,
        "title": metadata["title"],
        "description": metadata["description"],
        "captured_at": metadata["captured_at"],
        "captured_by": metadata["captured_by"],
        "review_status": metadata["review_status"],
        "reviewer": review.get("reviewer"),
        "reviewed_at": review.get("reviewed_at"),
        "review_notes": review.get("notes"),
        "sha256": metadata["sha256"],
        "size_bytes": metadata["size_bytes"],
        "preview_url": relative_asset,
        "full_size_url": relative_asset,
        "record_url": relative_record,
    }
    captures = [
        item for item in manifest["captures"]
        if isinstance(item, dict) and item.get("capture_id") != capture_id
    ]
    captures.append(entry)
    captures.sort(
        key=lambda item: (str(item.get("captured_at", "")), str(item.get("capture_id", ""))),
        reverse=True,
    )
    _atomic_json(manifest_path, {"schema_version": 1, "captures": captures})
    return entry


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Check and stage an image for the private Sites gallery."
    )
    parser.add_argument("metadata", type=Path)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--config", type=Path, default=CONFIG_PATH)
    args = parser.parse_args(argv)
    try:
        checked, image_path, config = validate_metadata(
            args.metadata.resolve(), args.config.resolve()
        )
        if args.dry_run:
            result = {
                "status": "valid",
                "capture_id": checked["capture_id"],
                "image": str(image_path),
                "review_status": checked["review_status"],
                "sha256": checked["sha256"],
                "size_bytes": checked["size_bytes"],
            }
        else:
            entry = stage_capture(checked, image_path)
            origin = config.get("gallery_origin")
            if isinstance(origin, str) and origin.startswith("https://"):
                preview = origin.rstrip("/") + entry["preview_url"]
                full_size = origin.rstrip("/") + entry["full_size_url"]
            else:
                preview = entry["preview_url"]
                full_size = entry["full_size_url"]
            result = {
                "status": "staged_for_private_publish",
                "capture_id": checked["capture_id"],
                "preview_url": preview,
                "full_size_url": full_size,
                "review_status": entry["review_status"],
            }
        print(json.dumps(result, indent=2, sort_keys=True))
        return 0
    except ValidationError as error:
        print(f"publication blocked: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
