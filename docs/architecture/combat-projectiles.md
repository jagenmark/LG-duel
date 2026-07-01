# Combat And Projectiles

Combat logic is split between shared trace/simulation helpers in `src/sim/Combat.*` and authoritative application in `src/server/ServerGame.*`. The server decides whether a command may fire, computes targets, applies cooldowns, damage, knockback, vampirism, frag events, and projectile state.

## Hitscan Weapons

Lightning Gun, Railgun, Machine Gun, and Shotgun use world/player traces. `traceWorld()` clips against arena bounds, cuboid walls, and convex brushes. `tracePlayerCylinder()` tests player collision cylinders.

`simulateLightningGun()` uses `LightningGunState` to accumulate shot credit and fractional damage based on `fireHz` and fixed dt. Railgun and Machine Gun produce single `WeaponFireResult`s. Shotgun uses deterministic pellet directions and reports pellet count/hit count.

Server-side targeting first traces the world, then tests candidate players up to the nearest world hit. For received attack commands, the target can be rewound through `historyFrameForTick()` using the client-presented `viewedServerTick`, clamped by `kMaxLagCompensationTicks`.

## Projectile Weapons

Rockets, grenades, and plasma share `RocketProjectile` storage (`kMaxRocketProjectiles`). `spawnProjectile()` allocates a free slot; `simulateRockets()` advances active projectiles each tick.

- Rocket Launcher: straight projectile, explodes on world/player hit or lifetime expiry.
- Grenade Launcher: gravity, bounce damping, optional resting state, fuse, direct-hit radius from config, bounce audio events, and explosion on fuse.
- Plasma Gun: fast projectile with shorter lifetime and smaller splash/direct tuning.

Projectiles snapshot as `RocketProjectileSnapshot` with active flag, owner, weapon, position, velocity, and radius. Visual mesh choice is renderer-side.

## Damage, Knockback, And Healing

`applyDamageAndKnockback()` gates damage by combat phase and `damageAllowed()`, clamps damage to target health, applies velocity impulse, emits local hit feedback, heals attackers through `vampirism_`, and emits frag/round-end state when applicable. Self-damage uses `selfDamagePercent_` in projectile explosion handling before final application.

Knockback tuning mixes Quake-style values converted by `q3KnockbackToInternal()` with weapon-specific tuning structs. Lightning has a separate remap through `lightningKnockbackToInternal()`.

## Transient Events

Combat visuals/audio are snapshot events, not independent gameplay systems: `WeaponFireResult`, `LightningGunResult`, `RocketExplosionResult`, `FootstepAudioEvent`, `GrenadeBounceAudioEvent`, `FragEvent`, and `LocalHitFeedbackEvent`. The server retains recent events briefly so packet loss does not make effects disappear immediately.

## Invariants And Footguns

- Cooldowns are authoritative server counters; client display should not decide whether a shot happened.
- Projectile slots are bounded. Do not add unbounded active projectile/event lists.
- Owner collision for projectiles arms only after the projectile leaves the owner cylinder, avoiding immediate self-hit on spawn.
- Grenade direct hits can be disabled by `projectile_hitbox_radius = 0`.
- Shotgun pellet visuals are derived from deterministic result data; do not network every pellet unless there is a gameplay reason.
- If adding weapons, document cooldowns, damage source, snapshot fields, prediction assumptions, and packet-size impact.
