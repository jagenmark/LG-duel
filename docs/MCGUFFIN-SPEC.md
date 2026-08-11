# McGuffin mode specification

McGuffin is LG Duel's original implementation of the neutral-objective control
mode popularized by Diabotical. It does not use Diabotical code, assets, names
other than the generic mode name requested for this project, sounds, or UI art.

## Verified reference rules

- Two teams fight over one neutral objective, which appears near the center
  roughly 30 seconds after live play begins.
- Carrying the objective builds up to 10 unbanked coins. Reaching the carrier's
  owned base installs it and banks those coins.
- An installed objective generates coins for its team. A round ends at 100.
- An attacker steals an installed objective by remaining in the enemy base for
  a visible interaction period. Death or leaving interrupts the attempt.
- Carrier death and deliberate throws drop the objective. Available play
  descriptions support persistent drops rather than a routine short return;
  LG Duel adds a 30-second recovery timer to guard against unreachable drops.
- The reference mode used a three-second respawn. LG Duel uses the shared
  `sv_respawn_delay` rule with a two-second shipped default. Matchmaking McGuffin had friendly fire
  disabled.
- Normal scoring pauses at 99 for a short final uncontested hold. An attacker
  contesting the base resets that hold.
- The match is best of three rounds. Bases swap for round two. In the deciding
  round, the first valid installation claims a base.

Sources: [Epic Games mode description](https://store.epicgames.com/p/diabotical),
[Gamepur overview](https://www.gamepur.com/guides/diabotical-game-modes-explained),
[competitive playbook](https://anima.nz/diabotical-3v3-playbook/),
[contemporary overview](https://forum.rocketbeans.tv/t/diabotical-zock-am-21-09-20/80493),
[Diabotical update notes with in-game tips](https://www.reddit.com/r/Diabotical/comments/k91qrj/),
[respawn change notes](https://www.reddit.com/r/Diabotical/comments/i1vvci/), and
[99-point fix notes](https://www.reddit.com/r/Diabotical/comments/j4utpu/).

## Objective state machine

| Current state | Authoritative event | Next state | Result |
| --- | --- | --- | --- |
| Neutral spawn | Spawn delay expires and a live team player touches it | Carried | Carrier and team are recorded |
| Carried | Carrier enters its owned base | Installed Red/Blue | Carry coins are banked; control scoring begins |
| Carried | Carrier dies, disconnects, leaves its team, or becomes invalid | Dropped | Last valid server position is used |
| Carried | Carrier sends `mcguffin_throw` | Dropped | Aim, carrier velocity, arc, gravity, bounce, and pickup lockout are server controlled |
| Carried | Carrier enters a base it does not own | Carried | No effect |
| Dropped | A live team player touches it | Carried | New carrier is authoritative |
| Dropped | Uncollected for the configured safety interval | Neutral spawn | Teleports to the initial spawn; installed/base states are unaffected |
| Installed Red/Blue | Enemy completes the base hold | Carried | Scoring and final-hold progress stop |
| Installed Red/Blue | Score reaches 99 | Installed Red/Blue | Final uncontested hold begins |
| Installed Red/Blue | Enemy contests at 99 | Installed Red/Blue | Final-hold timer resets |
| Any | Round or match reset | Neutral spawn | Carrier, timers, credit, scores, and events reset |

A carried objective always has one valid carrier. Installed, neutral, and
dropped states have no carrier. Only a live McGuffin match can change objective
scores. Fixed-tick sub-points retain fractional scoring credit.

## Shipped values

| Setting | Default | Status |
| --- | ---: | --- |
| Round score limit | 100 | Verified |
| Match rounds required | 2 (best of 3) | Verified |
| Initial objective delay | 30 s | Verified approximately |
| Player respawn | Shared `sv_respawn_delay`, default 2 s | Intentional LG Duel tuning; reference was 3 s |
| Carry credit | 1 point/s, maximum 10 | Rate provisional; cap verified |
| Installed scoring | 1 point/s | Provisional cadence |
| Installation hold | 0 s | Provisional |
| Steal hold | 1 s | Provisional |
| Dropped safety return | 30 s | Intentional LG Duel recovery rule for unreachable or bugged drops |
| Final uncontested hold | 3 s | Provisional duration; mechanic verified |
| Friendly fire | Disabled | Verified for matchmaking |
| Throw | `G`; 12 forward, 4 upward, full velocity inheritance | Intentional LG Duel defaults; fully server configurable |

## Uncertain details and implementation assumptions

Public descriptions do not reliably specify the installed scoring cadence,
steal duration, exact final-hold duration, multi-attacker acceleration,
wrong-base behavior, or disconnect/team-change behavior. LG Duel therefore uses
the configurable provisional values above, does not accelerate steals, ignores
wrong-base entry, prevents live team changes, and drops an invalid carrier at
the last valid position. If losing a player makes the team roster invalid, the
existing match flow safely resets to warmup.

The comeback armor mentioned in later Diabotical versions is not implemented.
Deliberate objective throwing is an LG Duel extension with authoritative
physics and replicated tuning. Carry credit is replicated and banked, and the
client sends the carried-objective throw edge on `G`. Base direction guidance
uses HUD state text, colored world markers, and a low-noise screen-edge
navigation widget when the current target is off-screen.

## Map entities

McGuffin maps use exactly one point entity and two cuboid trigger entities:

```text
{
"classname" "info_mcguffin_spawn"
"origin" "0 0 64"
}
{
"classname" "trigger_mcguffin_base"
"team" "red"
{ ... one non-degenerate cuboid brush ... }
}
```

Team spawns use physical base groups so round-two swaps and deciding-round
claims automatically move each team to its currently owned side:

```text
{
"classname" "info_player_team"
"spawn_group" "red_base"
"origin" "-256 96 32"
"angle" "0"
}
```

Valid groups are `red_base` and `blue_base`; they identify map geometry rather
than permanent teams. Authored `angle`/`yaw` is applied on spawn. Legacy `team`
properties still map to the corresponding physical group. McGuffin requires at
least one candidate for each base; `mcg.map` ships four per side. Duplicate objective spawns or bases,
invalid teams, and non-cuboid/degenerate base triggers are rejected with a
line-numbered map error. Ordinary Duel and Clan Arena maps may omit all of
these entities.

The server rejects solid or in-base candidates, avoids occupied candidates,
scores enemy distance, enemy line of sight, nearby teammates, and recent use,
then makes a deterministic weighted choice among the best three. `spawn_debug`
prints the most recent candidate scores and selected index.

The server rejects a request to enter McGuffin mode when the active arena does
not have a complete valid layout. Static objective geometry participates in the
map content hash and is not repeated in snapshots.

## Networking and presentation

Protocol version 58 replicates the explicit objective state, carrier, dynamic
position, score credit, interaction/final-hold timers, event sequence, team
scores, round wins, and dynamic base ownership. Snapshot state is sufficient
to join an active match or recover after packet loss. The client renders simple
colored placeholder markers for both bases and the objective, plus a screen-edge
card and direction cue for the current objective or base when it is off-screen.
The HUD also shows score, round score, carrier, control state, and spawn timing.
