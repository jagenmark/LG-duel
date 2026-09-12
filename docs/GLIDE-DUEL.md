# Glide Duel

A fast round-based 1v1 mode built around grounded strafing, crouch slides,
slide jumps, and air turns. Select it during warmup in the client console:

```
map glide_duel
gamemode glide
```

The new `glide_duel` arena has mirrored spawns, a central sightline blocker,
four beveled cover blocks, raised side decks, and two short flanks. The central
pillar also has beveled corners, giving high-speed routes clearance without
opening the spawn sightline. Both spawn anchors
pass navigation validation. Other maps remain selectable.

For solo play, then run:

```
bot_add 1
bot_attack medium
ready
```

For multiplayer, connect both clients to the same server, select `gamemode glide`
during warmup, and have both players press F3. `bot_kick` removes practice bots
during warmup. `bot_attack easy` and `bot_attack hard` change solo difficulty.
Use matching client/server builds for the new mode ID.

## Rules

- First to seven round wins. Two seconds to prepare and two between rounds.
- Each round lasts at most 45 seconds. Elimination wins immediately; at the
  buzzer the player with more health wins. Equal health replays the round.
- The last 15 seconds are **Overdrive**: enemy damage doubles and a visible
  circle closes from 24 to 4 metres around the final pickup location. Outside
  the circle, both players lose 8 HP per second. Self-damage and zone damage
  do not gain the Overdrive multiplier.
- Both players respawn with the same configured health and weapon ammunition.
  All existing weapons are available; server damage/ammo settings still apply.
- Ordinary map health pickups and vampirism are disabled. Timed contested
  power-ups provide brief advantages and a chance to recover.
- The mode enables glide movement and disables flight. Only slots 1 and 2
  participate; additional connected slots do not enter the duel.
- After a match, press F3 to ready for the rematch. F5 resets the match.

The movement toggle also works in other modes: `g_glide_movement 1`. Glide Duel
applies its movement profile without changing the configured base values;
switching modes restores those values.

## Conditions and contested power-ups

The countdown announces a condition shared by both players. It rotates even
when a round ends in a draw:

| Condition | What changes | Opportunity |
|---|---|---|
| FLOAT | 30% lower gravity | Carry a slide jump over cover; punish exposed landings. |
| BLAST | 50% stronger enemy knockback | Push the opponent off their route or out of the closing circle. |
| RUSH | 40% shorter dash cooldown | Chase an advantage or dash back to contest a pickup. |

Power-ups appear 8, 20, and 32 seconds into each round. A world marker and HUD
announce the location and type three seconds early. Each pickup remains for
eight seconds or until one player collects it. The order rotates each round.

| Power-up | Effect |
|---|---|
| SURGE | 35% more enemy damage for six seconds. |
| GUARD | 35% less incoming enemy damage for six seconds. |
| REPAIR | Restore 25 HP, capped at the round's starting health. |

Touch a marker to collect it. REPAIR stays available when you are at full health.
Temporary buffs do not stack; REPAIR does not replace an active buff. Zone and
self-damage ignore SURGE and GUARD. The HUD shows your active buff and remaining
time, and announces each collection.

Locations are open standing points reachable from both actual round spawns.
The final circle centers on the last pickup, away from the central pillar.
Maps without a common reachable area retain the standard round clock and
Overdrive damage, with contested pickups and the closing circle disabled.

Read the condition, race or ambush the next pickup, spend the brief advantage,
and move toward the final circle before it closes. Bots follow the same public
announcements, contest pickups while firing, and navigate toward the circle's
interior without receiving hidden opponent positions.

## Playing the movement

Use standing strafes to aim, brake, and reverse direction. Hold Ctrl while
moving quickly to slide. Press Space from a slide for a boosted jump, then
steer in the air. Forward/side input carves a controlled arc during a grounded
slide; it turns more slowly than air steering and does not add speed. Input
against the direction of travel brakes the slide. Analogue input scales the
carve/brake strength. Release Ctrl before tight corners for full standing
control. Neutral slide input preserves momentum.
Crouching keeps 80% of standing collision height, with bent-knee body poses
and a dedicated slide stance. See [animation review](PLAYER-ANIMATION-REVIEW.md).

The tactical loop is to earn damage, reposition through a slide, and choose
when to commit. Overdrive lets a trailing player turn a round with a clean
shot, while self-damage makes careless close rockets costly. Short rounds
reset the health disadvantage and make adapting to an opponent worthwhile.

Bots retain their normal visibility and reaction limits, navigate the map,
and choose weapons by range and shot quality. In combat they plan lateral
positions around their preferred weapon range. They use crouch slides on long,
aligned walking routes and release crouch before corners. Stricter slide-entry
thresholds and more forgiving continuation thresholds avoid repeated crouch
switches near a speed or alignment boundary; jumps remain tied
to navigation links. Difficulty changes input quality, not health or damage.

The rules and simulation have automated coverage. Long-term enjoyment and
weapon balance need human playtesting; this is a playable first iteration,
not a claim of established competitive balance.

See [verification and playtest notes](GLIDE-DUEL-PLAYTEST.md) for the checks,
findings, and limits of the current iteration.

### Glide foot trail

Grounded slides leave two short cyan trails made from overlapping, tapered wisps
behind the boots. Their edges fade in every direction, with a 0.12-second
slide onset and a brief fade-in for each wisp. The trail
follows the player's path, scales with speed, and fades over 0.24 seconds.
Jumping and braking stop emission. Hidden or dead players lose their trails;
map changes reset the effect and teleports do not draw connecting streaks.
The effect is local presentation only and uses a bounded pool.

### Match HUD and health position

Glide Duel uses the compact score panel with cyan/coral edges, both player
scores, seven round-win markers, the round timer, and an amber overdrive state.
A draining clock bar marks the Overdrive threshold. Ready checks identify each
player's readiness; match point and a 6-6 deciding round are called out beneath
the clock. Round results distinguish eliminations, health-based timeouts, tied
health, and double knockouts, and show the next-round or rematch countdown.
Countdown and readiness messages sit under the panel. The weapon icon strip retains its existing art
and layout. The default health display (`cl_health_style 0`) uses the same thin cyan bar
in **all modes**, 20 pixels from the left and 24 pixels from the bottom.
Low health turns the fill coral. The shared health-style setting also applies
in Glide Duel; the mode no longer overrides it.

`cl_health_offset_x` and `cl_health_offset_y` move the entire health display in
screen pixels. These are shared, archived client settings in **all modes and
all health styles**. Positive X moves right; positive Y moves down. For example,
`cl_health_offset_x 100` and `cl_health_offset_y -60` move it 100 pixels right and
60 pixels up. Set both to `0` to restore the default position. Offsets support
`-16384..16384`; `cl_health_size` still controls scale.

This update uses game protocol 67, replay format 9, and replay simulation
revision 4. Use matching builds; recordings from earlier simulation revisions
are rejected rather than played with different movement physics.
