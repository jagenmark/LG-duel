# Glide Duel checkpoint — 2026-09-12

Status: draft PR checkpoint, paused at the user's request. Gameplay implementation
is in place; the remaining work below must be reviewed before declaring the mode
finished or ready to merge. Last implementation commit: `c935a9f`.

## Implemented

- First-to-seven Glide Duel, 45-second rounds, mirrored arena, slide carving and
  braking, bot slide control, and authoritative round respawn/view resets.
- Shared round conditions rotate through FLOAT (30% lower gravity), BLAST (50%
  stronger enemy knockback), and RUSH (40% shorter dash cooldown), including draws.
- Contested SURGE, GUARD and REPAIR pickups at 8/20/32 seconds, announced three
  seconds early. Ownership, expiry, collection and healing are server-authoritative.
- Final 15-second Overdrive doubles enemy damage and closes a visible circle;
  players outside lose eight HP per second. Pickups and the circle require a
  location reachable from both actual round starts.
- Bots prioritize announced pickups while retaining legal combat and move toward
  the circle interior. No hidden opponent positions enter their objective sense.
- HUD condition descriptions, pickup previews/claims, buff timers and zone warning;
  color-coded rotating world markers and circle geometry.
- Snapshot/replay state, gravity replication, validation and deterministic hashes.
  Protocol 67, replay format 9, simulation revision 4 require matching builds.
- Earlier requested presentation work is included: Pip/Fuse and Ratch runtime
  model support, slide trail/animation polish, shared cyan HP HUD and offsets,
  headshot-aware killcam/kill feed, and supporting playtest tooling.

## Verified at this checkpoint

- SDL client and server build successfully.
- 17 focused CTest targets pass on the combined branch: Glide chaos and full
  Glide server/bot soak; movement, general server, bot server, FFA server, client,
  protocol, replay codec/playback, client killcam coordinator, client cvars,
  miscellaneous menu, HUD presentation, client session, ScreenUi and Scene3D.
- The new chaos test exercises all three condition profiles, pickup spawn timing,
  one-owner collection, buff expiry and hit scaling, REPAIR cap, eight-HP zone
  damage, simultaneous zone-death draws, draw rotation, and replay/seek equality
  across pickups and a round boundary.
- Fixed two failures found during integration: FLOAT gravity was missing from the
  snapshot wire configuration, and GUARD was applied after an earlier health cap,
  allowing low-health targets to survive overkill. Both regressions now pass.
- A real SDL_GPU/Vulkan client on Intel Graphics (LNL), Mesa 26.2.2 rendered the
  FLOAT HUD and live SURGE world marker correctly. Local capture:
  `build/captures/glide-chaos-live.png` (development-camera view, not a human
  gameplay evaluation).
- `git diff --check origin/main` is clean. This is focused coverage, not a claim
  that the entire repository test suite or packaging/CI has passed.

## Remaining work

1. **Play and tune the loop.** Evaluate pickup contest frequency, buff strength,
   round variety and circle pressure with real movement and combat. The bot soak
   completes, but does not yet assert pickup collection counts or sustained zone
   convergence. Enjoyment and balance have not been established by human play.
2. **Finish live presentation verification.** Capture actual pickup collection,
   active-buff expiry, BLAST/RUSH rounds and the shrinking zone/outside warning in
   normal first-person play. Assess marker visibility through busy fights and the
   clarity of the final circle. Only FLOAT + a live SURGE marker has been visually
   inspected in the combined build.
3. **Add map edge-case coverage.** Exercise maps with more than two rotating
   spawns and disconnected spawn components. Verify actual-round reachability and
   safe objective disablement with server tests, beyond the existing HUD check.
4. **Review delayed tuning requests across round changes.** The server currently
   recognizes an echoed RUSH dash cooldown by comparing it with the current
   effective snapshot. Add a regression for an old RUSH request arriving after
   the next condition starts; this may require revision-aware normalization or
   explicit separation of configured and effective values on the request path.
5. **Finish PR-wide checks.** Run the Python playtest/launcher/animation-tool
   suites, package checks and CI for the complete carried-forward presentation
   work. Review the broad foundation commit and split unrelated presentation
   pieces if that makes review clearer.

## Resume and workspace ownership

Worktree: `/home/jagenmark/Documents/ChatGPT/LG-duel-glide-pr`

Branch: `codex/glide-arcade-duel`, based on `origin/main` at `0ed5eb6`.

The original `/home/jagenmark/Documents/ChatGPT/LG-duel` checkout and its
uncommitted changes were preserved. The isolated test session used server/control
ports 28460/28461 and was closed cleanly at checkpoint; do not control the user's
separate game session.

The reviewed agent commits have been integrated into the branch. A manifest is
saved locally at `build/glide-chaos-integration.json`. The repository's PowerShell
integration helper could not be run because PowerShell is not installed on this
host; ordinary Git cherry-picks and the combined-build tests were used instead.
No merge into main has been performed.

Test logs are local and ignored by Git:

- `/tmp/glide-chaos-tests-final.log`
- `/tmp/glide-chaos-regression.log`
- `/tmp/glide-chaos-soak.log`
- `/tmp/glide-chaos-build-final.log`
