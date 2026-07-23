# Networking

Networking is UDP-oriented and snapshot based. Packet structures live in `src/net/NetProtocol.hpp`; binary layout and validation live in `src/net/NetCodec.*`; transport/session behavior is in `src/net/UdpTransport.*`, `src/client/ClientSession.*`, `src/client/ClientGame.*`, and server code in `src/server/ServerGame.*`.

## Packets And Protocol

`NetCodec.hpp` defines `kProtocolMagic`, `kProtocolVersion` (`56` at this writing), `kMaxPacketBytes`, the 1,200-byte UDP application-datagram ceiling, and `PacketType`. Every packet has a fixed header: magic, version, type, flags, payload byte count, and reserved field. The codec rejects wrong versions, invalid enum values, non-finite floats, out-of-range tuning values, invalid strings, malformed sparse masks, invalid compression back-references, inconsistent expansion lengths, and trailing bytes.

Snapshot and combat-stat payloads use a deterministic, stateless 4 KiB-window compressor when it reduces their size. Compression is lossless: authoritative player state, command acknowledgements, ammo, and slot indices retain their exact wire values. Snapshot encoding, UDP receive, and the UDP send boundary enforce 1,200 bytes, so the application never depends on IP fragmentation. An event-heavy snapshot is retried in fixed priority order without rewind diagnostics, movement audio, recipient-irrelevant hit-feedback windows, recurring beam visuals, and finally lower-priority transient combat visuals; player, projectile, objective, score, and match state are never discarded to make room. If that authoritative core still cannot fit, encoding fails closed and the transport reports an error instead of sending a partial or fragmented core.

Supported packet types are connect request/accept, command, command bundle, snapshot, combat statistics, ping/pong, disconnect, chat history, and chat-history acknowledgement. `CommandBundle` carries up to 12 compact commands (about 96 ms at 125 Hz), shares client identity and cumulative action edges once, and is trimmed from the oldest command when a rare control payload would exceed the 1,200-byte datagram budget. Attack, jump, dash, reset, ready, and McGuffin-throw edges are deduplicated server-side; attack and throw edges retain their original aim.

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

Every command bundle also carries a transport datagram sequence. Per-client snapshots return the newest accepted sequence and a 32-bit acknowledgement mask for the preceding datagrams. This adds four bytes to command bundles and eight bytes to gameplay snapshots, and lets the client measure upstream loss independently from command recovery through redundant bundles.

The optional client-carried `g_*` tuning path is a temporary development affordance. Server startup defaults for those values come from `config/server_cvars.cfg`, while non-cvar authoritative balance comes from server-side `config/balance.cfg`. Clients must not load local `balance.cfg` for gameplay authority.

## Snapshot Ownership

`ServerSnapshot` is authoritative for player states, selected weapons, lightning results, weapon fire events, projectile/explosion events, freeze-gun ice pools, footsteps, frags, scores, teams, match phase/rules, cvar-derived gameplay tuning, map revision, and optional arena data. Chat history is authoritative server state but uses its separate acknowledged packet stream.

Inactive fixed-capacity event and projectile slots are represented by multiword occupancy masks; unused high bits are invalid rather than aliases for future slots. The per-player local-hit-feedback window is likewise a multiword mask and covers all four events for all sixteen players. Gameplay snapshots do not carry scoreboard combat aggregates over UDP. Instead, every command repeats whether that client currently has the scoreboard open; a closed-to-open transition sends four independently bounded four-player statistics pages immediately, followed by 5 Hz refreshes only to that client while the scoreboard remains open. The client stages pages by server tick and publishes a new aggregate only after all sixteen player rows arrive, so diverse valid counters never require fragmentation or leave a partially updated scoreboard. Gameplay configuration has its own revision: snapshots carry that revision, commands acknowledge the latest configuration installed by the client, and the server repeats the full configuration block per client until the matching acknowledgement arrives. A client never applies a lean snapshot for an unknown configuration revision.

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
