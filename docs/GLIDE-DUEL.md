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
- The last 15 seconds are **Overdrive**: damage doubles, including self-damage.
  The HUD announces Overdrive and shows the time remaining.
- Both players respawn with the same configured health and weapon ammunition.
  All existing weapons are available; server damage/ammo settings still apply.
- Health pickups and vampirism are disabled in this mode. Every hit matters.
- The mode enables glide movement and disables flight. Only slots 1 and 2
  participate; additional connected slots do not enter the duel.
- After a match, press F3 to ready for the rematch. F5 resets the match.

The movement toggle also works in other modes: `g_glide_movement 1`. Switching
away from Glide Duel keeps that toggle enabled; use `g_glide_movement 0` to
return to classic movement.

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

The September 12 movement update uses game protocol 66 and replay simulation
revision 3. Use matching builds; recordings from earlier simulation revisions
are rejected rather than played with different movement physics.
