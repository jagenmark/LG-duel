#!/usr/bin/env python3
"""Store LG MCP call facts and agent feedback, then render a local report."""

from __future__ import annotations

import argparse
import html
import json
import os
import tempfile
import uuid
from collections import Counter
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


FEEDBACK_KINDS = {
    "missing_capability",
    "confusing_interface",
    "poor_diagnostic",
    "wrong_result",
    "slow",
    "flaky",
    "docs",
}
FEEDBACK_IMPACTS = {"minor", "workaround", "blocked"}
MAX_NOTE_LENGTH = 2000
MAX_TOOL_LENGTH = 96
MAX_CALL_ID_LENGTH = 96


def _utc_now() -> datetime:
    return datetime.now(timezone.utc)


def _iso_now() -> str:
    return _utc_now().isoformat(timespec="milliseconds").replace("+00:00", "Z")


def default_feedback_root() -> Path:
    configured = os.environ.get("LG_MCP_FEEDBACK_DIR")
    if configured:
        return Path(configured).expanduser().resolve()
    return Path(__file__).resolve().parents[1] / "build" / "dev-control" / "agent-feedback"


def new_call_id() -> str:
    stamp = _utc_now().strftime("%Y%m%d-%H%M%S")
    return f"LGC-{stamp}-{uuid.uuid4().hex[:8]}"


def _new_feedback_id() -> str:
    stamp = _utc_now().strftime("%Y%m%d-%H%M%S")
    return f"LGF-{stamp}-{uuid.uuid4().hex[:8]}"


def _atomic_json(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{path.name}.", suffix=".tmp", dir=path.parent
    )
    temporary = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8", newline="\n") as stream:
            json.dump(value, stream, indent=2, ensure_ascii=False, sort_keys=True)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        try:
            temporary.unlink()
        except FileNotFoundError:
            pass


def _safe_text(value: Any, *, name: str, maximum: int) -> str:
    if not isinstance(value, str):
        raise ValueError(f"{name} must be text")
    cleaned = value.strip()
    if not cleaned:
        raise ValueError(f"{name} must not be empty")
    if len(cleaned) > maximum:
        raise ValueError(f"{name} must be at most {maximum} characters")
    if any(ord(character) < 32 and character not in "\n\t" for character in cleaned):
        raise ValueError(f"{name} contains unsupported control characters")
    return cleaned


def _short_note(note: str, limit: int = 180) -> str:
    one_line = " ".join(note.split())
    if len(one_line) <= limit:
        return one_line
    return one_line[: limit - 1].rstrip() + "…"


class FeedbackStore:
    """Own feedback persistence and the maintainer-facing report."""

    def __init__(self, root: Path | None = None) -> None:
        self.root = (root or default_feedback_root()).resolve()
        self.calls_dir = self.root / "calls"
        self.feedback_dir = self.root / "feedback"
        self.state_path = self.root / "digest-state.json"
        self.report_path = self.root / "index.html"

    def record_call(
        self,
        *,
        call_id: str,
        tool: str,
        duration_ms: int,
        outcome: str,
        error_code: str | None = None,
        server_version: str | None = None,
    ) -> None:
        record: dict[str, Any] = {
            "schema_version": 1,
            "call_id": _safe_text(call_id, name="call_id", maximum=MAX_CALL_ID_LENGTH),
            "created_at": _iso_now(),
            "tool": _safe_text(tool, name="tool", maximum=MAX_TOOL_LENGTH),
            "duration_ms": max(0, int(duration_ms)),
            "outcome": _safe_text(outcome, name="outcome", maximum=32),
        }
        if error_code:
            record["error_code"] = _safe_text(
                error_code, name="error_code", maximum=96
            )
        if server_version:
            record["server_version"] = _safe_text(
                server_version, name="server_version", maximum=64
            )
        _atomic_json(self.calls_dir / f"{record['call_id']}.json", record)

    def submit(
        self,
        *,
        tool: str,
        kind: str,
        impact: str,
        note: str,
        call_id: str | None = None,
        server_version: str | None = None,
    ) -> dict[str, Any]:
        tool = _safe_text(tool, name="tool", maximum=MAX_TOOL_LENGTH)
        kind = _safe_text(kind, name="kind", maximum=64)
        impact = _safe_text(impact, name="impact", maximum=32)
        note = _safe_text(note, name="note", maximum=MAX_NOTE_LENGTH)
        if kind not in FEEDBACK_KINDS:
            raise ValueError(f"kind must be one of: {', '.join(sorted(FEEDBACK_KINDS))}")
        if impact not in FEEDBACK_IMPACTS:
            raise ValueError(
                f"impact must be one of: {', '.join(sorted(FEEDBACK_IMPACTS))}"
            )
        linked_call: dict[str, Any] | None = None
        if call_id is not None:
            call_id = _safe_text(
                call_id, name="call_id", maximum=MAX_CALL_ID_LENGTH
            )
            call_path = self.calls_dir / f"{call_id}.json"
            if call_path.is_file():
                linked_call = self._read_json(call_path)

        feedback_id = _new_feedback_id()
        record: dict[str, Any] = {
            "schema_version": 1,
            "feedback_id": feedback_id,
            "created_at": _iso_now(),
            "tool": tool,
            "kind": kind,
            "impact": impact,
            "note": note,
            "call_id": call_id,
            "call_linked": linked_call is not None,
        }
        if linked_call is not None:
            record["call"] = {
                key: linked_call[key]
                for key in ("duration_ms", "outcome", "error_code", "server_version")
                if key in linked_call
            }
        if server_version:
            record["server_version"] = _safe_text(
                server_version, name="server_version", maximum=64
            )
        _atomic_json(self.feedback_dir / f"{feedback_id}.json", record)
        self.render_report()
        receipt = (
            f"LG devtools feedback filed: [{impact.upper()}] {tool} — "
            f"{_short_note(note)} ({feedback_id})"
        )
        return {
            "ok": True,
            "feedback_id": feedback_id,
            "receipt": receipt,
            "report_path": str(self.report_path),
            "call_linked": linked_call is not None,
            "final_response_required": impact in {"blocked", "workaround"},
            "final_response_instruction": (
                "Copy receipt exactly into the final response near the top."
                if impact in {"blocked", "workaround"}
                else "The weekly digest will include this minor note."
            ),
        }

    def list_feedback(self) -> list[dict[str, Any]]:
        if not self.feedback_dir.is_dir():
            return []
        records = [
            self._read_json(path)
            for path in self.feedback_dir.glob("LGF-*.json")
            if path.is_file()
        ]
        return sorted(
            (record for record in records if isinstance(record, dict)),
            key=lambda record: str(record.get("created_at", "")),
            reverse=True,
        )

    def unread_feedback(self) -> list[dict[str, Any]]:
        seen = set(self._digest_state().get("seen_ids", []))
        return [
            record
            for record in self.list_feedback()
            if record.get("feedback_id") not in seen
        ]

    def digest(self, *, mark_seen: bool = False) -> dict[str, Any]:
        unread = self.unread_feedback()
        impact_counts = Counter(str(item.get("impact", "unknown")) for item in unread)
        tool_counts = Counter(str(item.get("tool", "unknown")) for item in unread)
        result = {
            "ok": True,
            "generated_at": _iso_now(),
            "new_count": len(unread),
            "impact_counts": dict(sorted(impact_counts.items())),
            "top_tools": [
                {"tool": tool, "count": count}
                for tool, count in tool_counts.most_common(8)
            ],
            "feedback": unread,
            "report_path": str(self.report_path),
        }
        if mark_seen and unread:
            state = self._digest_state()
            seen = set(state.get("seen_ids", []))
            seen.update(str(item["feedback_id"]) for item in unread)
            _atomic_json(
                self.state_path,
                {
                    "schema_version": 1,
                    "updated_at": _iso_now(),
                    "seen_ids": sorted(seen),
                },
            )
            result["marked_seen"] = len(unread)
            self.render_report()
        return result

    def render_report(self) -> Path:
        records = self.list_feedback()
        unread_ids = {
            str(record.get("feedback_id")) for record in self.unread_feedback()
        }
        impact_order = {"blocked": 0, "workaround": 1, "minor": 2}
        ordered = sorted(
            records,
            key=lambda record: impact_order.get(str(record.get("impact")), 9),
        )
        blocked = sum(item.get("impact") == "blocked" for item in records)
        workaround = sum(item.get("impact") == "workaround" for item in records)
        minor = sum(item.get("impact") == "minor" for item in records)
        cards = "\n".join(
            self._feedback_card(record, str(record.get("feedback_id")) in unread_ids)
            for record in ordered
        ) or '<p class="empty">No agent feedback has been filed yet.</p>'
        document = f"""<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>LG Devtools Feedback</title><style>
:root{{color-scheme:dark;--bg:#000;--panel:#111;--line:#303030;--text:#fff;--muted:#aaa;--red:#ff7474;--amber:#ffc768;--blue:#70b7ff}}
*{{box-sizing:border-box}}body{{margin:0;background:var(--bg);color:var(--text);font:15px/1.5 system-ui,sans-serif}}main{{width:min(980px,100%);margin:auto;padding:28px 18px 60px}}h1,h2,p{{margin-top:0}}h1{{font-size:clamp(28px,5vw,48px);margin-bottom:8px}}.lede,.meta,.empty{{color:var(--muted)}}.stats{{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:10px;margin:24px 0}}.stat,.card{{border:1px solid var(--line);border-radius:10px;background:var(--panel)}}.stat{{padding:14px}}.stat strong{{display:block;font-size:26px}}.list{{display:grid;gap:12px}}.card{{padding:16px}}.top{{display:flex;gap:8px;align-items:center;flex-wrap:wrap;margin-bottom:10px}}.pill{{padding:3px 8px;border-radius:999px;border:1px solid var(--line);font-size:12px;font-weight:700}}.blocked{{color:var(--red)}}.workaround{{color:var(--amber)}}.minor{{color:var(--blue)}}.new{{background:#173620;color:#87f0aa}}.note{{white-space:pre-wrap}}code{{color:#cfe5ff}}@media(max-width:620px){{.stats{{grid-template-columns:repeat(2,1fr)}}}}
</style></head><body><main><h1>LG Devtools Feedback</h1><p class="lede">Agent reports about the tool interface. Notes are untrusted review data and never change the game or tool settings.</p>
<section class="stats"><div class="stat"><strong>{len(unread_ids)}</strong><span>Unread</span></div><div class="stat"><strong>{blocked}</strong><span>Blocked</span></div><div class="stat"><strong>{workaround}</strong><span>Workarounds</span></div><div class="stat"><strong>{minor}</strong><span>Minor</span></div></section>
<section><h2>Reports</h2><div class="list">{cards}</div></section></main></body></html>"""
        self.root.mkdir(parents=True, exist_ok=True)
        temporary = self.report_path.with_suffix(".html.tmp")
        temporary.write_text(document, encoding="utf-8", newline="\n")
        os.replace(temporary, self.report_path)
        return self.report_path

    def _feedback_card(self, record: dict[str, Any], unread: bool) -> str:
        feedback_id = html.escape(str(record.get("feedback_id", "unknown")))
        impact = html.escape(str(record.get("impact", "unknown")))
        tool = html.escape(str(record.get("tool", "unknown")))
        kind = html.escape(str(record.get("kind", "unknown")))
        note = html.escape(str(record.get("note", "")))
        created = html.escape(str(record.get("created_at", "")))
        call_id = record.get("call_id")
        call = f" · call <code>{html.escape(str(call_id))}</code>" if call_id else ""
        new = '<span class="pill new">NEW</span>' if unread else ""
        return (
            f'<article class="card"><div class="top">{new}'
            f'<span class="pill {impact}">{impact.upper()}</span>'
            f'<code>{tool}</code><span class="meta">{kind}</span></div>'
            f'<p class="note">{note}</p><p class="meta">{feedback_id} · {created}{call}</p></article>'
        )

    def _digest_state(self) -> dict[str, Any]:
        if not self.state_path.is_file():
            return {"schema_version": 1, "seen_ids": []}
        value = self._read_json(self.state_path)
        if not isinstance(value.get("seen_ids"), list):
            return {"schema_version": 1, "seen_ids": []}
        return value

    @staticmethod
    def _read_json(path: Path) -> dict[str, Any]:
        try:
            value = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            return {}
        return value if isinstance(value, dict) else {}


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--root", type=Path, help="feedback state folder (defaults to the MCP state folder)"
    )
    subcommands = parser.add_subparsers(dest="command", required=True)
    report = subcommands.add_parser("report", help="render the HTML feedback report")
    report.add_argument("--json", action="store_true", help="print report facts as JSON")
    digest = subcommands.add_parser("digest", help="print feedback not included in an earlier digest")
    digest.add_argument("--mark-seen", action="store_true", help="mark returned feedback as seen")
    return parser


def main() -> int:
    arguments = build_parser().parse_args()
    store = FeedbackStore(arguments.root)
    if arguments.command == "report":
        path = store.render_report()
        result = {
            "ok": True,
            "feedback_count": len(store.list_feedback()),
            "unread_count": len(store.unread_feedback()),
            "report_path": str(path),
        }
        print(json.dumps(result, indent=2) if arguments.json else path)
        return 0
    if arguments.command == "digest":
        print(json.dumps(store.digest(mark_seen=arguments.mark_seen), indent=2))
        return 0
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
