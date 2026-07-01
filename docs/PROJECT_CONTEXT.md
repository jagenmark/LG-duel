# LG Duel — Project Context

## What this project is

LG Duel is currently a playable competitive arena-FPS reference game. It is not intended to remain only a small Lightning Gun duel prototype, nor is it intended to become a general-purpose engine comparable to Unity or Unreal.

The project is building a reusable, high-performance foundation specifically for competitive networked FPS games.

LG Duel is the testbed where core systems are implemented, measured, validated, and refined through real gameplay.

## Long-term direction

The long-term goal is to build an original game in the space between arena FPS and hero shooter.

The intended game should preserve:

* mechanical expression, movement, aiming, weapon mastery, and readable combat from Quake-like arena FPS games;
* role variety, body archetypes, abilities, weapon choices, and team composition possibilities associated with hero shooters;
* strong visual readability and a deliberate low-poly / stylized visual direction;
* competitive responsiveness rather than cinematic presentation.

The eventual game is not intended to be a Quake clone, a direct Quake Live replacement, or a generic hero shooter. LG Duel exists to establish the systems needed to make those later design choices possible.

## Core technical priorities

The project prioritizes:

1. Low input latency.
2. Stable frame pacing, not merely high average FPS.
3. Predictable p95 and p99 frame times.
4. Authoritative server simulation with responsive client prediction.
5. Clear, understandable networking and hit registration.
6. Testable deterministic gameplay systems where practical.
7. Performance telemetry before performance claims.
8. Rendering systems that scale beyond the current duel prototype.
9. Debuggability and developer tooling as first-class features.
10. Architecture that supports future weapons, projectiles, bodies, abilities, maps, teams, and game modes.

## Implementation principles

### Reusable systems over local special cases

When a feature represents a recurring category of game content, prefer a reusable framework over a weapon-specific or one-off implementation.

Examples:

* Repeated projectiles should converge toward shared mesh/sprite assets plus instance data.
* Weapons should converge toward mesh assets, sockets, transforms, and descriptors.
* Repeated renderable objects should use shared GPU resources and instance rendering where appropriate.
* Visibility should become a shared stage for world chunks and render instances.
* New content should preferably be registered through data/descriptors rather than added through large chains of special-case conditionals.

Do not create abstraction for its own sake. A reusable system should be proportionate to the feature and have a clear likely second use case.

### Separate domains of responsibility

Keep these areas separate:

```text
Gameplay simulation:
  authoritative rules, collision, weapons, damage, movement, match state

Networking:
  commands, snapshots, interpolation, prediction, reconciliation

Rendering:
  visual transforms, interpolation, meshes, materials, instances, effects

Assets:
  models, textures, sounds, map source data, visual metadata
```

Rendering must not become gameplay authority. Render-model animation, weapon sockets, glow effects, and visual projectile placement may improve presentation, but must not silently alter collision, damage, hit registration, or authoritative simulation.

### Performance means architecture as well as profiling

Use telemetry to identify immediate bottlenecks, but do not use measurements as a reason to preserve architecture that cannot support planned systems.

A local tactical fix is appropriate for:

* correctness bugs;
* crash fixes;
* obvious accidental allocations or copies;
* temporary developer tooling;
* isolated regressions.

New feature work should have a clear migration path toward the intended architecture.

Avoid broad rewrites without a working vertical slice proving the new path. Prefer:

```text
shared foundation
→ one real user of the foundation
→ validation and telemetry
→ migrate similar systems
```

### Preserve competitive feel

When making tradeoffs, protect:

* input responsiveness;
* stable pacing;
* predictable networking;
* readable combat feedback;
* consistent movement;
* reliable hit feedback;
* visual clarity under pressure.

A visually impressive change that adds unstable frame times, input delay, excessive visual noise, or ambiguity in combat is not automatically an improvement.

## Renderer direction

The current renderer contains prototype and transitional paths. They are useful current implementations, not necessarily the final architecture.

The desired direction is:

```text
Static world:
  cached GPU-resident world meshes
  → later chunked world meshes and map visibility data

Repeated static objects:
  shared mesh/sprite assets
  + compact per-instance transforms and material parameters

Third-person weapons:
  reusable static mesh assets
  + hand sockets
  + weapon grip/muzzle metadata

First-person weapons:
  separate viewmodel assets
  + separate viewmodel render pass
  + independent recoil/sway/FOV behavior

Player bodies:
  low-poly skinned mesh assets
  + lightweight animation state
  + GPU skinning/bone palettes

Projectiles:
  reusable mesh and billboard assets
  + dynamic instance buffers
  + separate opaque and glow/effect passes

Outlines:
  transition away from procedural wire/geometry outlines
  toward mask, stencil, or screen-space silhouette outlines

Visibility:
  frustum culling first
  → later map-level cells, portals, PVS, or other suitable visibility systems
```

The intended per-frame model is:

```text
game state and interpolation
→ compact render instances
→ visibility filtering
→ batching by mesh/material/pass
→ GPU draw submission
```

The intended model is not:

```text
every visible object
→ generate fresh CPU triangles
→ append to a large dynamic vertex list
→ upload the same mesh data again every frame
```

Transient effects such as beams, tracers, impact sparks, debug geometry, and short-lived trails may continue using a dynamic geometry path where appropriate.

## Current transitional systems

| Current area         | Current/prototype tendency             | Intended direction                                      |
| -------------------- | -------------------------------------- | ------------------------------------------------------- |
| Player rendering     | Procedural or CPU-expanded geometry    | GPU-resident body mesh assets, animation data, skinning |
| Held weapons         | Weapon-specific procedural geometry    | Static mesh assets, sockets, transforms, descriptors    |
| Projectiles          | Per-frame generated primitive geometry | Shared mesh/billboard assets and instance batches       |
| Player outlines      | Geometry/wireframe fallback            | Mask/stencil/screen-space outlines                      |
| Dynamic scene        | Per-frame vertex expansion             | Render-instance lists plus transient-effect geometry    |
| Visibility           | Narrow per-object checks               | Shared visibility stage for world and dynamic instances |
| Maps                 | Cached static geometry                 | Later chunk/cell-aware world visibility architecture    |
| Content registration | Conditional logic in rendering code    | Data-driven descriptors and asset registration          |

These transitional systems are not failures. They were appropriate for early prototyping. New work should avoid deepening dependency on them when a planned replacement architecture is relevant.

## Design constraints for future game systems

The eventual game should be able to support, without fundamental rewrites:

* multiple player body archetypes with different speed, size, health, mobility, and resistances;
* multiple teams and modes;
* varied weapons with distinct visuals and projectile behavior;
* abilities, utility tools, healing, traversal, area control, and defensive mechanics;
* loadout, economy, upgrade, or build-choice systems;
* maps with rooms, corridors, verticality, visibility boundaries, and more complex geometry;
* high-refresh-rate competitive play;
* real playtesting, debugging, telemetry, and balance iteration.

Do not prematurely build every one of these systems now. Build current systems so that these directions remain possible.

## How to approach substantial work

For a substantial task, first identify:

1. Which domain the change belongs to.
2. Whether it is a one-off bug fix or a recurring content/system category.
3. The likely next user of the abstraction.
4. The current transitional implementation, if one exists.
5. The intended target architecture.
6. What can be delivered as a small vertical slice without locking in a dead-end design.
7. Which telemetry, tests, or debug tools are needed to validate it.

For architecture, rendering, networking, performance, or content-pipeline changes:

* prefer a reusable foundation where there is a credible recurring use case;
* keep the first migration small and playable;
* preserve existing behavior unless the task explicitly changes it;
* add tests around correctness boundaries;
* add diagnostics where performance or timing is relevant;
* document meaningful architectural decisions in code comments or architecture docs.

## Decision rule

When choosing between a quick local patch and a reusable design:

```text
Use the local patch when:
- the problem is isolated;
- the code is temporary developer tooling;
- the fix is correctness-critical and narrowly scoped;
- generalization would be speculative.

Use a reusable design when:
- the feature represents a recurring category;
- future content will clearly need the same pipeline;
- the local solution would force repeated geometry generation, duplicated data,
  large hot-path copies, or growing special-case branches;
- the task is part of rendering, assets, projectiles, weapons, models,
  visibility, networking infrastructure, or performance architecture.
```

## Current source of truth

This file defines the intended scope, architectural direction, and implementation principles for LG Duel.

Current code may contain older prototype assumptions that conflict with this document. When that happens, treat the code as the current implementation and this document as the direction future work should converge toward.
