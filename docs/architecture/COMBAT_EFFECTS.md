# Combat effects

## Scope

The combat-effects path is client presentation code. It consumes already accepted `WeaponFireResult` and explosion events and never changes weapon timing, traces, damage, movement, prediction, or server state. The machine gun and Rocket Launcher use it.

## Event and render flow

```text
replicated or predicted WeaponFireResult
  -> fixed fire-event history keyed by player, weapon, and visual seed
  -> current weapon socket from the viewmodel or remote pose
  -> typed MachineGunShotEffectsRequest or RocketLauncherShotEffectsRequest
  -> fixed CombatEffects pools and visual simulation
  -> TransientEffect render records
  -> Scene3D batches, temporary-light list, and GPU uniforms

authoritative explosion event
  -> fixed owner and sequence history
  -> typed RocketExplosionEffectsRequest
  -> short seeded particles in the same fixed pools
```

The system does not read rendered weapon parts to decide whether a shot occurred. The accepted fire event remains the visual event source. The same replicated event path creates remote effects. Rocket projectile position and velocity still come from the authoritative snapshot.

## Authored machine-gun motion and sockets

The machine gun keeps its existing Blender-authored `MG_BARREL_REFERENCE_LOOP`, `MG_BARREL_SPIN_AXIS` hierarchy, imported pivot, and runtime presentation playback. This pass does not add another barrel angle or alter gameplay cadence.

The GLB exports these stable points:

- `MG_MUZZLE_SOCKET` sits at the common front ring. The model fires from the cluster centre, so it does not need a per-tube active muzzle.
- `MG_CASING_EJECT_SOCKET` sits on the fixed receiver and does not rotate with the barrel cluster.

First-person socket transforms use the same weapon-position, field-of-view, sway, recoil, vibration, and view basis as the viewmodel. Remote sockets use the same held-weapon frame as the remote model. Short muzzle lights keep an owner index and update from the current muzzle point each rendered frame.

The Rocket Launcher uses its existing `ROCKET_MUZZLE_SOCKET`. Its compact flash and light follow a separate Rocket Launcher attachment point, so a machine-gun update cannot move them. The local point uses view sway, recoil, and mechanism motion. The remote point uses the current pose, lean, and mechanism motion.

## Pools and budgets

`CombatEffects` owns fixed arrays:

| Effect | Hard capacity | Shipped active limit |
| --- | ---: | ---: |
| Temporary lights | 16 | 16 |
| Casings | 96 | 48 |
| Muzzle and impact particles | 384 | 192 |
| Bullet decals | 256 | 128 |

Each configured limit clamps to the hard capacity. A full pool reuses its oldest entry in a stable serial order. No shot grows a pool. The output vector reserves all hard capacities once. `r_combat_effects 0` clears the secondary pools. The Rocket Launcher's base muzzle, opaque projectile, rear flame, impact flash, and impact core stay visible at that setting.

The visual seed controls optional muzzle sparks, particle spread, casing motion, decal scale, and decal angle. This makes repeated tests stable without adding state to gameplay packets.

## Effect layers

A machine-gun shot uses the existing directional muzzle flash and additive core, plus a short muzzle light, a small smoke puff, an optional spark, and a casing. World hits add a normal-directed spark burst, short flash, faint dust, and a surface-aligned mark with a depth offset.

A Rocket Launcher shot uses a short pale-yellow core inside a coral exhaust shape. Its projectile keeps one opaque, velocity-aligned rocket mesh and a bounded rear flame. Its impact uses a fast pale core and short coral body. The quality tiers only change the small supporting layers:

| `r_combat_effects` | Rocket Launcher layers |
| ---: | --- |
| `0` | Base muzzle core and exhaust, one rear flame, impact flash and core |
| `1` | Base layers, restrained muzzle halo and light, inner rear flame, faint impact ring |
| `2` | Medium layers, one muzzle smoke puff, up to four seeded impact shards, one impact smoke puff |

Bloom is not part of the readability rule. The base colours and opaque rocket remain clear when bloom is off. Rocket particles stay in the shared fixed pool and expire in at most 270 ms.

The impact request supports generic hard, metal, stone, and energy categories. The first slice uses the generic hard fallback because the current world trace does not return a stable material category. Player hits do not create world decals.

Temporary lights reach the static world and authored weapon material shaders through one small fixed uniform block. They do not cast shadows. The world and weapon material paths apply a fixed filmic tone map. Bright additive effect sprites use a high-threshold compact glow response; the HUD does not use that shader.

## Lifetime and cleanup

Simulation uses elapsed render time, capped to 250 ms per update after a long pause. Casings and sparks use gravity; smoke and dust use drag. Every entry has a fixed lifetime. New game, map load, session reset, and disabled combat effects clear all pools.

Scene diagnostics and the `r_perf` sample record active and peak lights, casings, particles, decals, and submitted transparent effects. Benchmark frame files also record light, particle, and transparent-effect counts. These counts measure presentation only.

## Backends and builds

The SDL GPU path draws the new effect layers, lights, tone map, and compact glow. The compatibility renderer keeps its current safe output and ignores unsupported GPU work. The fixed-pool code does not link into the dedicated server, so headless authority stays unchanged.

## Current limits

- Casings have gravity and tumble but no static-world bounce in this pass.
- World material classification remains the generic hard fallback.
- Decals are surface-aligned quads; they do not clip against sharp mesh edges or follow moving entities.
- The compact glow is an effect-sprite response, not a full-screen HDR blur pass.
- Automatic exposure, contact shadows, ambient occlusion, atmospheric depth, and rim lighting remain separate work.
