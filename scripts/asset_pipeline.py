#!/usr/bin/env python3
"""Safe, review-first intake for third-party game assets.

This tool never writes to production asset folders. Each command advances a
package in the repo-local staging tree, and import copies a validated package
to the review import tree for a person to approve.
"""

from __future__ import annotations

import argparse
import base64
import dataclasses
import datetime as dt
import hashlib
import io
import json
import os
import pathlib
import re
import shutil
import stat
import struct
import subprocess
import sys
import tarfile
import tempfile
import urllib.parse
import urllib.request
import wave
import zipfile
from collections.abc import Callable, Iterable, Mapping, Sequence
from typing import Any, Protocol


PIPELINE_VERSION = 1
REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
DEFAULT_POLICY = REPO_ROOT / "asset_pipeline" / "policy.json"
EXECUTABLE_SUFFIXES = {
    ".app", ".bat", ".cmd", ".com", ".dll", ".dylib", ".exe", ".hta",
    ".jar", ".js", ".lnk", ".msi", ".ps1", ".py", ".scr", ".sh", ".so",
    ".vbs",
}
PROHIBITED_LICENSE_WORDS = (
    "all rights reserved", "custom", "editorial", "non-commercial",
    "noncommercial", "nc only", "personal use", "unknown", "uncertain",
)
STAGE_ORDER = {"downloaded": 1, "inspected": 2, "processed": 3, "validated": 4}
REVIEW_MANIFEST = "reports/review-manifest.json"


class PipelineError(RuntimeError):
    """A user-facing safety or validation failure."""


def utc_now() -> str:
    return dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def canonical_json(value: Any) -> str:
    return json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n"


def read_json(path: pathlib.Path) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise PipelineError(f"cannot read JSON {path}: {exc}") from exc


def write_json(path: pathlib.Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(canonical_json(value), encoding="utf-8", newline="\n")


def sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def safe_name(value: str, fallback: str = "asset") -> str:
    cleaned = re.sub(r"[^A-Za-z0-9._-]+", "-", value).strip(".-")
    return cleaned[:96] or fallback


def contained_path(root: pathlib.Path, value: str | pathlib.Path) -> pathlib.Path:
    """Resolve a path and require it to remain below root.

    Path checks happen before every pipeline write. Do not weaken this check:
    source names, asset IDs, archive names, and report paths are untrusted.
    """
    base = root.resolve()
    candidate = (base / value).resolve() if not pathlib.Path(value).is_absolute() else pathlib.Path(value).resolve()
    try:
        candidate.relative_to(base)
    except ValueError as exc:
        raise PipelineError(f"path escapes allowed root: {value}") from exc
    return candidate


def _policy_path(policy: Mapping[str, Any], key: str, default: str) -> pathlib.Path:
    raw = policy.get(key, default)
    if not isinstance(raw, str) or not raw.strip():
        raise PipelineError(f"policy {key} must be a non-empty relative path")
    if pathlib.Path(raw).is_absolute():
        raise PipelineError(f"policy {key} must stay inside the repository")
    return contained_path(REPO_ROOT, raw)


def load_policy(path: pathlib.Path = DEFAULT_POLICY) -> dict[str, Any]:
    value = read_json(path.resolve())
    if not isinstance(value, dict):
        raise PipelineError("policy root must be an object")
    required = ("version", "staging_root", "review_import_root", "engine_validator", "blender_version_prefix",
                "blender_script", "validation_manifests",
                "approved_sources", "licenses", "budgets", "formats", "archive_limits")
    missing = [key for key in required if key not in value]
    if missing:
        raise PipelineError("policy lacks required keys: " + ", ".join(missing))
    if not isinstance(value["approved_sources"], list):
        raise PipelineError("policy approved_sources must be an array")
    if not isinstance(value["licenses"], dict) or not isinstance(value["licenses"].get("permitted"), dict):
        raise PipelineError("policy licenses.permitted must be an object keyed by exact license ID")
    _policy_path(value, "staging_root", "asset_pipeline/staging")
    _policy_path(value, "review_import_root", "asset_pipeline/review_imports")
    return value


def source_policy(policy: Mapping[str, Any], name: str) -> dict[str, Any]:
    for item in policy.get("approved_sources", []):
        if isinstance(item, str) and item == name:
            return {"name": item, "domains": []}
        if isinstance(item, dict) and item.get("name") == name and item.get("enabled", True):
            return dict(item)
    raise PipelineError(f"source is not approved by policy: {name}")


def _host_allowed(url: str, source: Mapping[str, Any]) -> bool:
    domains = source.get("domains", [])
    if not domains:
        return False
    host = (urllib.parse.urlparse(url).hostname or "").lower().rstrip(".")
    return any(host == str(domain).lower().lstrip("*.") or host.endswith("." + str(domain).lower().lstrip("*.")) for domain in domains)


def require_candidate(candidate: Mapping[str, Any], policy: Mapping[str, Any]) -> dict[str, Any]:
    required = ("source", "id", "name", "page_url", "download_url", "author")
    missing = [key for key in required if not isinstance(candidate.get(key), str) or not str(candidate[key]).strip()]
    if missing:
        raise PipelineError("candidate lacks required fields: " + ", ".join(missing))
    approved = source_policy(policy, str(candidate["source"]))
    local_only = bool(approved.get("local_only", False))
    for key in ("page_url", "download_url"):
        parsed = urllib.parse.urlparse(str(candidate[key]))
        if local_only and not parsed.scheme and not parsed.netloc:
            base = pathlib.Path(str(candidate.get("_candidate_base", REPO_ROOT)))
            contained_path(base, urllib.parse.unquote(parsed.path))
        else:
            if parsed.scheme not in {"https", "http"} or not parsed.netloc:
                raise PipelineError(f"candidate {key} must be an HTTP(S) URL")
            if not _host_allowed(str(candidate[key]), approved):
                raise PipelineError(f"candidate {key} is outside approved source domains")
    return dict(candidate)


def _license_fields(evidence: Mapping[str, Any] | None) -> tuple[str, str, str]:
    evidence = evidence or {}
    license_id = evidence.get("license_id", "")
    license_url = evidence.get("license_url", "")
    license_text = evidence.get("license_text", "")
    return str(license_id).strip(), str(license_url).strip(), str(license_text).strip()


def load_license_evidence(path: pathlib.Path) -> dict[str, Any]:
    if path.absolute().is_symlink():
        raise PipelineError("license evidence must be a regular JSON file")
    path = path.resolve()
    if not path.is_file():
        raise PipelineError("license evidence must be a regular JSON file")
    value = read_json(path)
    if not isinstance(value, dict):
        raise PipelineError("license evidence must be a JSON object")
    result = dict(value)
    reference = result.get("evidence_path")
    if reference is None:
        text_value = result.get("license_text")
        # A short single-line value with a known text suffix is a snapshot link,
        # not the license itself. Resolve it next to the evidence JSON only.
        if isinstance(text_value, str) and "\n" not in text_value and pathlib.Path(text_value).suffix.lower() in {".txt", ".md", ".license"}:
            reference = text_value
    if reference is not None:
        if not isinstance(reference, str) or not reference or pathlib.Path(reference).is_absolute():
            raise PipelineError("evidence_path must be a non-empty relative path")
        unresolved_file = path.parent / reference
        if unresolved_file.is_symlink():
            raise PipelineError(f"license snapshot must be a regular file: {reference}")
        evidence_file = contained_path(path.parent, reference)
        if not evidence_file.is_file():
            raise PipelineError(f"license snapshot must be a regular file: {reference}")
        try:
            result["license_text"] = evidence_file.read_text(encoding="utf-8")
        except (OSError, UnicodeError) as exc:
            raise PipelineError(f"cannot read license snapshot: {exc}") from exc
        result["evidence_path"] = pathlib.Path(reference).as_posix()
        result["evidence_sha256"] = sha256_file(evidence_file)
        result["_evidence_kind"] = "snapshot"
    elif isinstance(result.get("license_text"), str) and str(result["license_text"]).strip():
        result["_evidence_kind"] = "literal"
    result["_evidence_file_name"] = path.name
    result["_evidence_file_sha256"] = sha256_file(path)
    result["_evidence_file_path"] = str(path)
    return result


def inspect_license(candidate: Mapping[str, Any], policy: Mapping[str, Any], evidence: Mapping[str, Any] | None = None) -> dict[str, Any]:
    """Return a fail-closed license decision with exact saved evidence."""
    reasons: list[str] = []
    try:
        require_candidate(candidate, policy)
    except PipelineError as exc:
        reasons.append(str(exc))
    license_id, license_url, license_text = _license_fields(evidence)
    permitted = policy.get("licenses", {}).get("permitted", {})
    review = {str(item).lower() for item in policy.get("licenses", {}).get("review_required", [])}
    prohibited = {str(item).lower() for item in policy.get("licenses", {}).get("prohibited", [])}
    if evidence is None or not evidence.get("_evidence_file_sha256"):
        reasons.append("an explicit independent license evidence file is required")
    if not license_id or not license_url or not license_text:
        reasons.append("exact license ID, URL, and text are all required")
    if evidence is not None and evidence.get("_evidence_kind") not in {"literal", "snapshot"}:
        reasons.append("license evidence needs literal text or a safe snapshot path")
    if license_id not in permitted:
        reasons.append(f"license is not in the permitted exact-ID map: {license_id or '<missing>'}")
    elif isinstance(permitted[license_id], dict):
        terms = permitted[license_id]
        if not terms.get("commercial_use", False) or not terms.get("modification", False):
            reasons.append("policy does not grant both commercial use and modification for this license")
        if terms.get("attribution", False):
            attribution = candidate.get("attribution_text")
            if not isinstance(attribution, str) or not attribution.strip():
                reasons.append("this license requires exact attribution text")
    declared_license = candidate.get("license", {})
    declared_text = declared_license.get("text", "") if isinstance(declared_license, dict) else candidate.get("license_text", "")
    restrictions = (evidence or {}).get("restrictions", "")
    # Scan source claims and structured limits, not the full legal code. Legal
    # code can use words such as "unknown" while waiving unknown future claims.
    evidence_claim = license_text if (evidence or {}).get("_evidence_kind") == "literal" else ""
    lowered = f"{license_id} {declared_text} {restrictions} {evidence_claim}".lower()
    if license_id.lower() in review or license_id.lower() in prohibited:
        reasons.append("license policy requires rejection or manual legal review")
    if any(word in lowered for word in PROHIBITED_LICENSE_WORDS):
        reasons.append("license evidence contains restricted, custom, editorial, noncommercial, or uncertain terms")
    declared = candidate.get("license_ids")
    if isinstance(declared, list) and len({str(item) for item in declared}) != 1:
        reasons.append("candidate has conflicting license IDs")
    if evidence and evidence.get("license_id") and nested_license_id(candidate) and str(evidence["license_id"]) != nested_license_id(candidate):
        reasons.append("license evidence conflicts with candidate metadata")
    declared_url = nested_license_url(candidate)
    if evidence and evidence.get("license_url") and declared_url and str(evidence["license_url"]).strip() != declared_url:
        reasons.append("license evidence URL conflicts with candidate metadata")
    parsed = urllib.parse.urlparse(license_url)
    if license_url and (parsed.scheme not in {"https", "http"} or not parsed.netloc):
        reasons.append("license evidence URL must be HTTP(S)")
    return {
        "approved": not reasons,
        "license_id": license_id,
        "license_url": license_url,
        "license_text": license_text,
        "reasons": sorted(set(reasons)),
    }


def nested_license_id(candidate: Mapping[str, Any]) -> str:
    nested = candidate.get("license", {})
    return str(nested.get("id", candidate.get("license_id", ""))).strip() if isinstance(nested, dict) else str(candidate.get("license_id", "")).strip()


def nested_license_url(candidate: Mapping[str, Any]) -> str:
    nested = candidate.get("license", {})
    return str(nested.get("url", candidate.get("license_url", ""))).strip() if isinstance(nested, dict) else str(candidate.get("license_url", "")).strip()


class SearchProvider(Protocol):
    name: str

    def search(self, query: str) -> list[dict[str, Any]]: ...


@dataclasses.dataclass(frozen=True)
class CatalogProvider:
    name: str
    catalog: Mapping[str, Any]

    def search(self, query: str) -> list[dict[str, Any]]:
        words = query.lower().split()
        rows = self.catalog.get("assets", self.catalog.get("items", []))
        if not isinstance(rows, list):
            raise PipelineError(f"catalog for {self.name} has no assets array")
        found = []
        for row in rows:
            if not isinstance(row, dict):
                continue
            item = dict(row)
            item.setdefault("source", self.name)
            text = " ".join(str(item.get(key, "")) for key in ("id", "name", "description", "tags")).lower()
            if all(word in text for word in words):
                found.append(item)
        return sorted(found, key=lambda item: (str(item.get("source", "")), str(item.get("id", ""))))


def load_catalog(location: str, *, timeout: int = 20,
                 approved_source: Mapping[str, Any] | None = None) -> Mapping[str, Any]:
    parsed = urllib.parse.urlparse(location)
    if parsed.scheme in {"http", "https"}:
        if approved_source is None or not _host_allowed(location, approved_source):
            raise PipelineError("catalog URL is outside approved source domains")
        request = urllib.request.Request(location, headers={"User-Agent": "LG-Duel-Asset-Pipeline/1"})
        opener = urllib.request.build_opener(_ApprovedRedirectHandler(approved_source))
        with opener.open(request, timeout=timeout) as response:
            if not _host_allowed(response.geturl(), approved_source):
                raise PipelineError("catalog response is outside approved source domains")
            data = response.read(8 * 1024 * 1024 + 1)
        if len(data) > 8 * 1024 * 1024:
            raise PipelineError("catalog exceeds 8 MiB")
        try:
            return json.loads(data.decode("utf-8"))
        except (UnicodeError, json.JSONDecodeError) as exc:
            raise PipelineError(f"invalid catalog JSON: {exc}") from exc
    return read_json(pathlib.Path(location))


def search_catalogs(query: str, policy: Mapping[str, Any], source: str | None = None,
                    explicit_catalog: pathlib.Path | None = None) -> list[dict[str, Any]]:
    output: list[dict[str, Any]] = []
    for raw in policy.get("approved_sources", []):
        item = {"name": raw, "search_catalogs": []} if isinstance(raw, str) else raw
        if not isinstance(item, dict) or not item.get("enabled", True) or (source and item.get("name") != source):
            continue
        name = str(item.get("name", ""))
        catalogs = [str(explicit_catalog)] if explicit_catalog is not None and item.get("name") == source else item.get("search_catalogs", [])
        if isinstance(catalogs, str):
            catalogs = [catalogs]
        for location in catalogs:
            location = str(location)
            if urllib.parse.urlparse(location).scheme not in {"http", "https"}:
                location = str(contained_path(REPO_ROOT, location))
            provider = CatalogProvider(name, load_catalog(location, approved_source=item))
            for candidate in provider.search(query):
                if urllib.parse.urlparse(location).scheme not in {"http", "https"}:
                    candidate["_candidate_base"] = str(pathlib.Path(location).resolve().parent)
                try:
                    require_candidate(candidate, policy)
                except PipelineError:
                    continue
                output.append(candidate)
    return sorted(output, key=lambda item: (str(item["source"]), str(item["id"]), str(item["download_url"])))


def package_path(candidate: Mapping[str, Any], policy: Mapping[str, Any]) -> pathlib.Path:
    root = _policy_path(policy, "staging_root", "asset_pipeline/staging")
    return contained_path(root, f"{safe_name(str(candidate['source']))}-{safe_name(str(candidate['id']))}")


class _ApprovedRedirectHandler(urllib.request.HTTPRedirectHandler):
    """Keep every HTTP redirect on a host approved for this source."""

    def __init__(self, source: Mapping[str, Any]):
        super().__init__()
        self.source = source

    def redirect_request(self, req: urllib.request.Request, fp: Any, code: int, msg: str,
                         headers: Any, newurl: str) -> urllib.request.Request | None:
        target = urllib.parse.urljoin(req.full_url, newurl)
        parsed = urllib.parse.urlparse(target)
        if parsed.scheme not in {"http", "https"} or not parsed.netloc or not _host_allowed(target, self.source):
            raise PipelineError("download redirect is outside approved source domains")
        return super().redirect_request(req, fp, code, msg, headers, target)


def _default_fetch(url: str, destination: pathlib.Path, max_bytes: int,
                   approved_source: Mapping[str, Any] | None = None) -> None:
    if approved_source is None:
        raise PipelineError("download source policy is required")
    request = urllib.request.Request(url, headers={"User-Agent": "LG-Duel-Asset-Pipeline/1"})
    opener = urllib.request.build_opener(_ApprovedRedirectHandler(approved_source))
    with opener.open(request, timeout=60) as response, destination.open("wb") as output:
        final_url = response.geturl()
        if not _host_allowed(final_url, approved_source):
            raise PipelineError("download response is outside approved source domains")
        declared = response.headers.get("Content-Length")
        if declared and int(declared) > max_bytes:
            raise PipelineError("download exceeds policy size limit")
        total = 0
        while chunk := response.read(min(1024 * 1024, max_bytes + 1 - total)):
            total += len(chunk)
            if total > max_bytes:
                raise PipelineError("download exceeds policy size limit")
            output.write(chunk)


def stage_download(candidate: Mapping[str, Any], policy: Mapping[str, Any], evidence: Mapping[str, Any] | None = None,
                   fetch: Callable[[str, pathlib.Path, int], None] = _default_fetch,
                   local_source: pathlib.Path | None = None) -> pathlib.Path:
    candidate = require_candidate(candidate, policy)
    decision = inspect_license(candidate, policy, evidence)
    if not decision["approved"]:
        raise PipelineError("license rejected: " + "; ".join(decision["reasons"]))
    package = package_path(candidate, policy)
    if package.exists():
        raise PipelineError(f"staging package already exists: {package}")
    package.mkdir(parents=True)
    try:
        original = package / "original"
        original.mkdir()
        parsed = urllib.parse.urlparse(candidate["download_url"])
        declared_filename = candidate.get("download_filename")
        if declared_filename is not None:
            if not isinstance(declared_filename, str) or pathlib.PurePath(declared_filename).name != declared_filename:
                raise PipelineError("download_filename must be one plain file name")
            filename = safe_name(declared_filename, "download.bin")
        else:
            filename = safe_name(pathlib.PurePosixPath(parsed.path).name, "download.bin")
        target = contained_path(original, filename)
        max_bytes = int(policy.get("archive_limits", {}).get("max_download_bytes", 1024 * 1024 * 1024))
        download_url = str(candidate["download_url"])
        acquisition_method = "network"
        if local_source is not None:
            local_source = local_source.resolve()
            if not local_source.is_file() or local_source.is_symlink():
                raise PipelineError("local source must be a regular file")
            if local_source.stat().st_size > max_bytes:
                raise PipelineError("local download exceeds policy size limit")
            shutil.copyfile(local_source, target)
            acquisition_method = "local_override"
        elif not urllib.parse.urlparse(download_url).scheme:
            approved = source_policy(policy, str(candidate["source"]))
            if not approved.get("local_only", False):
                raise PipelineError("relative downloads require a local_only approved source")
            source_file = contained_path(pathlib.Path(str(candidate.get("_candidate_base", REPO_ROOT))), urllib.parse.unquote(download_url))
            if not source_file.is_file() or source_file.is_symlink():
                raise PipelineError("local sample download must be a regular file")
            if source_file.stat().st_size > max_bytes:
                raise PipelineError("download exceeds policy size limit")
            shutil.copyfile(source_file, target)
            acquisition_method = "local_catalog"
        else:
            if fetch is _default_fetch:
                fetch(download_url, target, max_bytes, source_policy(policy, str(candidate["source"])))  # type: ignore[call-arg]
            else:
                fetch(download_url, target, max_bytes)
        if not target.is_file():
            raise PipelineError("download did not create the requested file")
        digest = sha256_file(target)
        license_dir = package / "license"
        license_dir.mkdir()
        snapshot = license_dir / "LICENSE.txt"
        snapshot.write_text(decision["license_text"] + "\n", encoding="utf-8", newline="\n")
        evidence_copy = None
        evidence_path = (evidence or {}).get("_evidence_file_path")
        if evidence_path:
            evidence_copy = license_dir / "evidence.json"
            shutil.copyfile(pathlib.Path(str(evidence_path)), evidence_copy)
        captured = str((evidence or {}).get("captured_utc", utc_now()))
        license_terms = policy["licenses"]["permitted"][decision["license_id"]]
        attribution_required = bool(license_terms.get("attribution", False))
        provenance = {
            "pipeline_version": PIPELINE_VERSION,
            "source": candidate["source"],
            "source_asset_id": candidate["id"],
            "asset_name": candidate["name"],
            "asset_page_url": candidate["page_url"],
            "download_url": candidate["download_url"],
            "acquisition_method": acquisition_method,
            "retrieved_from_url": acquisition_method == "network",
            "author": candidate["author"],
            "license_id": decision["license_id"],
            "license_url": decision["license_url"],
            "commercial_use": bool(license_terms.get("commercial_use", False)),
            "modification": bool(license_terms.get("modification", False)),
            "attribution_required": attribution_required,
            "required_attribution_text": str(candidate.get("attribution_text", "")) if attribution_required else "",
            "license_snapshot": "license/LICENSE.txt",
            "license_snapshot_sha256": sha256_file(snapshot),
            "license_evidence": {
                "captured_utc": captured,
                "file_name": str((evidence or {}).get("_evidence_file_name", "")),
                "file_sha256": str((evidence or {}).get("_evidence_file_sha256", "")),
                "saved_path": "license/evidence.json" if evidence_copy else "",
                "kind": str((evidence or {}).get("_evidence_kind", "")),
                "license_id": decision["license_id"],
                "license_url": decision["license_url"],
                "snapshot_path": str((evidence or {}).get("evidence_path", "")),
                "snapshot_sha256": str((evidence or {}).get("evidence_sha256", "")),
            },
            "retrieved_utc": captured,
            "original_filename": filename,
            "original_size": target.stat().st_size,
            "original_sha256": digest,
        }
        if local_source is not None:
            provenance["local_source"] = {"file_name": local_source.name, "sha256": sha256_file(local_source)}
        write_json(package / "candidate.json", {key: value for key, value in candidate.items() if not key.startswith("_")})
        write_json(package / "provenance.json", provenance)
        write_json(package / "reports" / "license.json", decision)
        write_json(package / "state.json", {"stage": "downloaded", "pipeline_version": PIPELINE_VERSION})
        return package
    except BaseException:
        # A failed intake must not leave a package that looks ready for review.
        shutil.rmtree(package, ignore_errors=True)
        raise


def _archive_limits(policy: Mapping[str, Any]) -> dict[str, int]:
    raw = policy.get("archive_limits", {})
    return {
        "max_files": int(raw.get("max_files", raw.get("max_entries", 4096))),
        "max_file_bytes": int(raw.get("max_file_bytes", 512 * 1024 * 1024)),
        "max_total_bytes": int(raw.get("max_total_bytes", raw.get("max_uncompressed_bytes", 2 * 1024 * 1024 * 1024))),
        "max_path_length": int(raw.get("max_path_length", 240)),
        "max_path_depth": int(raw.get("max_path_depth", 24)),
        "max_compression_ratio": int(raw.get("max_compression_ratio", 200)),
    }


def _bad_member(name: str, size: int, mode: int, kind: str, limits: Mapping[str, int]) -> str | None:
    normalized = name.replace("\\", "/")
    pure = pathlib.PurePosixPath(normalized)
    if not normalized or pure.is_absolute() or ".." in pure.parts or re.match(r"^[A-Za-z]:", normalized):
        return "path traversal or absolute path"
    if len(normalized) > limits["max_path_length"]:
        return "path exceeds limit"
    if len(pure.parts) > limits["max_path_depth"]:
        return "path depth exceeds limit"
    if size < 0 or size > limits["max_file_bytes"]:
        return "file size exceeds limit"
    if kind in {"symlink", "hardlink", "device", "fifo", "encrypted"}:
        return f"unsupported {kind}"
    if pathlib.PurePosixPath(normalized).suffix.lower() in EXECUTABLE_SUFFIXES or mode & 0o111:
        return "executable content"
    return None


def _executable_header(data: bytes) -> bool:
    return data.startswith((b"MZ", b"\x7fELF", b"#!")) or data[:4] in {
        b"\xcf\xfa\xed\xfe", b"\xce\xfa\xed\xfe", b"\xfe\xed\xfa\xcf", b"\xfe\xed\xfa\xce",
    }


def extract_archive(archive: pathlib.Path, destination: pathlib.Path, policy: Mapping[str, Any]) -> dict[str, Any]:
    """Scan a zip/tar fully, then extract it without following archive links."""
    limits = _archive_limits(policy)
    entries: list[tuple[str, int, int, str, Any]] = []
    suspicious: list[dict[str, str]] = []
    kind = "zip" if zipfile.is_zipfile(archive) else "tar" if tarfile.is_tarfile(archive) else ""
    if not kind:
        raise PipelineError("file is not a supported zip or tar archive")
    if kind == "zip":
        with zipfile.ZipFile(archive) as bundle:
            for member in bundle.infolist():
                mode = (member.external_attr >> 16) & 0xFFFF
                entry_kind = "encrypted" if member.flag_bits & 1 else "symlink" if stat.S_ISLNK(mode) else "directory" if member.is_dir() else "file"
                entries.append((member.filename, member.file_size, mode, entry_kind, member))
                if member.file_size and (not member.compress_size or member.file_size / member.compress_size > limits["max_compression_ratio"]):
                    suspicious.append({"entry": member.filename, "reason": "compression ratio exceeds limit"})
    else:
        with tarfile.open(archive, "r:*") as bundle:
            for member in bundle.getmembers():
                entry_kind = "symlink" if member.issym() else "hardlink" if member.islnk() else "device" if member.isdev() else "fifo" if member.isfifo() else "directory" if member.isdir() else "file"
                entries.append((member.name, member.size, member.mode, entry_kind, member))
    if len(entries) > limits["max_files"]:
        suspicious.append({"entry": "<archive>", "reason": "entry count exceeds limit"})
    total = 0
    for name, size, mode, entry_kind, _ in entries:
        if entry_kind == "file":
            total += size
        reason = _bad_member(name, size, mode, entry_kind, limits)
        if reason:
            suspicious.append({"entry": name, "reason": reason})
    if kind == "zip":
        with zipfile.ZipFile(archive) as bundle:
            for name, _, _, entry_kind, member in entries:
                if entry_kind == "file" and _executable_header(bundle.open(member).read(4)):
                    suspicious.append({"entry": name, "reason": "executable file header"})
    else:
        with tarfile.open(archive, "r:*") as bundle:
            for name, _, _, entry_kind, member in entries:
                if entry_kind == "file":
                    source = bundle.extractfile(member)
                    if source is not None:
                        with source:
                            if _executable_header(source.read(4)):
                                suspicious.append({"entry": name, "reason": "executable file header"})
    if total > limits["max_total_bytes"]:
        suspicious.append({"entry": "<archive>", "reason": "total expanded size exceeds limit"})
    report = {"accepted": not suspicious, "archive_type": kind, "entries": len(entries), "expanded_bytes": total,
              "suspicious_entries": sorted(suspicious, key=lambda item: (item["entry"], item["reason"]))}
    if suspicious:
        return report
    destination.mkdir(parents=True, exist_ok=True)
    if kind == "zip":
        with zipfile.ZipFile(archive) as bundle:
            for name, _, _, entry_kind, member in entries:
                target = contained_path(destination, pathlib.PurePosixPath(name))
                if entry_kind == "directory":
                    target.mkdir(parents=True, exist_ok=True)
                else:
                    target.parent.mkdir(parents=True, exist_ok=True)
                    with bundle.open(member) as source, target.open("wb") as output:
                        shutil.copyfileobj(source, output, 1024 * 1024)
    else:
        with tarfile.open(archive, "r:*") as bundle:
            for name, _, _, entry_kind, member in entries:
                target = contained_path(destination, pathlib.PurePosixPath(name))
                if entry_kind == "directory":
                    target.mkdir(parents=True, exist_ok=True)
                else:
                    target.parent.mkdir(parents=True, exist_ok=True)
                    source = bundle.extractfile(member)
                    if source is None:
                        raise PipelineError(f"cannot read archive member: {name}")
                    with source, target.open("wb") as output:
                        shutil.copyfileobj(source, output, 1024 * 1024)
    return report


def image_dimensions(data: bytes) -> tuple[int, int] | None:
    if data.startswith(b"\x89PNG\r\n\x1a\n") and len(data) >= 24:
        width, height = struct.unpack(">II", data[16:24])
        return (width, height) if width and height else None
    if data.startswith(b"\xff\xd8"):
        index = 2
        while index + 9 <= len(data):
            if data[index] != 0xFF:
                index += 1
                continue
            marker = data[index + 1]
            index += 2
            if marker in {0xD8, 0xD9}:
                continue
            if index + 2 > len(data):
                break
            length = struct.unpack(">H", data[index:index + 2])[0]
            if length < 2 or index + length > len(data):
                break
            if marker in {0xC0, 0xC1, 0xC2, 0xC3, 0xC5, 0xC6, 0xC7, 0xC9, 0xCA, 0xCB, 0xCD, 0xCE, 0xCF} and length >= 7:
                height, width = struct.unpack(">HH", data[index + 3:index + 7])
                return (width, height) if width and height else None
            index += length
    return None


def inspect_obj(path: pathlib.Path) -> dict[str, Any]:
    errors: list[str] = []
    vertices: list[tuple[float, float, float]] = []
    triangles = 0
    objects: set[str] = set()
    materials: set[str] = set()
    material_files: set[str] = set()
    try:
        lines = path.read_text(encoding="utf-8-sig", errors="strict").splitlines()
    except (OSError, UnicodeError) as exc:
        return {"format": "obj", "errors": [str(exc)]}
    for number, raw in enumerate(lines, 1):
        parts = raw.strip().split()
        if not parts or parts[0].startswith("#"):
            continue
        if parts[0] == "v":
            if len(parts) < 4:
                errors.append(f"line {number}: vertex needs three values")
                continue
            try:
                point = tuple(float(value) for value in parts[1:4])
                if not all(value == value and abs(value) != float("inf") for value in point):
                    raise ValueError("non-finite")
                vertices.append(point)  # type: ignore[arg-type]
            except ValueError:
                errors.append(f"line {number}: invalid vertex")
        elif parts[0] == "f":
            if len(parts) < 4:
                errors.append(f"line {number}: face needs at least three vertices")
            else:
                triangles += len(parts) - 3
                for token in parts[1:]:
                    try:
                        index = int(token.split("/")[0])
                        actual = index - 1 if index > 0 else len(vertices) + index
                        if actual < 0 or actual >= len(vertices):
                            raise ValueError
                    except ValueError:
                        errors.append(f"line {number}: invalid vertex index {token!r}")
        elif parts[0] in {"o", "g"} and len(parts) > 1:
            objects.add(" ".join(parts[1:]))
        elif parts[0] == "usemtl" and len(parts) > 1:
            materials.add(" ".join(parts[1:]))
        elif parts[0] == "mtllib" and len(parts) > 1:
            material_files.update(parts[1:])
    for name in sorted(material_files):
        try:
            material_path = contained_path(path.parent, name)
            if not material_path.is_file() or material_path.is_symlink():
                raise PipelineError("missing material library")
            for number, raw in enumerate(material_path.read_text(encoding="utf-8-sig", errors="strict").splitlines(), 1):
                fields = raw.strip().split()
                if fields and fields[0].lower().startswith("map_") and len(fields) > 1:
                    texture = contained_path(path.parent, fields[-1])
                    if not texture.is_file() or texture.is_symlink():
                        errors.append(f"{name} line {number}: missing or unsafe texture {fields[-1]}")
        except (OSError, UnicodeError, PipelineError) as exc:
            errors.append(f"unsafe material library {name}: {exc}")
    bounds = None
    if vertices:
        low = [min(point[axis] for point in vertices) for axis in range(3)]
        high = [max(point[axis] for point in vertices) for axis in range(3)]
        bounds = {"min": low, "max": high, "size": [high[i] - low[i] for i in range(3)]}
    return {"format": "obj", "meshes": max(1 if vertices else 0, len(objects)), "vertices": len(vertices),
            "triangles": triangles, "materials": len(materials), "material_names": sorted(materials),
            "material_libraries": sorted(material_files), "textures": 0, "bones": 0,
            "max_influences_per_vertex": 0, "animations": 0, "bounds": bounds,
            "orientation": {"declared": None, "status": "not encoded by OBJ"}, "errors": sorted(set(errors))}


def _glb_json(path: pathlib.Path) -> tuple[dict[str, Any], bytes | None, list[str]]:
    errors: list[str] = []
    data = path.read_bytes()
    if len(data) < 12:
        raise PipelineError("GLB header is truncated")
    magic, version, declared = struct.unpack_from("<4sII", data)
    if magic != b"glTF" or version != 2 or declared != len(data):
        raise PipelineError("GLB header, version, or declared length is invalid")
    index = 12
    json_chunk = None
    binary = None
    while index + 8 <= len(data):
        length, chunk_type = struct.unpack_from("<II", data, index)
        index += 8
        if index + length > len(data):
            raise PipelineError("GLB chunk exceeds file length")
        chunk = data[index:index + length]
        index += length
        if chunk_type == 0x4E4F534A and json_chunk is None:
            json_chunk = chunk.rstrip(b" \t\r\n\0")
        elif chunk_type == 0x004E4942 and binary is None:
            binary = chunk
    if index != len(data) or json_chunk is None:
        raise PipelineError("GLB has invalid chunk layout or no JSON chunk")
    try:
        document = json.loads(json_chunk.decode("utf-8"))
    except (UnicodeError, json.JSONDecodeError) as exc:
        raise PipelineError(f"invalid GLB JSON: {exc}") from exc
    return document, binary, errors


def _read_gltf_image(document: Mapping[str, Any], image: Mapping[str, Any], base: pathlib.Path, binary: bytes | None) -> bytes | None:
    uri = image.get("uri")
    if isinstance(uri, str):
        if uri.startswith("data:") and ";base64," in uri:
            try:
                return base64.b64decode(uri.split(",", 1)[1], validate=True)
            except ValueError:
                return None
        parsed = urllib.parse.urlparse(uri)
        if parsed.scheme or parsed.netloc:
            return None
        try:
            target = contained_path(base, urllib.parse.unquote(parsed.path))
            return target.read_bytes()
        except (OSError, PipelineError):
            return None
    view_index = image.get("bufferView")
    views = document.get("bufferViews", [])
    if isinstance(view_index, int) and isinstance(views, list) and 0 <= view_index < len(views) and binary is not None:
        view = views[view_index]
        if isinstance(view, dict) and int(view.get("buffer", 0)) == 0:
            start = int(view.get("byteOffset", 0)); end = start + int(view.get("byteLength", 0))
            return binary[start:end] if 0 <= start <= end <= len(binary) else None
    return None


def inspect_gltf(path: pathlib.Path) -> dict[str, Any]:
    errors: list[str] = []
    binary: bytes | None = None
    try:
        if path.suffix.lower() == ".glb":
            document, binary, errors = _glb_json(path)
        else:
            document = read_json(path)
    except (OSError, PipelineError) as exc:
        return {"format": path.suffix.lower().lstrip("."), "errors": [str(exc)]}
    if not isinstance(document, dict) or document.get("asset", {}).get("version") != "2.0":
        errors.append("glTF asset.version must be 2.0")
        document = document if isinstance(document, dict) else {}
    if path.suffix.lower() == ".gltf":
        for kind, rows in (("buffer", document.get("buffers", [])), ("image", document.get("images", []))):
            if not isinstance(rows, list):
                continue
            for index, row in enumerate(rows):
                uri = row.get("uri") if isinstance(row, dict) else None
                if not isinstance(uri, str) or uri.startswith("data:"):
                    continue
                parsed = urllib.parse.urlparse(uri)
                try:
                    target = contained_path(path.parent, urllib.parse.unquote(parsed.path))
                    if parsed.scheme or parsed.netloc or not target.is_file() or target.is_symlink():
                        raise PipelineError("missing or external path")
                except PipelineError:
                    errors.append(f"{kind} {index} has a missing, remote, or escaping URI")
    accessors = document.get("accessors", []) if isinstance(document.get("accessors", []), list) else []
    meshes = document.get("meshes", []) if isinstance(document.get("meshes", []), list) else []
    vertices = triangles = max_influences = 0
    bounds_min: list[list[float]] = []
    bounds_max: list[list[float]] = []
    for mesh_index, mesh in enumerate(meshes):
        if not isinstance(mesh, dict) or not isinstance(mesh.get("primitives"), list):
            errors.append(f"mesh {mesh_index} has no primitives array")
            continue
        for primitive in mesh["primitives"]:
            if not isinstance(primitive, dict):
                errors.append(f"mesh {mesh_index} has an invalid primitive")
                continue
            attrs = primitive.get("attributes", {})
            position_index = attrs.get("POSITION") if isinstance(attrs, dict) else None
            position_count = 0
            if isinstance(position_index, int) and 0 <= position_index < len(accessors) and isinstance(accessors[position_index], dict):
                accessor = accessors[position_index]
                position_count = int(accessor.get("count", 0))
                vertices += position_count
                if isinstance(accessor.get("min"), list) and isinstance(accessor.get("max"), list):
                    bounds_min.append(accessor["min"][:3]); bounds_max.append(accessor["max"][:3])
            else:
                errors.append(f"mesh {mesh_index} primitive lacks a valid POSITION accessor")
            index_index = primitive.get("indices")
            count = position_count
            if isinstance(index_index, int) and 0 <= index_index < len(accessors) and isinstance(accessors[index_index], dict):
                count = int(accessors[index_index].get("count", 0))
            mode = int(primitive.get("mode", 4))
            triangles += count // 3 if mode == 4 else max(0, count - 2) if mode in {5, 6} else 0
            primitive_influences = 0
            if isinstance(attrs, dict):
                for key in attrs:
                    if str(key).startswith("JOINTS_"):
                        suffix = str(key).split("_", 1)[1]
                        if f"WEIGHTS_{suffix}" not in attrs:
                            errors.append(f"mesh {mesh_index} has joints without matching weights")
                        accessor_index = attrs[key]
                        if isinstance(accessor_index, int) and 0 <= accessor_index < len(accessors):
                            kind = accessors[accessor_index].get("type", "") if isinstance(accessors[accessor_index], dict) else ""
                            primitive_influences += {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}.get(kind, 0)
            max_influences = max(max_influences, primitive_influences)
    skins = document.get("skins", []) if isinstance(document.get("skins", []), list) else []
    bones = max((len(skin.get("joints", [])) for skin in skins if isinstance(skin, dict) and isinstance(skin.get("joints", []), list)), default=0)
    texture_info = []
    images = document.get("images", []) if isinstance(document.get("images", []), list) else []
    for index, image in enumerate(images):
        blob = _read_gltf_image(document, image, path.parent, binary) if isinstance(image, dict) else None
        dimensions = image_dimensions(blob) if blob else None
        texture_info.append({"index": index, "width": dimensions[0] if dimensions else None, "height": dimensions[1] if dimensions else None})
        if blob is None:
            errors.append(f"image {index} is missing, external, or invalid")
    bounds = None
    if bounds_min and all(len(item) == 3 for item in bounds_min + bounds_max):
        low = [min(float(item[axis]) for item in bounds_min) for axis in range(3)]
        high = [max(float(item[axis]) for item in bounds_max) for axis in range(3)]
        bounds = {"min": low, "max": high, "size": [high[i] - low[i] for i in range(3)]}
    return {"format": path.suffix.lower().lstrip("."), "meshes": len(meshes), "vertices": vertices,
            "triangles": triangles, "materials": len(document.get("materials", [])) if isinstance(document.get("materials", []), list) else 0,
            "textures": len(document.get("textures", [])) if isinstance(document.get("textures", []), list) else 0,
            "texture_dimensions": texture_info, "skins": len(skins), "bones": bones,
            "max_influences_per_vertex": max_influences,
            "animations": len(document.get("animations", [])) if isinstance(document.get("animations", []), list) else 0,
            "bounds": bounds, "orientation": {"up_axis": "+Y", "forward_axis": "+Z", "source": "glTF 2.0 convention"},
            "errors": sorted(set(errors))}


def inspect_file(path: pathlib.Path) -> dict[str, Any]:
    suffix = path.suffix.lower()
    if suffix == ".obj":
        return inspect_obj(path)
    if suffix in {".glb", ".gltf"}:
        return inspect_gltf(path)
    if suffix in {".png", ".jpg", ".jpeg"}:
        dimensions = image_dimensions(path.read_bytes())
        return {"format": suffix.lstrip("."), "size": path.stat().st_size,
                "width": dimensions[0] if dimensions else None, "height": dimensions[1] if dimensions else None,
                "errors": [] if dimensions else ["invalid or unsupported image header"]}
    if suffix == ".wav":
        try:
            with wave.open(str(path), "rb") as audio:
                return {"format": "wav", "channels": audio.getnchannels(), "sample_rate": audio.getframerate(),
                        "frames": audio.getnframes(), "duration_seconds": audio.getnframes() / audio.getframerate(), "errors": []}
        except (wave.Error, OSError, ZeroDivisionError) as exc:
            return {"format": "wav", "errors": [str(exc)]}
    return {"format": suffix.lstrip(".") or "unknown", "size": path.stat().st_size, "errors": []}


def inspect_tree(root: pathlib.Path) -> dict[str, Any]:
    files = []
    for path in sorted((item for item in root.rglob("*") if item.is_file()), key=lambda item: item.relative_to(root).as_posix()):
        if path.is_symlink():
            files.append({"path": path.relative_to(root).as_posix(), "errors": ["symbolic links are not allowed"]})
            continue
        report = inspect_file(path)
        if path.suffix.lower() in EXECUTABLE_SUFFIXES or _executable_header(path.read_bytes()[:4]):
            report.setdefault("errors", []).append("executable content is not allowed")
        report["path"] = path.relative_to(root).as_posix()
        report["sha256"] = sha256_file(path)
        files.append(report)
    return {"pipeline_version": PIPELINE_VERSION, "files": files,
            "errors": sorted(error for item in files for error in item.get("errors", []))}


def _state(package: pathlib.Path, minimum: str | None = None) -> dict[str, Any]:
    state = read_json(package / "state.json")
    if not isinstance(state, dict) or state.get("stage") not in STAGE_ORDER:
        raise PipelineError("package state is missing or invalid")
    if minimum and STAGE_ORDER[str(state["stage"])] < STAGE_ORDER[minimum]:
        raise PipelineError(f"package must reach {minimum} before this command")
    return state


def inspect_package(package: pathlib.Path, policy: Mapping[str, Any]) -> dict[str, Any]:
    package = contained_path(_policy_path(policy, "staging_root", "asset_pipeline/staging"), package)
    _state(package, "downloaded")
    original_files = sorted(item for item in (package / "original").iterdir() if item.is_file())
    if len(original_files) != 1:
        raise PipelineError("package must contain one original download")
    source = original_files[0]
    inspect_root = package / "work" / "extracted"
    archive_report = None
    if zipfile.is_zipfile(source) or tarfile.is_tarfile(source):
        archive_report = extract_archive(source, inspect_root, policy)
        write_json(package / "reports" / "archive.json", archive_report)
        if not archive_report["accepted"]:
            raise PipelineError("archive contains suspicious entries; see reports/archive.json")
    else:
        inspect_root = package / "original"
    report = inspect_tree(inspect_root)
    report["archive"] = archive_report
    write_json(package / "reports" / "inspection.json", report)
    write_json(package / "state.json", {"stage": "inspected", "pipeline_version": PIPELINE_VERSION})
    return report


def _run_tool(command: Sequence[str], cwd: pathlib.Path, timeout: int) -> subprocess.CompletedProcess[str]:
    env = {key: value for key, value in os.environ.items() if key.upper() in {"PATH", "SYSTEMROOT", "WINDIR", "TEMP", "TMP"}}
    env.update({"LANG": "C", "LC_ALL": "C", "PYTHONHASHSEED": "0", "TZ": "UTC", "SOURCE_DATE_EPOCH": "0"})
    return subprocess.run(list(command), cwd=cwd, env=env, capture_output=True, text=True,
                          encoding="utf-8", errors="replace", timeout=timeout, check=False, shell=False)


def _bind_processing_input(path: pathlib.Path, policy: Mapping[str, Any]) -> dict[str, Any]:
    staging = _policy_path(policy, "staging_root", "asset_pipeline/staging").resolve()
    if path.absolute().is_symlink():
        raise PipelineError(f"processing input must not be a symbolic link: {path}")
    resolved = path.resolve()
    try:
        relative = resolved.relative_to(staging)
    except ValueError as exc:
        raise PipelineError(f"processing input is outside staging: {path}") from exc
    if not resolved.is_file() or resolved.is_symlink() or not relative.parts:
        raise PipelineError(f"processing input must be a regular staged file: {path}")
    source_package = staging / relative.parts[0]
    _state(source_package, "inspected")
    provenance_path = source_package / "provenance.json"
    provenance = read_json(provenance_path)
    if not isinstance(provenance, dict) or not provenance.get("license_id") or not provenance.get("license_snapshot_sha256"):
        raise PipelineError(f"processing input package has incomplete provenance: {source_package.name}")
    snapshot = source_package / str(provenance.get("license_snapshot", ""))
    if not snapshot.is_file() or sha256_file(snapshot) != provenance.get("license_snapshot_sha256"):
        raise PipelineError(f"processing input package has invalid license evidence: {source_package.name}")
    extracted = source_package / "work" / "extracted"
    original = source_package / "original"
    inspect_root = extracted if extracted.exists() else original
    try:
        inspected_name = resolved.relative_to(inspect_root.resolve()).as_posix()
    except ValueError as exc:
        raise PipelineError(f"processing input is outside the inspected tree: {path}") from exc
    inspection = read_json(source_package / "reports" / "inspection.json")
    rows = inspection.get("files", []) if isinstance(inspection, dict) else []
    digest = sha256_file(resolved)
    if not any(isinstance(row, dict) and row.get("path") == inspected_name and row.get("sha256") == digest for row in rows):
        raise PipelineError(f"processing input does not match the inspection report: {path}")
    return {"path": str(resolved), "sha256": digest, "source_package": source_package.name,
            "source_provenance_sha256": sha256_file(provenance_path), "license_id": provenance["license_id"]}


def process_package(package: pathlib.Path, policy: Mapping[str, Any], blender: pathlib.Path, script: pathlib.Path,
                    timeout: int = 900, request: pathlib.Path | None = None) -> dict[str, Any]:
    package = contained_path(_policy_path(policy, "staging_root", "asset_pipeline/staging"), package)
    _state(package, "inspected")
    inspection = read_json(package / "reports" / "inspection.json")
    if not isinstance(inspection, dict) or inspection.get("errors"):
        raise PipelineError("package inspection has errors and cannot be processed")
    configured_script = _policy_path(policy, "blender_script", "tools/asset_pipeline_blender.py").resolve()
    if script.resolve() != configured_script or blender.name.lower() not in {"blender", "blender.exe"}:
        raise PipelineError("processing must use the fixed script and a Blender executable")
    if not blender.is_file() or not configured_script.is_file():
        raise PipelineError("Blender executable and processing script must exist")
    output = package / "work" / "processed"
    output.mkdir(parents=True, exist_ok=True)
    report_path = package / "reports" / "process-tool.json"
    input_root = package / "work" / "extracted" if (package / "work" / "extracted").exists() else package / "original"
    supported = {"." + str(item).lower().lstrip(".") for item in policy.get("formats", {}).get("source", [])}
    inputs = sorted(item for item in input_root.rglob("*") if item.is_file() and item.suffix.lower() in supported)
    if len(inputs) != 1:
        raise PipelineError("processing needs exactly one supported source file")
    job: dict[str, Any] = {}
    if request is not None:
        raw_job = read_json(request)
        if isinstance(raw_job, dict) and isinstance(raw_job.get("request"), dict):
            raw_job = raw_job["request"]
        if not isinstance(raw_job, dict):
            raise PipelineError("processing request must be a JSON object")
        job = dict(raw_job)
    asset_type = str(job.get("asset_type", "prop"))
    budget = policy.get("budgets", {}).get(asset_type)
    if not isinstance(budget, dict):
        raise PipelineError(f"processing request has unknown asset_type: {asset_type}")
    job.update({"input_path": str(inputs[0].resolve()), "output_dir": str(output.resolve()),
                "asset_type": asset_type, "output_name": safe_name(str(job.get("output_name", package.name))),
                "budgets": dict(budget)})
    bound_paths = [inputs[0]]
    options = job.get("options", {})
    if not isinstance(options, dict):
        raise PipelineError("processing options must be an object")
    for part in options.get("mesh_parts", []):
        if not isinstance(part, dict) or not isinstance(part.get("path"), str):
            raise PipelineError("each mesh part needs a path")
        bound_paths.append((REPO_ROOT / part["path"]).resolve())
    animation_source = options.get("animation_source_path")
    if animation_source:
        if not isinstance(animation_source, str):
            raise PipelineError("animation source path must be text")
        bound_paths.append((REPO_ROOT / animation_source).resolve())
    bindings = [_bind_processing_input(path, policy) for path in bound_paths]
    staging = _policy_path(policy, "staging_root", "asset_pipeline/staging")
    package_names = {str(item["source_package"]) for item in bindings}
    for package_name in sorted(package_names):
        source_package = contained_path(staging, package_name)
        inspection = read_json(source_package / "reports" / "inspection.json")
        inspect_root = source_package / "work" / "extracted"
        if not inspect_root.exists():
            inspect_root = source_package / "original"
        for row in inspection.get("files", []) if isinstance(inspection, dict) else []:
            if isinstance(row, dict) and isinstance(row.get("path"), str):
                bindings.append(_bind_processing_input(inspect_root / row["path"], policy))
    bindings = list({item["path"]: item for item in bindings}.values())
    bindings.sort(key=lambda item: str(item["path"]))
    write_json(package / "reports" / "input-bindings.json", {"inputs": bindings})
    job["input_bindings"] = bindings
    request_path = package / "work" / "process-request.json"
    write_json(request_path, job)
    command = [str(blender.resolve()), "--background", "--factory-startup", "--disable-autoexec", "--python",
               str(script.resolve()), "--", str(request_path.resolve()), "--result", str(report_path.resolve())]
    result = _run_tool(command, REPO_ROOT, timeout)
    report = {"command": command, "returncode": result.returncode, "stdout": result.stdout, "stderr": result.stderr}
    write_json(package / "reports" / "process.json", report)
    if result.returncode != 0:
        raise PipelineError(f"Blender processing failed with exit code {result.returncode}")
    tool_report = read_json(report_path)
    version_prefix = str(policy.get("blender_version_prefix", ""))
    if (not isinstance(tool_report, dict) or tool_report.get("status") != "ok" or
            tool_report.get("input_bindings") != bindings or not str(tool_report.get("version", "")).startswith(version_prefix)):
        raise PipelineError("Blender report did not confirm the sealed input bindings")
    write_json(package / "state.json", {"stage": "processed", "pipeline_version": PIPELINE_VERSION})
    return report


def _budget_errors(report: Mapping[str, Any], budget: Mapping[str, Any]) -> list[str]:
    mapping = {"max_vertices": "vertices", "max_triangles": "triangles", "max_materials": "materials",
               "max_bones": "bones", "max_influences_per_vertex": "max_influences_per_vertex", "max_animations": "animations"}
    errors = []
    for item in report.get("files", []):
        if not isinstance(item, dict):
            continue
        for limit_name, metric in mapping.items():
            if limit_name in budget and int(item.get(metric, 0)) > int(budget[limit_name]):
                errors.append(f"{item.get('path')}: {metric} exceeds {limit_name}")
        if "max_texture_dimension" in budget:
            dimensions = item.get("texture_dimensions", [])
            if item.get("width"):
                dimensions = dimensions + [{"width": item.get("width"), "height": item.get("height")}]
            if any(max(int(dim.get("width") or 0), int(dim.get("height") or 0)) > int(budget["max_texture_dimension"]) for dim in dimensions):
                errors.append(f"{item.get('path')}: texture dimension exceeds max_texture_dimension")
    return errors


def _review_manifest(package: pathlib.Path) -> dict[str, Any]:
    """Hash the sealed review packet, apart from the manifest that holds the hashes."""
    files: list[dict[str, Any]] = []
    root = package.resolve()
    for current, directories, names in os.walk(root, topdown=True, followlinks=False):
        directories.sort()
        names.sort()
        current_path = pathlib.Path(current)
        for name in directories:
            path = current_path / name
            if path.is_symlink():
                raise PipelineError(f"review package contains a symbolic link: {path.relative_to(root).as_posix()}")
        for name in names:
            path = current_path / name
            relative = path.relative_to(root).as_posix()
            if relative == REVIEW_MANIFEST:
                continue
            mode = path.lstat().st_mode
            if stat.S_ISLNK(mode):
                raise PipelineError(f"review package contains a symbolic link: {relative}")
            if not stat.S_ISREG(mode):
                raise PipelineError(f"review package contains a non-regular file: {relative}")
            files.append({
                "path": relative,
                "sha256": sha256_file(path),
                "size": path.stat().st_size,
                "type": path.suffix.lower().lstrip(".") or "none",
            })
    files.sort(key=lambda item: str(item["path"]))
    return {"pipeline_version": PIPELINE_VERSION, "files": files}


def _load_review_manifest(package: pathlib.Path) -> dict[str, Any]:
    manifest_path = package / REVIEW_MANIFEST
    if not manifest_path.is_file() or manifest_path.is_symlink():
        raise PipelineError("review manifest is missing or invalid")
    expected = _review_manifest(package)
    manifest = read_json(manifest_path)
    if not isinstance(manifest, dict) or not isinstance(manifest.get("files"), list):
        raise PipelineError("review manifest is missing or invalid")
    if manifest != expected:
        raise PipelineError("review package files do not match the validation manifest")
    return manifest


def validate_package(package: pathlib.Path, policy: Mapping[str, Any], asset_class: str,
                     engine_validator: Sequence[str] | None = None, timeout: int = 300) -> dict[str, Any]:
    package = contained_path(_policy_path(policy, "staging_root", "asset_pipeline/staging"), package)
    _state(package, "processed")
    root = package / "work" / "processed"
    structural = inspect_tree(root)
    budget = policy.get("budgets", {}).get(asset_class)
    if not isinstance(budget, dict):
        raise PipelineError(f"unknown or missing asset budget: {asset_class}")
    errors = list(structural["errors"]) + _budget_errors(structural, budget)
    try:
        _review_manifest(package)
    except PipelineError as exc:
        errors.append(str(exc))
    runtime_suffixes = {"." + str(item).lower().lstrip(".") for item in policy.get("formats", {}).get("runtime", [])}
    runtime_files = sorted(
        item for item in root.rglob("*")
        if item.is_file() and not item.is_symlink() and item.suffix.lower() in runtime_suffixes and item.stat().st_size > 0
    )
    if not runtime_files:
        errors.append("processed output has no nonempty policy runtime-format file")
    engine = None
    glb_files = [item for item in runtime_files if item.suffix.lower() == ".glb"]
    configured_validator = policy.get("engine_validator")
    trusted_validator = _policy_path(policy, "engine_validator", "build/default/lg_duel_asset_validate")
    if os.name == "nt" and trusted_validator.suffix.lower() != ".exe":
        trusted_validator = trusted_validator.with_suffix(".exe")
    if engine_validator:
        if len(engine_validator) != 1 or pathlib.Path(engine_validator[0]).resolve() != trusted_validator.resolve():
            raise PipelineError("engine validator must match the fixed policy path")
    if glb_files and (not configured_validator or not trusted_validator.is_file()):
        errors.append("the fixed engine validator is missing")
    if glb_files and configured_validator and trusted_validator.is_file():
        engine = []
        manifests = policy.get("validation_manifests", {})
        manifest_value = manifests.get(asset_class) if isinstance(manifests, dict) else None
        manifest_path = _policy_path(policy, "_unused", str(manifest_value)) if manifest_value else None
        if asset_class == "player_model" and manifest_path is None:
            errors.append("player_model needs a fixed validation manifest")
        for runtime_file in glb_files:
            command = [str(trusted_validator.resolve()), str(runtime_file.resolve())]
            if manifest_path is not None:
                command += ["--manifest", str(manifest_path.resolve())]
            command += ["--instances", "16"]
            result = _run_tool(command, REPO_ROOT, timeout)
            engine.append({"command": command, "returncode": result.returncode, "stdout": result.stdout, "stderr": result.stderr})
            if result.returncode:
                errors.append(f"engine validator failed for {runtime_file.name} with exit code {result.returncode}")
    report = {"accepted": not errors, "asset_class": asset_class, "structural": structural,
              "engine_validator": engine, "errors": sorted(set(errors))}
    write_json(package / "reports" / "validation.json", report)
    if errors:
        raise PipelineError("asset validation failed; see reports/validation.json")
    write_json(package / "state.json", {"stage": "validated", "pipeline_version": PIPELINE_VERSION})
    write_json(package / REVIEW_MANIFEST, _review_manifest(package))
    return report


def import_for_review(package: pathlib.Path, policy: Mapping[str, Any], attribution_file: pathlib.Path | None = None,
                      third_party_file: pathlib.Path | None = None) -> pathlib.Path:
    package = contained_path(_policy_path(policy, "staging_root", "asset_pipeline/staging"), package)
    state_path = package / "state.json"
    if not state_path.is_file() or state_path.is_symlink():
        raise PipelineError("package state is missing or invalid")
    _state(package, "validated")
    # Verify every other sealed byte before trusting validation or provenance.
    _load_review_manifest(package)
    provenance = read_json(package / "provenance.json")
    validation = read_json(package / "reports" / "validation.json")
    if not isinstance(validation, dict) or validation.get("accepted") is not True:
        raise PipelineError("package has no accepted validation report")
    if not isinstance(provenance, dict):
        raise PipelineError("package provenance is invalid")
    original = package / "original" / str(provenance.get("original_filename", ""))
    snapshot = package / str(provenance.get("license_snapshot", ""))
    if not original.is_file() or sha256_file(original) != provenance.get("original_sha256"):
        raise PipelineError("original file does not match package provenance")
    if not snapshot.is_file() or sha256_file(snapshot) != provenance.get("license_snapshot_sha256"):
        raise PipelineError("license snapshot does not match package provenance")
    attribution_required = provenance.get("attribution_required") is True
    required_credit = provenance.get("required_attribution_text", "")
    if attribution_required and (not isinstance(required_credit, str) or not required_credit.strip()):
        raise PipelineError("attribution license has no exact required credit")
    if attribution_required and (attribution_file is None or third_party_file is None):
        raise PipelineError("attribution and third-party output files are required for this license")
    review_root = _policy_path(policy, "review_import_root", "asset_pipeline/review_imports")
    destination = contained_path(review_root, package.name)
    if destination.exists():
        raise PipelineError(f"review import already exists: {destination}")
    review_root.mkdir(parents=True, exist_ok=True)
    shutil.copytree(package, destination, symlinks=False)
    try:
        _load_review_manifest(destination)
    except BaseException:
        shutil.rmtree(destination, ignore_errors=True)
        raise
    entry = {key: provenance[key] for key in ("asset_name", "author", "source", "source_asset_id", "asset_page_url",
                                               "license_id", "license_url", "original_sha256")}
    entry["attribution_required"] = attribution_required
    entry["required_attribution_text"] = required_credit if attribution_required else ""
    for path, heading in ((attribution_file, "Third-party asset attribution"), (third_party_file, "Third-party assets")):
        if path is None:
            continue
        resolved = contained_path(REPO_ROOT, path)
        fields = [entry["asset_name"], entry["author"], entry["license_id"], entry["asset_page_url"],
                  f"SHA-256 {entry['original_sha256']}"]
        if attribution_required:
            fields.append(required_credit)
        line = "- " + " | ".join(str(item) for item in fields)
        prior = resolved.read_text(encoding="utf-8") if resolved.exists() else f"# {heading}\n\n"
        if line not in prior.splitlines():
            resolved.parent.mkdir(parents=True, exist_ok=True)
            resolved.write_text(prior.rstrip() + "\n\n" + line + "\n", encoding="utf-8", newline="\n")
    write_json(destination / "review-entry.json", entry)
    write_json(destination / REVIEW_MANIFEST, _review_manifest(destination))
    _load_review_manifest(destination)
    return destination


def _candidate_arg(path: str) -> dict[str, Any]:
    candidate_path = pathlib.Path(path).resolve()
    value = read_json(candidate_path)
    if not isinstance(value, dict):
        raise PipelineError("candidate must be a JSON object")
    value["_candidate_base"] = str(candidate_path.parent)
    return value


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--policy", type=pathlib.Path, default=DEFAULT_POLICY)
    sub = parser.add_subparsers(dest="command", required=True)
    search = sub.add_parser("search"); search.add_argument("query"); search.add_argument("--source"); search.add_argument("--catalog", type=pathlib.Path)
    license_cmd = sub.add_parser("inspect-license"); license_cmd.add_argument("candidate"); license_cmd.add_argument("--evidence", required=True)
    download = sub.add_parser("download"); download.add_argument("candidate"); download.add_argument("--evidence", required=True); download.add_argument("--local-file", type=pathlib.Path)
    inspect_cmd = sub.add_parser("inspect"); inspect_cmd.add_argument("package", type=pathlib.Path)
    process = sub.add_parser("process"); process.add_argument("package", type=pathlib.Path); process.add_argument("--blender", type=pathlib.Path, required=True); process.add_argument("--script", type=pathlib.Path, required=True); process.add_argument("--request", type=pathlib.Path); process.add_argument("--timeout", type=int, default=900)
    validate = sub.add_parser("validate"); validate.add_argument("package", type=pathlib.Path); validate.add_argument("--asset-class", required=True); validate.add_argument("--engine-validator", nargs="+"); validate.add_argument("--timeout", type=int, default=300)
    import_cmd = sub.add_parser("import"); import_cmd.add_argument("package", type=pathlib.Path); import_cmd.add_argument("--attribution-file", type=pathlib.Path); import_cmd.add_argument("--third-party-file", type=pathlib.Path)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        policy = load_policy(args.policy)
        if args.command == "search":
            if args.catalog is not None and not args.source:
                raise PipelineError("--catalog requires --source")
            result: Any = search_catalogs(args.query, policy, args.source, args.catalog)
            result = [{key: value for key, value in item.items() if not key.startswith("_")} for item in result]
        elif args.command == "inspect-license":
            candidate = _candidate_arg(args.candidate); evidence = load_license_evidence(pathlib.Path(args.evidence)) if args.evidence else None
            result = inspect_license(candidate, policy, evidence)
            print(canonical_json(result), end="")
            return 0 if result["approved"] else 2
        elif args.command == "download":
            candidate = _candidate_arg(args.candidate); evidence = load_license_evidence(pathlib.Path(args.evidence)) if args.evidence else None
            fetch = _default_fetch
            if args.local_file is not None:
                local_file = args.local_file.resolve()
                if not local_file.is_file() or local_file.is_symlink():
                    raise PipelineError("--local-file must name a regular downloaded file")

                def fetch(_url: str, destination: pathlib.Path, max_bytes: int) -> None:
                    if local_file.stat().st_size > max_bytes:
                        raise PipelineError("local download exceeds policy size limit")
                    shutil.copyfile(local_file, destination)
            result = {"package": str(stage_download(candidate, policy, evidence, fetch, args.local_file))}
        elif args.command == "inspect":
            result = inspect_package(args.package, policy)
        elif args.command == "process":
            result = process_package(args.package, policy, args.blender, args.script, args.timeout, args.request)
        elif args.command == "validate":
            result = validate_package(args.package, policy, args.asset_class, args.engine_validator, args.timeout)
        else:
            result = {"review_import": str(import_for_review(args.package, policy, args.attribution_file, args.third_party_file))}
        print(canonical_json(result), end="")
        return 0
    except PipelineError as exc:
        print(f"asset pipeline: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
