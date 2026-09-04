# Networking

Networking is UDP-oriented and snapshot based. Packet structures live in `src/net/NetProtocol.hpp`; binary layout and validation live in `src/net/NetCodec.*`; transport/session behavior is in `src/net/UdpTransport.*`, `src/client/ClientSession.*`, `src/client/ClientGame.*`, and server code in `src/server/ServerGame.*`.

## Packets And Protocol

`NetCodec.hpp` defines `kProtocolMagic`, `kProtocolVersion` (`63` at this writing), `kMaxPacketBytes`, the 1,200-byte UDP application-datagram ceiling, and `PacketType`. Every packet has a fixed header: magic, version, type, flags, payload byte count, and reserved field. The codec rejects wrong versions, invalid enum values, non-finite floats, out-of-range tuning values, invalid strings, malformed sparse masks, invalid compression back-references, inconsistent expansion lengths, and trailing bytes. Player scores use signed 16-bit values so Free For All self-kills can take a score below zero.

Snapshot and combat-stat payloads use a deterministic, stateless 4 KiB-window compressor when it reduces their size. Compression is lossless: authoritative player state, command acknowledgements, ammo, and slot indices retain their exact wire values. Snapshot encoding, UDP receive, and the UDP send boundary enforce 1,200 bytes, so the application never depends on IP fragmentation. An event-heavy snapshot is retried in fixed priority order without rewind diagnostics, movement audio, recipient-irrelevant hit-feedback windows, recurring beam visuals, and finally lower-priority transient combat visuals. Player, objective, score, and match state are never discarded to make room. If that authoritative core still cannot fit, encoding fails closed and the transport reports an error instead of sending a partial or fragmented core.

Full live projectile arrays do not belong in gameplay snapshots. The server
sends bounded projectile update packets for spawns, removals, and periodic
correction batches. Each record carries a stable 16-bit slot, sequence, update
kind, and the state needed for client display. Clients move the display copy
locally; server collision, damage, and explosion events remain authoritative.
Revisions clear old projectiles after resets or map changes. An adaptive
round-robin budget aims to refresh the active set in 24 ticks, while new spawns
and removals take priority. A packet holds at most 28 records and 1,173 bytes;
a rare event burst may use more than one bounded packet.

Supported packet types are connect request/accept, command, command bundle,
snapshot, projectile updates, combat statistics, ping/pong, disconnect, chat
history, chat-history acknowledgement, and the replay-transfer Begin, Chunk,
Ack, and Cancel messages. Remote replay transfer is session- and
generation-bound, caps one segment at 512 KiB, checks each chunk with CRC-32,
and verifies the assembled stream with SHA-256 before bounded decoding. `CommandBundle` carries up to 12
compact commands (about 96 ms at 125 Hz), shares client identity and cumulative
action edges once, and is trimmed from the oldest command when a rare control
payload would exceed the 1,200-byte datagram budget. Attack, jump, dash, reset,
ready, and McGuffin-throw edges are deduplicated server-side; attack and throw
edges retain their original aim.

## Connections, Players, And Spectators

A UDP connection slot is distinct from an authoritative player body. The
transport accepts up to sixteen mapped players and eight additional spectators.
Commands identify the stable connection slot; the server transport validates
that identity and translates accepted gameplay commands to the connection's
current player index. Per-recipient snapshot fields report whether the client
currently owns a body. Consequently, releasing a body with `team spectator`
does not disconnect the observer, and observer connections never enter the
fixed gameplay arrays used for spawning, readiness, scoring, or objectives.
Connection-authenticated spectator chat keeps the no-body sentinel and is
deduplicated separately; it does not grant access to body-authoritative input.

## Command Ownership

Clients own input intent: movement axes, view angles, attack/jump/weapon request, ready/reset/team/game-mode requests, the one-shot McGuffin throw request, chat/name/map requests, and optional cvar/tuning values. The server owns acceptance. `ServerGame::receiveCommands()` ignores stale command sequences using `isSequenceNewer()`, stores `viewedServerTick` for lag compensation, and writes acknowledged sequence state into the snapshot. Accepted chat is retained in a bounded server ring and replicated through acknowledged, four-message history chunks; chat text is not repeated in high-frequency gameplay snapshots.

Command ordering belongs to the connection and survives match and map resets. The UDP transport rejects old commands before changing player/spectator assignments, including while the client has no player body. A new connection session starts a fresh command sequence. Match resets retain acknowledgements for all players, so redundant input cannot repeat chat, roster changes, or resets from the previous match.

Each server transport update reads at most 64 datagrams, including malformed and unauthenticated packets, then returns so the simulation can advance. The command queue holds at most 256 commands. When it is full, the transport drops further commands without advancing their command sequence; clients can retry them through the existing redundant bundles. These limits bound game-thread work and queue growth; they do not provide traffic filtering or per-client bandwidth fairness.

Every command bundle also carries a transport datagram sequence. Per-client snapshots return the newest accepted sequence and a 32-bit acknowledgement mask for the preceding datagrams. This adds four bytes to command bundles and eight bytes to gameplay snapshots, and lets the client measure upstream loss independently from command recovery through redundant bundles.

The optional client-carried `g_*` tuning path is a temporary development affordance. Server startup defaults for those values come from `config/server_cvars.cfg`, while non-cvar authoritative balance comes from server-side `config/balance.cfg`. Clients must not load local `balance.cfg` for gameplay authority.

## Snapshot Ownership

`ServerSnapshot` is authoritative for player states, selected weapons, lightning results, weapon fire and explosion events, freeze-gun ice pools, footsteps, frags, scores, teams, match phase/rules, cvar-derived gameplay tuning, map revision, damage-feedback timeline revision, and optional arena data. Bounded projectile update packets carry display state for live projectiles. Chat history is authoritative server state but uses its separate acknowledged packet stream.

Inactive fixed-capacity event slots are represented by multiword occupancy masks; unused high bits are invalid rather than aliases for future slots. Sparse payloads do not repeat an `active` byte. Frags, projectile explosions, and grenade bounces each use a global sixteen-record ring with an explicit owner or attacker and a nonzero sequence. A separate slot cursor prevents sequence wrap from reusing a live slot. The server keeps the newest sixteen records and repeats each for eight ticks. Clients sort unseen records by sequence and ignore repeats. Each sixteen-player boolean row uses a validated `uint16_t` mask; the 32 health-pickup bits use a `uint32_t` mask. Snapshot ammo uses a two-byte value for counts below `65535` and a marked four-byte extension for larger nonnegative signed simulation values. Gameplay snapshots do not carry scoreboard combat aggregates over UDP. Instead, every command repeats whether that client currently has the scoreboard open; a closed-to-open transition sends four independently bounded four-player statistics pages immediately, followed by 5 Hz refreshes only to that client while the scoreboard remains open. The client stages pages by server tick and publishes a new aggregate only after all sixteen player rows arrive, so diverse valid counters never require fragmentation or leave a partially updated scoreboard. Gameplay configuration has its own revision: snapshots carry that revision, commands acknowledge the latest configuration installed by the client, and the server repeats the full configuration block per client until the matching acknowledgement arrives. A client never applies a lean snapshot for an unknown configuration revision.

Player names have a separate revision and acknowledgement. The server sends a full name row until that client acknowledges the exact revision, then sends only the revision. The client caches a full row, rejects a lean snapshot for an unknown newer row, and never lets an old full row roll its cache back. This keeps a roster change loss-safe without writing names into normal gameplay snapshots.

## Victim damage feedback

The server records `DamageTakenEvent` records by victim. The sparse wire payload is eight bytes: `sequence:u32`, `direction256:u8`, `presentationDamage:u8`, `metadata:u8`, and `weapon:u8`. `presentationDamage` is `min(actual health loss, 255)` and does not affect authoritative health. Metadata bit 0 marks a valid direction, bit 1 self damage, bit 2 a valid attacker, and bits 4-7 hold that attacker slot. Bit 3 is reserved and rejected. A no-direction event must encode bearing zero. A self event must name that victim as its valid attacker, and that matching attacker must set the self bit. These checks reject ambiguous or forged packet forms.

`direction256` is a victim-to-source horizontal world bearing: 0 is +X and 64 is +Y. Each victim retains the newest eight events for up to 32 ticks. Slot `(sequence - 1) % 8` makes overflow deterministic: a ninth new event replaces the oldest matching slot, so a sustained stream keeps its newest eight events rather than claiming an impossible 32-tick history. A single event with no newer collision stays for 32 ticks. Recipients get only their own victim row and attacker hit-marker row before the first encode; observers get neither. The client dedupes sequences and subtracts the presented camera yaw only for HUD display. It never derives direction from another player's replicated position. `damageFeedbackRevision` advances on every match reset, including map and scenario setup; the client clears its damage-event dedupe and fade state when that timeline changes, so a fresh setup may safely restart event sequences at one.

The bounded snapshot encoder drops rewind data, movement audio, beams, fire and blast visuals, frags, attacker hit feedback, then victim damage feedback. It never drops player, health, score, objective, match, configuration, or cached-roster correctness.

## Measured snapshot sizes

The table records compressed packet bytes from `lg_duel_protocol_tests` on the same fixtures. Projectile packets did not change.

| Fixture | Before | After | Change | Compressed | Optional blocks |
| --- | ---: | ---: | ---: | --- | --- |
| Gameplay duel | 485 | 383 | -102 (-21.0%) | yes | config, names, stats omitted |
| Configuration retry | 692 | 585 | -107 (-15.5%) | yes | config only |
| Name refresh | 485 | 447 | -38 (-7.8%) | yes | names only |
| Full duel | 972 | 929 | -43 (-4.4%) | yes | config, names, stats |
| Six-player | 490 | 386 | -104 (-21.2%) | yes | config, names, stats omitted |
| Active combat | 1,068 | 1,017 | -51 (-4.8%) | yes | config and names; combat events |
| Sixteen-player | 685 | 583 | -102 (-14.9%) | yes | config, names, stats omitted |
| Full retained damage ring | n/a | 445 | new | yes | eight victim events |
| Control command bundle | 1,140 | 1,122 | -18 (-1.6%) | no | control commands |
| Gameplay command bundle | 549 | 597 | +48 (+8.7%) | no | name-revision acknowledgement |
| Maximum projectile update | 1,173 | 1,173 | 0 | no | projectile updates |

One active victim event costs 8 payload bytes plus the 16-byte snapshot occupancy mask; a full eight-slot row costs 64 payload bytes. All player ammo saves 288 raw bytes. A fully acknowledged default name row saves about 146 raw bytes. The packed fixed boolean arrays save 98 raw bytes in this snapshot shape. Each active sparse event saves one payload activity byte.

We trialled a stateless `uint16_t` player-slot mask that wrote only connected or bot rows. It reduced the compressed duel fixture from 383 to 203 bytes, but changed the six-player fixture from 386 to 389 bytes and the sixteen-player fixture from 583 to 586 bytes. It also omitted a server-maintained waiting player state that a client may need before connection. We rejected the trial and kept dense, stateless player rows; the current sixteen-player fixture remains 583 bytes, well below the ceiling.

Arena data is intentionally revision-gated. `ClientGame::receiveSnapshots()` ignores snapshots with a new `mapRevision` unless `hasArena` is true. When an arena is received, the client caches it locally and clears `snapshot_.arena` before storing the snapshot to avoid carrying large static data in normal client state.

## Prediction, Reconciliation, Interpolation

Local prediction is in `src/client/Prediction.*`. The client pushes sent commands into a deque, simulates movement immediately with shared `simulateMovement()`, then removes acknowledged commands and replays the remaining commands when an authoritative snapshot arrives.

Remote interpolation is in `src/client/Interpolation.*`. It buffers up to 64 snapshot frames and samples remote `PlayerState`s at a presentation clock behind the newest snapshot. One controller owns startup reserve, presentation time, playback-rate correction, underrun holds, hard corrections, and presentation-aligned collision samples. Adaptive mode only supplies a bounded, smoothed target delay derived from accepted-newest snapshot jitter; duplicate and reordered arrivals do not affect that timing. A gap holds at the newest buffered state without extrapolation, and fresh covered history can trigger a discrete hard correction. Only player presentation is affected; authoritative simulation, lag-compensation tick bounds, and transient combat events remain server controlled.

UDP client telemetry measures accepted packet bytes, snapshot rate and age, inter-arrival jitter, missing and reordered snapshot ticks, smoothed RTT variation, and the command acknowledgement window. `cl_netgraph 1` presents a compact right-side panel; `cl_netgraph 2` adds bandwidth, packet sizes, the interpolation controller state, prediction and rewind diagnostics, plus a ten-second history graph. Red marks missing snapshots, yellow marks late/jittery delivery, purple marks a discrete buffer underrun, green marks a hard correction, orange marks outgoing loss, and blue marks prediction corrections.

Lag compensation is server-side. Commands include `viewedServerTick`; `ServerGame::tick()` clamps rewinds to `kMaxLagCompensationTicks` and traces hitscan/lightning against stored `HistoryFrame`s. Debug fields are included in `LightningGunResult`.

## Authoritative Vs Visual Only

Authoritative: health, damage, knockback, movement after reconciliation, projectiles, cooldowns, match phase, scores, teams, map revision, and accepted tuning.

Visual-only/client presentation: crosshair/hit marker fade, local damage numbers, weapon beam linger, muzzle visual offsets, HUD/audio playback, render interpolation, and debug overlays.

## Footguns

- Packet field order is the protocol. Add fields only by updating encode/decode/validation/tests and bumping `kProtocolVersion`.
- Avoid large strings or static data in per-tick packets. Arena/map data belongs on connect/map revision change, not every snapshot.
- Keep command validation strict; accepting non-finite or extreme values can destabilize simulation and rendering.
- Do not make clients authoritative for damage or projectile state. Client effects can predict or linger visually but must not alter gameplay state.
