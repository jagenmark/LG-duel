# Server Tick

`src/server/ServerGame.cpp` is the authoritative simulation loop. `ServerGame::tick(float fixedDt)` consumes network input, advances match/gameplay state, runs movement and combat, simulates projectiles, retains short-lived events, increments `serverTick`, records history, and publishes a snapshot.

## Lifecycle

1. `receivedCommandThisTick_` is cleared and `receiveCommands()` drains the transport.
2. `updateMatchState()` advances waiting, ready, countdown, live, round-end, and match-end phases.
3. `updateBotCommands()` fills commands for bot-controlled participant slots.
4. One-tick snapshot event arrays are cleared: weapon fires, explosions, footsteps, grenade bounces, frags, and local hit feedback.
5. Weapon cooldowns and pullout timers are decremented.
6. For each player, the selected weapon is updated and `simulateMovement()` runs for living players, including bounded jumppad trigger checks after arena collision.
7. Player/player collision is resolved, then player/arena collision is resolved, then footstep events are generated.
8. Hitscan/lightning targeting uses a copy of pre-damage `combatPlayers`; optional lag compensation selects a stored `HistoryFrame`.
9. Hitscan weapon results are generated, cooldowns are set, and damage/knockback is applied through `applyDamageAndKnockback()`.
10. `simulateRockets()` advances rockets, grenades, and plasma projectiles, handles impacts/bounces/fuse/lifetime, applies splash/direct damage, and prepares bounded projectile display updates.
11. Live match timers and time-limit ending are updated.
12. `rememberTransientCombatEvents()` stores fresh events, `restoreTransientCombatEvents()` replays recent events for a short receive window, `serverTick` increments, `recordHistory()` stores players, and `publishSnapshot()` sends the snapshot.

## Important Source

- `ServerGame::receiveCommands`, `tick`, `updateMatchState`, `respawnRound`, `recordHistory`, `historyFrameForTick`, `simulateRockets`, `applyDamageAndKnockback`, `rememberTransientCombatEvents`, `restoreTransientCombatEvents`, `publishSnapshot`
- Shared helpers: `simulateMovement`, `resolvePlayerCollision`, `resolvePlayerArenaCollision`, `traceWorld`, `tracePlayerCylinder`
- Rules helpers: `DuelRules.*`, `ClanArenaRules.*`

## Ordering Requirements

- Commands must be received before match state, bot commands, movement, and combat so authoritative state reflects the latest accepted input.
- Movement and collision run before combat; hit traces use post-movement positions, while `combatPlayers` freezes positions for consistent per-attacker combat resolution during the tick.
- Player-vs-arena wall movement uses a bounded pmove-style slide over arena bounds and box walls with fixed bump and plane limits. Grounded stair traversal layers a step-up, horizontal slide, then drop-down retry over that move only when the normal move loses horizontal progress; sidestep probing is intentionally not part of this model.
- Arena collision runs after player/player collision to clamp final positions back into valid space.
- Jumppad launch is server-authoritative movement state. Trigger cooldown comes from `balance.cfg`, runs as a fixed tick countdown on player state, and is not added to per-tick network packets.
- `recordHistory()` happens after `serverTick` increments so lag compensation can find a frame by authoritative tick number.
- Transient events are remembered before restore; restoring after simulation keeps short-lived events visible across packet loss without making them persistent gameplay state.

## Footguns

- Do not put rendering, GPU work, asset loading, or map mesh generation in the server tick.
- Do not add unbounded per-player/projectile/event containers. Current arrays are fixed-size around `kDuelPlayerCount`, `kMaxRocketProjectiles`, and small event windows.
- If a new event must survive packet loss, add explicit retention/sequence behavior like the existing recent event arrays.
- If command semantics change, update snapshot acknowledgements and client prediction/reconciliation assumptions together.
- Map changes call `setArena()`, bump `mapRevision_`, reset the match, clear history, and require clients to receive the arena before accepting later snapshots.
