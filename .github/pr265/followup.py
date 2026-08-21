from __future__ import annotations

import sys
from pathlib import Path


def replace_exact(path: Path, old: str, new: str, *, count: int = 1) -> None:
    text = path.read_text(encoding="utf-8")
    actual = text.count(old)
    if actual != count:
        raise RuntimeError(
            f"{path}: expected {count} occurrence(s) of {old!r}, found {actual}"
        )
    path.write_text(text.replace(old, new), encoding="utf-8")


def replace_all(path: Path, old: str, new: str) -> None:
    text = path.read_text(encoding="utf-8")
    if old not in text:
        raise RuntimeError(f"{path}: missing expected text {old!r}")
    path.write_text(text.replace(old, new), encoding="utf-8")


def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit("usage: followup.py <worktree> <manifest>")
    root = Path(sys.argv[1]).resolve()
    manifest = Path(sys.argv[2]).resolve()

    readme = root / "docs/replay/README.md"
    architecture = root / "docs/replay/ARCHITECTURE.md"
    format_doc = root / "docs/replay/FORMAT.md"
    operations = root / "docs/replay/OPERATIONS.md"

    replace_exact(readme, "The system has a bot-free v5 format, recorder, headless playback runner,", "The system has a bot-free v6 format, recorder, headless playback runner,")
    replace_exact(
        readme,
        "| `.lgdemo` v5 sparse-slot encoder/decoder and canonical hash | Implemented; v1 through v4 are rejected; covered by `lg_duel_replay_codec_tests` |",
        "| `.lgdemo` v6 sparse-slot encoder/decoder and canonical hash | Implemented; v1 through v5 are rejected; decoded native allocations are capped; covered by `lg_duel_replay_codec_tests` |",
    )
    replace_exact(
        readme,
        "`ReplayIoService` owns one worker and a bounded queue. The worker performs file\nand codec work. The app polls results on its main thread. The server uses the\nsame service for recording saves, directory scans, and deletes. `ServerGame::tick`\nonly records native replay data; it does not encode or write a file.",
        "`ReplayIoService` owns one worker and a bounded queue. The worker performs file\nand codec work. The app polls movable results on its main thread. A completed\nserver recording is retained and retried when temporary queue backpressure\nprevents immediate admission; killcam encode requests likewise remain pending\nuntil accepted or terminally invalid. `ServerGame::tick` only records native\nreplay data; it does not encode or write a file.",
    )
    replace_exact(
        readme,
        "The coordinator and client receiver run outside `ServerGame::tick` and render.\nThey do not read or write replay files on the server tick path. `killcam_skip`,\ndisconnect, timeout, map/reset generation changes, and failed decode clear the\nremote session without pausing or rewinding live play.",
        "The coordinator and client receiver run outside `ServerGame::tick` and render.\nThey do not read or write replay files on the server tick path. A reordered\ntransfer Begin may arrive before the authoritative death snapshot; the client\nkeeps only transfer identity until that matching snapshot binds the live victim\nstate, so packet order cannot turn a valid killcam into an alive-state rejection.\n`killcam_skip`, disconnect, timeout, map/reset generation changes, and failed\ndecode clear the remote session without pausing or rewinding live play.",
    )

    replace_all(architecture, "current v5 `ReplayTickInput`", "current v6 `ReplayTickInput`")
    replace_all(architecture, "V5 carries", "V6 carries")
    replace_exact(
        architecture,
        "The replay decoder accepts only format v5. It rejects v1 through v4 before\nrestoring any state.",
        "The replay decoder accepts only format v6. It rejects v1 through v5 before\nrestoring any state and charges native decoded allocations against a fixed\nresident-memory budget before growing replay containers.",
    )
    replace_exact(
        architecture,
        "`GameApp` owns a single\npresentation-source adapter that selects either the live `ClientGame` source or\nthe replay frame source at a frame boundary. Replay commands never enter the\nlive command send path. The replay source supplies the arena, snapshot, player",
        "`GameApp` owns a single\npresentation-source adapter that selects either the live `ClientGame` source or\nthe replay frame source at a frame boundary. Replay commands never enter the\nlive command send path; normal transport Ping/Pong traffic keeps the authenticated\nconnection alive while replay presentation owns input. The replay source supplies\nthe arena, snapshot, player",
    )
    replace_exact(
        architecture,
        "The coordinator marks pending and active transfers for an explicit Cancel packet;\nthe receiver still has an idle/overall timeout if that packet is lost. The\ncoordinator and client receiver invoke the same cleanup path and leave live\nplay intact when they reject data.",
        "The coordinator marks pending and active transfers for an explicit Cancel packet;\nthe receiver still has an idle/overall timeout if that packet is lost. After a\nreceiver assembles a transfer it retains a short completion tombstone, allowing\na duplicate terminal chunk to recover a lost final ACK without pinning the\nserver slot until timeout. The coordinator and client receiver invoke the same\ncleanup path and leave live play intact when they reject data.",
    )

    replace_all(format_doc, "Format version 5", "Format version 6")
    replace_all(format_doc, "Versions 1 through 4", "Versions 1 through 5")
    replace_all(format_doc, "v5 wire contract", "v6 wire contract")
    replace_exact(format_doc, "- format version: `5` (`kReplayFormatVersion`);", "- format version: `6` (`kReplayFormatVersion`);")
    replace_all(format_doc, "Version 5", "Version 6")
    replace_all(format_doc, "version 5", "version 6")
    replace_all(format_doc, "v5 order", "v6 order")
    replace_all(format_doc, "V5 stores", "V6 stores")
    replace_all(format_doc, "Each v5 chunk", "Each v6 chunk")
    replace_all(format_doc, "no v5 chunk flags", "no v6 chunk flags")
    replace_all(format_doc, "A v5 `TickInputs`", "A v6 `TickInputs`")
    replace_all(format_doc, "V5 does not", "V6 does not")
    replace_all(format_doc, "validated v5 `ReplayDemo`", "validated v6 `ReplayDemo`")
    replace_all(format_doc, "v5 `ReplayDemo`", "v6 `ReplayDemo`")
    replace_all(format_doc, "v5 round trips", "v6 round trips")
    replace_all(format_doc, "v5 chunk", "v6 chunk")
    replace_all(format_doc, "protocol version 61", "protocol version 62")
    replace_exact(
        format_doc,
        "- saved-file cap: 512 MiB; chunk cap: 8 MiB; tick cap: 4,194,304; checkpoint cap:\n  4,096; and lag-history cap: 256 frames; and",
        "- saved-file cap: 512 MiB; decoded native resident cap: 512 MiB; chunk cap:\n  8 MiB; tick cap: 4,194,304; checkpoint cap: 4,096; lag-history cap: 256\n  frames; and",
    )
    replace_exact(
        format_doc,
        "Validation happens before allocation where possible. Bounded allocations and\ncount checks come before decode loops. The decoder does not repair corrupt data,\nskip unknown required records, or apply the valid prefix of a bad checkpoint.",
        "Validation happens before allocation where possible. Bounded allocations and\ncount checks come before decode loops. Every native replay record and nested\ncheckpoint-history allocation is charged to a checked decoded-resident budget;\na compact file is rejected before it can expand beyond that cap. Allocation\nfailures return a clean decode error without mutating the destination. The\ndecoder does not repair corrupt data, skip unknown required records, or apply\nthe valid prefix of a bad checkpoint.",
    )
    replace_exact(
        format_doc,
        "The receiver permits duplicate and\nout-of-order chunks but completes only when every index is present, the byte\ncount matches, every CRC-32 matches, and the whole payload matches the Begin\nSHA-256. Idle and overall timeouts, disconnects, session changes, generation\nchanges, cancel, and map/content checks clear incomplete data.",
        "The receiver permits duplicate and\nout-of-order chunks but completes only when every index is present, the byte\ncount matches, every CRC-32 matches, and the whole payload matches the Begin\nSHA-256. It retains a one-second completion tombstone so a retransmitted final\nchunk receives another ACK if the original terminal ACK was lost. Idle and\noverall timeouts, disconnects, session changes, generation changes, cancel, and\nmap/content checks clear incomplete data.",
    )
    replace_exact(
        format_doc,
        "A completed payload goes through the existing `ReplayIoService` decode\njob and `ReplayRuntime` path.",
        "A completed payload goes through the existing `ReplayIoService` decode\njob and `ReplayRuntime` path. Transfer Begin identity and the authoritative death\nsnapshot are bound independently, so Begin-before-snapshot reordering does not\nrecord an alive state or reject the later valid killcam. Replay presentation\nuses transport Ping/Pong rather than a default gameplay command as keepalive.",
    )

    replace_exact(
        operations,
        "The worker queue is bounded. A full queue rejects the request with a console\nerror. The worker has no callback into `GameApp`, `ServerGame`, or the renderer;\nthe owner polls `ReplayIoService::Result` and applies the result on its own\nthread.",
        "The worker queue is bounded. Ordinary new work receives a clear queue-full\nerror, but temporary backpressure does not destroy already completed data: the\nserver retains a finished recording until save admission succeeds, and the\nkillcam coordinator leaves a ready lethal event pending until its encode job is\naccepted or becomes terminally stale. The worker has no callback into `GameApp`,\n`ServerGame`, or the renderer; the owner polls movable\n`ReplayIoService::Result` values and applies them on its own thread.",
    )
    replace_exact(
        operations,
        "A segment that has no valid checkpoint, crosses a map/reset generation, exceeds\nits cap, crosses a dropped authority boundary, or has missing data is rejected.",
        "A segment that has no valid checkpoint, crosses a map/reset generation, exceeds\nits cap, crosses a genuinely missing authority boundary, or has missing data is\nrejected. Pruning an obsolete boundary behind the retained anchor does not mark\nlater killcams incomplete.",
    )
    replace_exact(
        operations,
        "The remote killcam must never pause or rewind the server, change respawn rules,\ndelay a round or match, inject replay commands into the live player, or replace\nlive client state. It must abort on control return, skip, respawn, round/match",
        "The remote killcam must never pause or rewind the server, change respawn rules,\ndelay a round or match, inject replay commands into the live player, or replace\nlive client state. During playback, connection liveness comes from transport\nPing/Pong; no default gameplay command is accepted as a keepalive. It must abort\non control return, skip, respawn, round/match",
    )
    replace_exact(
        operations,
        "receiver expires on idle or overall timeout when a cancel packet is lost, so\nstale transfer state cannot remain pinned. The server sends at a per-tick\npacket budget, and a failed transfer only skips the killcam.",
        "receiver expires on idle or overall timeout when a cancel packet is lost, so\nstale transfer state cannot remain pinned. A short completion tombstone ACKs a\nretransmitted terminal chunk, recovering a lost final ACK without waiting for\nthe server timeout. The server sends at a per-tick packet budget, and a failed\ntransfer only skips the killcam.",
    )
    replace_exact(
        operations,
        "The client binds Begin/Chunk messages to its current session and active transfer.\nReplay or map generation changes drop pending work and mark active work for an",
        "The client binds Begin/Chunk messages to its current session and active transfer.\nIt binds the live victim state only after observing the matching authoritative\ndeath/respawn snapshot, so a reordered Begin cannot snapshot the player as alive.\nReplay or map generation changes drop pending work and mark active work for an",
    )

    for path in (readme, architecture, format_doc, operations):
        text = path.read_text(encoding="utf-8")
        if path != operations and any(token in text for token in (" v5 ", "`v5", "V5 ", "version 5", "Version 5")):
            raise RuntimeError(f"{path}: stale v5 contract text remains")
        if "protocol version 61" in text:
            raise RuntimeError(f"{path}: stale protocol 61 text remains")

    changed = [
        "docs/replay/README.md",
        "docs/replay/ARCHITECTURE.md",
        "docs/replay/FORMAT.md",
        "docs/replay/OPERATIONS.md",
    ]
    manifest.write_text("\n".join(changed) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
