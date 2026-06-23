# AGENTS.md

Guidance for agents working on this repository.

## Project Goal

Build a narrow Quake-like lightning-gun duel prototype. This is not a general FPS engine. Optimize for a tight, testable 1v1 LG duel loop: movement, aiming, continuous beam combat, prediction/reconciliation, and clear instrumentation.

Avoid broad engine features unless they directly serve that prototype.

## Architecture Principles

- Keep gameplay simulation separate from rendering.
- Use a fixed-tick simulation for movement, combat, input processing, and networking state.
- Treat rendering as a view over simulation state, not the source of gameplay truth.
- Keep systems small and explicit: movement, input, combat, networking, and rendering should have clear boundaries.
- Prefer deterministic, data-oriented simulation code where practical.
- Prefer small, reviewable patches over large rewrites.
- Do not introduce abstractions for imagined future FPS features.

## Build And Test Commands

Use the repository's checked-in build system. This repository has CMake presets,
and those presets are authoritative for local builds. Do not use the top-level
`build/` directory as the local build tree; the default preset writes to
`build/default`.

```powershell
cmake --preset default
cmake --build --preset default
ctest --preset default
```

Equivalent plain CMake commands are documented in `README.md` and should use
`build/default`, not `build`. For focused iteration, prefer building and running
the smallest relevant test target using the preset build tree.

## Dependency Policy

- Keep dependencies minimal and purposeful.
- Prefer standard C++ and small, well-maintained libraries over large frameworks.
- Do not add engine-scale dependencies unless they directly support the duel prototype.
- Keep gameplay simulation free of renderer, platform, and windowing dependencies.
- Vendor or pin dependencies when reproducibility matters.
- Document any new dependency with its purpose, ownership risk, and build impact.

## Coding Conventions

- Write clear, modern C++ with explicit ownership and value semantics where possible.
- Keep simulation code deterministic and independent of wall-clock frame time.
- Prefer plain data structures for hot simulation state.
- Keep units visible in names or types where ambiguity is likely.
- Avoid hidden global state in gameplay, input, combat, and netcode paths.
- Use concise comments for non-obvious math, physics, prediction, or protocol behavior.
- Match existing formatting and naming once project conventions are established.

## Physics, Input, And Netcode Priorities

- Movement should feel Quake-like before it becomes feature-rich.
- Input should be sampled, stored, and consumed in fixed-tick-friendly structures.
- Combat should be authoritative in simulation code, with beam traces and damage rules testable without rendering.
- Networking work should prioritize prediction, reconciliation, snapshot clarity, and instrumentation over feature breadth.
- For movement, input, combat, and networking changes, add tests or instrumentation where practical.
- Any variable-frame interpolation should live outside authoritative simulation.

## Validation Expectations

- Run the relevant build and tests before handing off changes.
- Add or update tests for simulation behavior whenever practical.
- Add instrumentation for timing, prediction errors, hit registration, or packet/snapshot behavior when tests are insufficient.
- Validate fixed-tick behavior separately from rendering behavior.
- For visual changes, verify the app manually or with screenshots when possible.
- If something was not verified, say so plainly.

## Final Response Expectations

Every final response for code changes must include:

- Changed files.
- Tests run.
- Anything not verified.

Keep the summary brief, practical, and focused on what changed and how it was validated.
