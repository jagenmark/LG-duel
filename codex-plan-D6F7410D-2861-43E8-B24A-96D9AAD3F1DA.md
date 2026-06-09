# Grounded-First LG Duel Prototype, Flight-Compatible Architecture

## 1. Short Summary Of Target Architecture

Build a small C++20, CMake-based, Windows-first LG duel prototype with pure fixed-tick gameplay simulation separated from rendering, platform input, audio, and networking transport.

The first playable implements grounded Quake-like movement first, but all core gameplay state and networking must be compatible with future unrestricted equippable flight. No system should assume players are always floor-bound.

Core choices stay fixed:

- SDL3 for windowing, raw/relative mouse input, keyboard, and timing.
- Fixed `125 Hz` authoritative simulation.
- Pure `sim` module with no renderer/platform/socket dependency.
- Server-authoritative networking, loopback before UDP.
- Client prediction, reconciliation, snapshot interpolation.
- Lag-compensated Lightning Gun.
- Minimal dependencies, lightweight tests, small reviewable patches.

## 2. Proposed Directory Structure

```text
LG-duel/
  CMakeLists.txt
  CMakePresets.json
  AGENTS.md
  README.md

  cmake/
    Dependencies.cmake
    CompilerWarnings.cmake

  src/
    app/
    platform/
    shared/
      Math.hpp
      Types.hpp
      Constants.hpp

    sim/
      SimState.hpp
      PlayerState.hpp
      UserCommand.hpp
      Movement.hpp
      MovementModes.hpp
      Collision.hpp
      Combat.hpp
      Arena.hpp

    client/
      ClientGame.hpp
      Prediction.hpp
      Reconciliation.hpp
      Interpolation.hpp

    server/
      ServerGame.hpp

    net/
      NetProtocol.hpp
      NetTransport.hpp
      LoopbackTransport.hpp
      Snapshot.hpp

    render/
      Renderer.hpp
      DebugDraw.hpp

    audio/

  tests/
    sim/
      MovementTests.cpp
      CollisionTests.cpp
      CombatTests.cpp
      FixedTickTests.cpp
    net/
      CommandSequenceTests.cpp
      SnapshotInterpolationTests.cpp
      ReconciliationTests.cpp
```

Use `doctest` for lightweight unit tests unless the repo later standardizes on another test framework.

## 3. Flight-Compatible Core Model

Simulation tick:

- Server owns authoritative `SimState`.
- All gameplay advances at fixed `125 Hz`, `dt = 0.008s`.
- Rendering may run at any frame rate and only consumes interpolated presentation state.
- Render delta never affects authoritative movement, combat, collision, health, or networking.

Player state:

- `PlayerState` must include:
  - full `Vec3 position`
  - full `Vec3 velocity`
  - upright view orientation, including pitch/yaw
  - health
  - collision bounds
  - `MovementMode movementMode`
- Define from the start:

```cpp
enum class MovementMode {
    Grounded,
    Airborne,
    Flying
};
```

- Grounded and airborne modes are implemented first.
- `Flying` may be disabled by gameplay config until later, but it must be representable in state, snapshots, debug output, and movement dispatch.

User commands:

- `UserCommand` must support full 3D movement intent:
  - `forwardMove`
  - `rightMove`
  - ` upMove`
  - attack bit
  - jump/up intent where needed
  - command sequence
  - client tick or timestamp
  - view angles or angle delta
- Grounded mode maps these fields to Quake-like walking, strafing, jumping, and air movement.
- Future flying mode consumes the same fields as acceleration-based thrust:
  - W/S thrust along full camera forward/back direction, including pitch.
  - A/D thrust relative to view.
  - Space thrusts upward.
  - Ctrl/Shift thrusts downward.
  - Player remains upright by default; no roll or six-DOF flight yet.

Movement config:

- Use a `MovementConfig` / `MovementTuning` struct grouped by mode.
- Include grounded/airborne values now: acceleration, air acceleration, friction, jump impulse, gravity, max speed, knockback response.
- Reserve flying values now, even if unused initially: flight acceleration, max flight speed, damping/friction, vertical thrust scale, gravity cancel factor.
- LG knockback always applies to full `Vec3 velocity`, regardless of movement mode.

Movement dispatch:

- Prediction, reconciliation replay, server simulation, and tests all call the same movement interface.
- The interface dispatches by `MovementMode`.
- No caller should special-case grounded movement except for intentional gameplay rules.
- Mode transitions should be explicit: grounded from floor contact, airborne when not grounded, flying only when future equipped flight is active.

Collision and arena assumptions:

- Collision bounds must be full 3D from the start, preferably capsule or upright cylinder.
- Collision queries should support 3D sweeps/traces, not only 2D floor movement.
- The first arena can be simple and mostly flat, but code should allow vertical separation, tall spaces, and players circling above/below each other later.
- LG traces and player collision must not assume both players share a ground plane.

Networking and snapshots:

- Loopback transport uses the real protocol path before UDP exists.
- Snapshots must include:
  - full `Vec3 position`
  - full `Vec3 velocity`
  - `MovementMode`
  - health
  - view state
  - command ack / server tick
  - active LG state needed for interpolation/debug
- Client prediction replays local commands through the shared sim movement interface.
- Reconciliation applies authoritative position, velocity, movement mode, and command ack.
- Snapshot interpolation handles full 3D position and velocity for remote players.

Lightning Gun combat:

- Server performs authoritative 3D beam trace from shooter view origin and direction each fixed tick.
- Beam damage is continuous while attack is held.
- Hit detection uses target full 3D collision bounds.
- Lag compensation stores historical full 3D pose, velocity, movement mode, view state, and bounds.
- Vertical circling/flying later should not require rewriting LG hit detection.

Debug instrumentation:

- Show tick rate, frame time, command sequence/ack, snapshot rate, ping, prediction error, correction count.
- Show movement mode, full position, full velocity, grounded/contact status, and knockback impulse.
- For LG debug, show beam trace, hit/miss, target bounds, and lag-compensated historical target pose.

## 4. Milestone Plan

Milestone 0: Build skeleton

- Add CMake project, compiler warnings, SDL3 dependency setup, `doctest`, app target, and test target.
- Verify configure/build/test:
  - `cmake -S . -B build`
  - `cmake --build build`
  - `ctest --test-dir build --output-on-failure`

Milestone 1: Pure simulation sandbox

- Implement math/types, fixed tick helper, `SimState`, `PlayerState`, `UserCommand`, `MovementMode`, movement config, simple arena, full 3D collision bounds, grounded/airborne movement.
- Include `Flying` in enums, state, config, snapshots, and dispatch, but do not expose it as playable unless trivial.
- Add movement/collision tests without SDL or renderer dependencies.
- Add deterministic replay test for command sequences.

Milestone 2: Local playable input/rendering

- Add SDL3 window, raw relative mouse, keyboard input, and minimal renderer.
- Map keyboard/mouse into full `forward/right/up` command fields.
- Run one local player through fixed-tick sim with Quake-like grounded movement.
- Add debug overlay for raw mouse, tick timing, movement mode, position, and velocity.

Milestone 3: Duel combat through loopback networking

- Add `ServerGame`, `ClientGame`, `LoopbackTransport`, command packets, snapshots.
- Add two-player arena, LG beam, health, death/reset.
- Ensure all snapshots preserve full 3D position, velocity, and movement mode.
- Add combat and command sequencing tests.

Milestone 4: Prediction, reconciliation, interpolation

- Client predicts local player by replaying commands through the shared movement interface.
- Reconciliation corrects full 3D position, velocity, and movement mode.
- Remote players interpolate full 3D snapshots.
- Add tests for reconciliation, snapshot interpolation, and deterministic replay.

Milestone 5: UDP duel

- Add UDP transport behind `NetTransport`.
- Add handshake, ping, command bundling, snapshot sequencing, and simulated latency/loss/jitter.
- Validate two local processes before LAN.

Milestone 6: Lag-compensated LG

- Add server-side 3D pose history.
- Rewind target pose for command timing/latency.
- Add tests for 3D historical hit/miss and vertical separation cases.
- Add debug visualization for current versus rewound target bounds.

Milestone 7: Equippable unrestricted flight

- Implement `Flying` movement through the existing dispatch.
- Use existing `forward/right/up` command fields.
- Disable or strongly cancel gravity while flying.
- Apply acceleration/velocity-based thrust with no fuel, cooldown, duration limit, or hover ceiling.
- Preserve upright player orientation with pitch/yaw only.
- Reuse existing prediction, reconciliation, snapshots, collision, LG hit detection, and knockback.

## 5. Risks And Hard Parts

- Mouse feel is fragile; keep raw input simple, measurable, and smoothing-free by default.
- Quake-like movement feel depends on constants, collision behavior, and tick rate together.
- The main architectural risk is accidental grounded-only logic leaking into snapshots, reconciliation, LG traces, or collision.
- Continuous LG lag compensation is harder than click hitscan because damage happens across many ticks.
- UDP reliability can sprawl; keep protocol narrow and duel-specific.
- Do not drift into a general FPS engine: no pickups, weapon framework, inventory, map format, scripting, editor, or general entity system for v1.

## 6. Definition Of Done For First Playable Prototype

The first playable prototype is done when:

- Two players can duel in a simple arena through client/server architecture, initially loopback or localhost.
- Grounded Quake-like movement, jumping, air movement, friction, collision, and knockback work.
- Core data structures support `MovementMode { Grounded, Airborne, Flying }`.
- Player state, commands, snapshots, prediction, reconciliation, interpolation, LG hit detection, and debug instrumentation all preserve full 3D position and velocity.
- Raw/relative mouse input has stable sensitivity and no unwanted smoothing or acceleration.
- Both players spawn with full health and infinite LG ammo.
- Holding attack renders a continuous LG beam and server-authoritatively applies damage until one player dies.
- Simulation runs fixed at `125 Hz`; rendering is decoupled.
- Movement/combat tests run without launching the game.
- Tests exist or are scaffolded for future flight thrust, 3D knockback, full 3D snapshot/reconciliation, and deterministic replay.

Minimum validation:

```powershell
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Manual validation:

- Run one local duel session.
- Verify mouse feel in raw relative mode.
- Verify grounded movement/collision is stable across render frame rates.
- Verify LG damage/death is authoritative.
- Verify debug output reports prediction, snapshots, movement mode, position, velocity, and LG hit state.

## Assumptions Chosen

- Use C++20, CMake, SDL3, and `doctest`.
- Start with grounded/airborne Quake-like movement.
- Keep `Flying` disabled or unplayable initially, but fully represented in architecture.
- Start with simple generated geometry and minimal OpenGL rendering.
- Use loopback networking before UDP while preserving the final command/snapshot architecture.
