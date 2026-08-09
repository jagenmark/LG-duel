#!/usr/bin/env python3
"""Validate and upload one image to the private LG Duel evidence gallery."""

from __future__ import annotations

import argparse
import hashlib
import json
import mimetypes
import os
import re
import secrets
import sys
import urllib.error
import urllib.request
from datetime import datetime, timezone
from io import BytesIO
from pathlib import Path
from typing import Any

from PIL import Image, ImageOps


ROOT = Path(__file__).resolve().parent.parent
CONFIG_PATH = ROOT / "config" / "visual-evidence-gallery.json"
SITES_TOKEN_ENV = "LG_VISUAL_EVIDENCE_SITES_TOKEN"
SITES_TOKEN_PATH = Path.home() / ".codex" / "secrets" / "lg-duel-visual-evidence-sites-token"
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
    checked["retain_original"] = bool(record.get("retain_original")) or review_status == "pass"
    if review is not None:
        checked["review"] = dict(review)
    return checked, image_path, config


def _secret_token(environment_name: str, path: Path, label: str) -> str:
    token = os.environ.get(environment_name, "").strip()
    if token:
        return token
    try:
        token = path.read_text(encoding="utf-8").strip()
    except OSError as error:
        raise ValidationError(
            f"{label} is missing; set {environment_name} or configure {path}"
        ) from error
    if not token:
        raise ValidationError(f"{label} file is empty: {path}")
    return token


def sites_token() -> str:
    return _secret_token(SITES_TOKEN_ENV, SITES_TOKEN_PATH, "private Sites token")


def make_review_image(image_path: Path) -> tuple[bytes, int, int]:
    try:
        with Image.open(image_path) as opened:
            image = ImageOps.exif_transpose(opened)
            image.thumbnail((1600, 1600), Image.Resampling.LANCZOS)
            if image.mode not in {"RGB", "RGBA"}:
                image = image.convert("RGBA" if "transparency" in image.info else "RGB")
            output = BytesIO()
            image.save(output, format="WEBP", quality=82, method=6)
            review = output.getvalue()
            width, height = image.size
    except (OSError, ValueError) as error:
        raise ValidationError(f"cannot create compact review image: {error}") from error
    if not review or len(review) > 2 * 1024 * 1024:
        raise ValidationError("compact review image must be 1..2097152 bytes")
    return review, width, height


def _multipart_body(
    metadata: dict[str, Any],
    review_bytes: bytes,
    image_path: Path,
) -> tuple[bytes, str]:
    boundary = "lgduel-" + secrets.token_hex(16)
    metadata_bytes = json.dumps(metadata, sort_keys=True).encode("utf-8")
    chunks = [
        f"--{boundary}\r\n".encode(),
        b'Content-Disposition: form-data; name="metadata"\r\n',
        b"Content-Type: application/json; charset=utf-8\r\n\r\n",
        metadata_bytes,
        b"\r\n",
        f"--{boundary}\r\n".encode(),
        (
            'Content-Disposition: form-data; name="review"; '
            f'filename="{metadata["capture_id"]}-review.webp"\r\n'
        ).encode(),
        b"Content-Type: image/webp\r\n\r\n",
        review_bytes,
        b"\r\n",
    ]
    if metadata["retain_original"]:
        chunks.extend([
            f"--{boundary}\r\n".encode(),
            (
                'Content-Disposition: form-data; name="original"; '
                f'filename="{image_path.name}"\r\n'
            ).encode(),
            f"Content-Type: {metadata['content_type']}\r\n\r\n".encode(),
            image_path.read_bytes(),
            b"\r\n",
        ])
    chunks.append(f"--{boundary}--\r\n".encode())
    return b"".join(chunks), boundary


def _open(request: urllib.request.Request):
    return urllib.request.urlopen(request, timeout=90)


def upload_capture(
    metadata: dict[str, Any],
    image_path: Path,
    config: dict[str, Any],
    private_sites_token: str,
) -> dict[str, Any]:
    origin = config.get("gallery_origin")
    if not isinstance(origin, str) or not origin.startswith("https://"):
        raise ValidationError("gallery config must contain an https gallery_origin")
    upload_path = config.get("upload_path", "/api/evidence")
    if not isinstance(upload_path, str) or not upload_path.startswith("/"):
        raise ValidationError("gallery config upload_path must be an absolute path")
    review_bytes, review_width, review_height = make_review_image(image_path)
    upload_metadata = dict(metadata)
    upload_metadata["review_sha256"] = hashlib.sha256(review_bytes).hexdigest()
    upload_metadata["review_width"] = review_width
    upload_metadata["review_height"] = review_height
    body, boundary = _multipart_body(upload_metadata, review_bytes, image_path)
    request = urllib.request.Request(
        origin.rstrip("/") + upload_path,
        data=body,
        method="POST",
        headers={
            "OAI-Sites-Authorization": f"Bearer {private_sites_token}",
            "Content-Type": f"multipart/form-data; boundary={boundary}",
            "Accept": "application/json",
        },
    )
    try:
        with _open(request) as response:
            result = json.loads(response.read().decode("utf-8"))
    except urllib.error.HTTPError as error:
        detail = error.read().decode("utf-8", errors="replace")
        try:
            message = json.loads(detail).get("error", detail)
        except json.JSONDecodeError:
            message = detail
        raise ValidationError(f"gallery upload failed ({error.code}): {message}") from error
    except (OSError, json.JSONDecodeError) as error:
        raise ValidationError(f"gallery upload failed: {error}") from error
    if not isinstance(result, dict) or not isinstance(result.get("capture"), dict):
        raise ValidationError("gallery returned an invalid upload response")
    return result


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Check and upload an image to the private Sites gallery."
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
            result = upload_capture(checked, image_path, config, sites_token())
            capture = result["capture"]
            origin = str(config["gallery_origin"]).rstrip("/")
            result = {
                "status": result.get("status", "uploaded"),
                "capture_id": checked["capture_id"],
                "preview_url": origin + str(capture["preview_url"]),
                "full_size_url": origin + str(capture["full_size_url"]),
                "original_url": (
                    origin + str(capture["original_url"])
                    if capture.get("original_url") else None
                ),
                "review_status": capture["review_status"],
            }
        print(json.dumps(result, indent=2, sort_keys=True))
        return 0
    except ValidationError as error:
        print(f"publication blocked: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
