# Networking

Networking is UDP-oriented and snapshot based. Packet structures live in `src/net/NetProtocol.hpp`; binary layout and validation live in `src/net/NetCodec.*`; transport/session behavior is in `src/net/UdpTransport.*`, `src/client/ClientSession.*`, `src/client/ClientGame.*`, and server code in `src/server/ServerGame.*`.

## Packets And Protocol

`NetCodec.hpp` defines `kProtocolMagic`, `kProtocolVersion` (`33` at this writing), `kMaxPacketBytes`, and `PacketType`. Every packet has a fixed header: magic, version, type, flags, payload byte count, and reserved field. The codec rejects wrong versions, invalid enum values, non-finite floats, out-of-range tuning values, invalid strings, and trailing bytes.

Supported packet types are connect request/accept, command, command bundle, snapshot, ping/pong, and disconnect. `CommandBundle` can carry up to `kMaxBundledCommands` commands.

## Command Ownership

Clients own input intent: movement axes, view angles, attack/jump/weapon request, ready/reset/team/game-mode requests, chat/name/map requests, and optional cvar/tuning values. The server owns acceptance. `ServerGame::receiveCommands()` ignores stale command sequences using `isSequenceNewer()`, stores `viewedServerTick` for lag compensation, and writes acknowledged sequence state into the snapshot.

The optional client-carried `g_*` tuning path is a temporary development affordance. Server startup defaults for those values come from `config/server_cvars.cfg`, while non-cvar authoritative balance comes from server-side `config/balance.cfg`. Clients must not load local `balance.cfg` for gameplay authority.

## Snapshot Ownership

`ServerSnapshot` is authoritative for player states, selected weapons, lightning results, weapon fire events, projectile/explosion events, footsteps, frags, scores, teams, match phase/rules, cvar-derived gameplay tuning, chat state, map revision, and optional arena data.

Arena data is intentionally revision-gated. `ClientGame::receiveSnapshots()` ignores snapshots with a new `mapRevision` unless `hasArena` is true. When an arena is received, the client caches it locally and clears `snapshot_.arena` before storing the snapshot to avoid carrying large static data in normal client state.

## Prediction, Reconciliation, Interpolation

Local prediction is in `src/client/Prediction.*`. The client pushes sent commands into a deque, simulates movement immediately with shared `simulateMovement()`, then removes acknowledged commands and replays the remaining commands when an authoritative snapshot arrives.

Remote interpolation is in `src/client/Interpolation.*`. It buffers up to 64 snapshot frames, advances a presentation tick behind the newest snapshot, clamps drift, and samples remote `PlayerState`s between frames. Only player states are interpolated there; transient combat visuals come from snapshot event arrays and app-side lingering presentation.

Lag compensation is server-side. Commands include `viewedServerTick`; `ServerGame::tick()` clamps rewinds to `kMaxLagCompensationTicks` and traces hitscan/lightning against stored `HistoryFrame`s. Debug fields are included in `LightningGunResult`.

## Authoritative Vs Visual Only

Authoritative: health, damage, knockback, movement after reconciliation, projectiles, cooldowns, match phase, scores, teams, map revision, and accepted tuning.

Visual-only/client presentation: crosshair/hit marker fade, local damage numbers, weapon beam linger, muzzle visual offsets, HUD/audio playback, render interpolation, and debug overlays.

## Footguns

- Packet field order is the protocol. Add fields only by updating encode/decode/validation/tests and bumping `kProtocolVersion`.
- Avoid large strings or static data in per-tick packets. Arena/map data belongs on connect/map revision change, not every snapshot.
- Keep command validation strict; accepting non-finite or extreme values can destabilize simulation and rendering.
- Do not make clients authoritative for damage or projectile state. Client effects can predict or linger visually but must not alter gameplay state.
