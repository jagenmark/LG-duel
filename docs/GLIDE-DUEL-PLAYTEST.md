# Glide Duel verification — 2026-09-05

The movement and mode are implemented, with a dedicated arena and a playable
bot opponent. The checks below establish mechanical behavior and basic match
flow. They do not establish long-term enjoyment or competitive balance.

## Live client checks

Used the real SDL_GPU/Vulkan client on Intel LNL graphics, connected to a local
UDP server. Drove ordinary movement, aim, weapon selection, and attack commands
through developer control and inspected exact game-window captures.

Checked the mode on Eye-to-Eye, Thunderstruck, and the new `glide_duel` arena.
Observed standing movement, crouch momentum, jumping, collision stops, weapon
fire, bot movement and kills, Overdrive HUD, round respawns, score progression,
and return to ready state after a completed match.

The live checks led to these changes:

- Show `PLAYERS 2/2` and both players' round scores.
- Show `DRAW - REPLAY ROUND` for a tied timeout.
- Add a compact mirrored arena with cover that breaks the spawn sightline.
- Add end stairs to its side decks after a straight outer-route traversal
  stopped against a deck edge. The revised route was then traversed in-client.
- Enable remote bodies: this client's saved `r_draw_remote_players` was
  accidentally `0`, which left floating weapons. The intended default is `1`,
  and the user requested that it remain enabled.

Representative captures are in `build/captures/`:
`glide-arena-live.png`, `glide-slide-playtest.png`, and
`glide-duel-deck-route.png`. These are observations from the real client,
not rendered mockups.

After the tools update, ran a continuous seven-segment input sequence through
the native client: strafe into the open lane, settle, run, slide, slide jump,
coast, and release to brake. The 306 input ticks were acknowledged in 2.47
seconds. The player moved from `(-14, 0)` to `(1.88, 3.24)` and stopped on open
floor with zero planar velocity and 100 health; the HUD capture confirms clear
space ahead. The initial wider route stopped against cover and was not counted
as braking evidence. The final report and captures are in
`build/playtests/glide-open-lane/index.html`.

## Automated checks

- Movement tests cover the stronger standing brake and direction changes,
  preserved slides, slide jumps, air turns, and the classic movement path.
- Glide mode tests cover command/snapshot codec roundtrips, two-player bot
  fill, replicated movement, timed wins/draws, health resets, next-round clock,
  first-to-seven match end, and doubled Overdrive weapon damage.
- Bot tests cover movement, accepted damage, round completion, deliberate
  sliding on a long route, and the existing visibility restrictions.
- A five-minute simulation with medium bots on the final arena completed one
  match. It recorded 19,657 ticks with at least one damaged player and 1,795
  ticks with at least one sliding player. At 125 Hz these are about 157 and
  14 seconds, respectively. Damaged-player time is not continuous firing time.
- The final arena passes map and spawn-navigation validation.
- Focused mode, HUD, FFA, bot, and bot-soak tests pass. A full 100-test run
  exposed an FFA configured/effective-rule regression; it was fixed and the
  FFA tests then passed. That run also found failures in the separate Worker
  glTF material tests; those remain outside this movement/mode change.

## Limits

Input and screenshot round trips make this a delayed, tool-driven playtest.
It is useful for finding broken behavior and presentation issues, but cannot
reproduce human mouse feel, continuous tracking, or reactive dodging. The live
network check used one client against a server bot, not two human opponents
under WAN latency. Human 1v1 sessions are still needed to assess the 45-second
clock, Overdrive lethality, weapon balance, and whether the mode remains fun.


## Polish pass — 2026-09-12

Grounded slides now carve at a bounded rate (1.8 rad/s) without adding speed.
Opposing input brakes a slide, and analogue input scales both controls. Neutral
slides, slide jumps, standing braking, and air control retain their roles.
Focused movement tests cover the arc, speed conservation, strong counter-brake,
and gentle analogue braking in addition to the existing movement cases.

All four cover blocks and the central pillar/cap have 0.6 m corner bevels.
The map validator reports 29 boxes and six convex brushes; navigation passes
with 244 nodes, 607 links, and both spawn anchors reachable. Four mirrored
player-cylinder checks and high-speed tangent sweeps confirm that the new
cover corners admit the intended route without losing slide speed.

Bots now require extra speed/alignment/runway margin to begin a slide and can
continue down to the existing safe release thresholds. Tests cover speed and
alignment fluctuations, corner approach, and resetting slide intent. The arena
soak reached 5–6 after five minutes and finished 7–6 at 344.544 simulated seconds:
13 decisive rounds, zero draws, and damage accepted in both directions. A
five-minute match-completion assertion was incorrect for a close first-to-seven
contest. The revised check requires sustained decisive rounds, damage, and
slides at five minutes, then completion within a full 13-round time budget.

The HUD adds ready/not-ready labels, seven round-win markers, match point and
6–6 deciding-round notices, a draining clock bar with an Overdrive marker,
result reasons, and next-round/rematch countdowns. Reused HUD state clears the
old clock and Overdrive color between phases. Long names/results fit their
columns at 308-, 640-, and 1920-pixel viewport widths.

Live GPU movement checks passed the continuous run–slide–jump–brake sequence
and an additional carve–counter-brake sequence. Both ended on open floor at
100 health with zero planar velocity and no pending input or correction offset.
The first sequence covered 16.34 m, and the second 6.60 m. Reproduction:
`examples/playtests/glide-polish.json`; current report:
`build/playtests/glide-polish-all-moves/index.html`. HUD captures are in
`build/captures/glide-polish-*.png`.

The live review also found that living players could retain stale view angles
and pending movement across a round respawn less than 32 m away. Round entry
now starts a fresh prediction timeline and applies the authoritative spawn
facing once. Tests cover both ordinary and omitted countdowns, pending-input
removal, preserving subsequent mouse aim, and client/killcam/session behavior.
Presentation reattachment after a replay initializes from current aim rather
than consuming a historical spawn angle.

Final live spawn check deliberately set the living player to 90 degrees in
warmup, then readied without scripted movement or aim. The client entered the
new round at 0 degrees, correctly facing the arena, with zero angular error.
A subsequent 35-degree view change remained intact across normal snapshots.
Evidence: `build/playtests/glide-polish-spawn.json` and
`build/captures/glide-polish-spawn-facing.png`. The rebuilt GPU client was
stopped after verification. All 13 focused test targets used in this pass
passed; this was not a full-suite run.
