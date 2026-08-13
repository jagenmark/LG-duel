# Weapon switch and first-person hands handoff

## Status

This branch keeps the switch code, reset rules, view ownership, tests, and
render paths. First-person hands are an experimental, default-off preview;
they need separate visual review before they become a normal setting.

The user asked to pause after the final pose pass on 2026-08-11. Resume from
the existing branch and worktree. Do not reset the three earlier commits or
start the feature again from `main`.

The final design choices are:

- First person: move the old weapon and its hands down until fully off screen,
  swap while hidden, then raise the new weapon and hands from that same point.
- Third person: use the inverse motion. The Worker arms and held weapon lift
  toward the head, swap near the top, then return to the normal grip.
- Hands: keep the weapon as the main shape. Hands should be small, natural,
  mostly below the weapon, and opt-in with `r_viewmodel_hands 1` until their
  visual work passes review.
- The Revolver may use only the right hand.

## Worktree and branch

Use:

```text
C:\Users\gosee\Documents\Codex\LG-duel-clean\.worktrees\weapon-switch-hands-20260810
codex/weapon-switch-hands-20260810
```

The task started this continuation at commit `81a5f4b`. Inspect later commits
and the worktree before changing code.

## What to retain

### Switch controller

`src/render/WeaponSwitchPresentation.*` is a pure, fixed-time presentation
controller. It takes selected-weapon observations and explicit frame delta. It
does not read wall time, network classes, bots, or replay state.

It now has:

- direct initialization to the first observed weapon;
- one outgoing phase, a hidden apex, and one incoming phase;
- a latest-target rule for rapid switching with no queue;
- no flash back to an older weapon;
- event de-duplication using existing weapon and visual-seed data;
- stale outgoing fire rejection;
- valid incoming fire promotion;
- stable continuous Lightning Gun and Freeze Gun use;
- an explicit reset entry point for map loads, disconnects, death, followed
  player changes, and future replay seeks.

The first-person output uses `lift` as a normalized hidden amount. `0` means
the normal rest pose and `1` means fully hidden at the swap. `GameApp.cpp`
maps that value to a negative viewmodel Z offset. Do not change this back to
an upward first-person raise.

### Final-frame attachment

`Scene3D.cpp` adds hands after the weapon-specific final frame has applied:

- camera placement and `r_weapon_pos`;
- viewmodel bob, sway, inertia, and landing motion;
- switch drop;
- weapon scale and placement;
- recoil;
- Machine Gun vibration and barrel motion;
- Rocket Launcher recoil and latch motion;
- Freeze Gun optical motion;
- Plasma Gun containment motion.

Each hand then adds a weapon-local pose transform. Keep that order. A hand
frame built again from the camera will drift from recoil and switching.

### Static hand resources

`src/render/BakedViewModelHands.hpp` contains three checked-in, compile-time
meshes:

- right trigger grip;
- left closed support;
- left open support.

They use the ViewModel pass and upload once with other static simple meshes.
They do not create or upload mesh data per frame. Current totals are:

- 140 triangles and 420 expanded vertices per mesh;
- 1,260 vertices across all three shared meshes;
- 50,400 GPU bytes at 40 bytes per vertex;
- no index buffer;
- two extra draws for two-handed weapons;
- one extra draw for the Revolver;
- no extra draws when `r_viewmodel_hands 0`.

### View ownership and reset work

Keep the split between:

- the connected client body;
- the player that supplies the first-person camera;
- the world body hidden to avoid drawing inside the camera;
- the player that supplies the first-person weapon and hands.

This fixes followed-player views without widening team or enemy visibility.
`r_dev_camera_draw_connected_body` is a default-off proof aid that lets a dev
camera show the connected Worker through the normal third-person path.

`src/render/WeaponPresentationLifecycle.hpp` clears local and remote switch
history on hard timeline changes. Keep it free of live network and replay
types so a replay seek can call the same reset later.

### Third-person pitch

Worker upper-body switch pitch must stay positive. In this model-space path,
positive pitch raises the arms and `weapon_socket`. The prior negative sign
aimed the gun through the torso. Keep locomotion on the lower body.

## Faults found and fixed in this continuation

### Duplicate Lightning Gun and Grenade Launcher bodies

The renderer drew the new 3D ViewModel body and the old 2D body at the same
time for Lightning Gun and Grenade Launcher. The old body came from
`buildPerspectiveWeaponOverlay` in `src/render/ScreenUi.cpp`.

The current worktree blocks legacy body geometry for every valid weapon while
keeping the Lightning Gun and Freeze Gun beam effects. Keep this fix. It also
prevents a legacy outgoing body from appearing during a switch to LG or GL.

### First-person direction

The current first-person switch drops the full final weapon frame down by up
to 0.55 world units. The controller uses a linear outgoing phase, a short
hidden plateau around the midpoint, and a linear incoming phase. Early local
diagnostic captures showed the old gun going down, no gun at the apex, and the
new gun rising.

## Hand coordinate facts

Hand mesh source and hand pose metadata use weapon-local axes:

```text
+X = weapon forward
+Y = weapon right
+Z = weapon up
```

`weaponLocalPoint` scales pose translation by `weaponFrame.scale`. The hand's
own `transform.scale` sets mesh size and does not inherit the weapon scale.
This split matters because authored weapon scale varies a great deal.

Useful authored model sockets are:

```text
Rocket Launcher grip          (-0.58, 0.00, -0.35)
Rocket Launcher support       ( 0.43, 0.00, -0.39)
Plasma Gun grip               (-0.42, 0.00, -0.28)
Plasma Gun support            ( 0.04, 0.00, -0.18)
Freeze Gun grip               (-0.39, 0.00, -0.30)
Freeze Gun support            ( 0.28, 0.00, -0.08)
Railgun grip                  ( 0.00, 0.00,  0.00)
Revolver grip                 (-0.23, 0.00, -0.24)
```

Do not copy these numbers into a pose without checking the final frame. Rocket
Launcher and Railgun apply grip alignment before the hands are added. Plasma
Gun and Freeze Gun keep their model-origin frame in first person. A socket can
also sit below the visible screen even when it is correct in model space.

## Final hand attempt and visual result

The final pass is `weapon-hands-v27-*` under `build/captures`. These are local
diagnostic files, not approved evidence and not gallery items.

The result still fails:

- Lightning Gun: no hand reads against the dark receiver.
- Freeze Gun: both hands remain hidden.
- Railgun: one blue glove block sits beside the rifle and does not read as a
  support grip.
- Machine Gun: only a small side group reads; the trigger hand stays hidden.
- Shotgun: hands remain mostly hidden.
- Revolver: only a narrow part of the right glove shows beside the frame.
- Rocket Launcher: hands remain hidden apart from a small lower-edge sliver.
- Grenade Launcher: hands remain hidden.
- Plasma Gun: hands remain hidden inside or below the body.

The pose-only offset approach has reached its limit. Do not spend another pass
moving the same meshes by small numbers.

## Likely next hand fix

Start with one weapon, one hand, and a clear debug color. Prove the local axes,
depth, winding, and screen bounds before tuning all nine rows.

The strongest next change is to revise the local origin and shape of each of
the three shared meshes:

1. Put the palm and grouped fingers around the local contact origin, not mostly
   below it.
2. Let only the cuff and short forearm extend toward negative Z and off screen.
3. Keep the palm compact and move most mass to the underside of the grip.
4. Give the thumb a clear side silhouette at the final game size.
5. Use a temporary bright glove material or bounded hand-axis view, disabled by
   default, to tell hidden geometry from bad placement.
6. Once one right trigger hand works on the Revolver, reuse it on Machine Gun,
   Shotgun, and Railgun before adding support hands.
7. Tune at `r_weapon_pos 0`, `1`, and `2`; the project default is centered.
8. Check low and high supported FOV before accepting a pose.

Keep the hands modest. The visual target is a small glove around a grip and a
short forearm that leaves the lower or side edge. Do not make the palms large
enough to cover the weapon.

## Tests already added

Focused code covers:

- switch init, outgoing, apex, incoming, and finish;
- equal elapsed time at 30, 125, and 240 Hz;
- rapid A to B to C before and after the apex;
- repeated switch spam and same-weapon selection;
- stale hitscan and projectile events;
- retained-event de-duplication;
- valid incoming fire and continuous LG or Freeze use;
- map revision, same-map explicit reset, death, respawn, disconnect, slot
  reuse, followed-player change, and explicit seek-style reset;
- local, followed-player, and third-person ownership;
- no duplicate viewmodel owner;
- positive Worker switch pitch;
- all nine pose rows and all selected mesh variants;
- finite hand transforms for centered, right, and left placement;
- static ViewModel pass resources and no dynamic hand mesh data;
- hands following the final weapon frame;
- hand cvars and dev control phase names.

Run the focused set with:

```powershell
cmake --build --preset default --target lg_duel_client lg_duel_screen_ui_tests lg_duel_scene_3d_tests lg_duel_weapon_switch_presentation_tests lg_duel_weapon_presentation_lifecycle_tests lg_duel_death_camera_tests lg_duel_client_cvars_tests lg_duel_dev_control_tests --parallel
ctest --test-dir build/default -R "^(lg_duel_(screen_ui|scene_3d|weapon_switch_presentation|weapon_presentation_lifecycle|death_camera|client_cvars|dev_control)_tests)$" --output-on-failure
python scripts/test_lg_live_scenario.py
```

The full suite, asset checks, package check, timing run, final evidence set, and
independent review still remain. Do not reuse an older full-suite result as a
final result.

## Live session and captures

Use owned ports so this work stays clear of other tasks:

```powershell
.\scripts\lg-control.ps1 start -ServerPort 28260 -ControlPort 28261 -Timeout 20
python .\scripts\lg_control.py --port 28261 --timeout 10 load-map eyetoeye
python .\scripts\lg_control.py --port 28261 --timeout 5 set-cvar r_viewmodel_hands 1
python .\scripts\lg_control.py --port 28261 --timeout 5 set-cvar r_weapon_switch_animation 1
python .\scripts\lg_control.py --port 28261 --timeout 5 set-cvar r_weapon_pos 0
python .\scripts\lg_control.py --port 28261 --timeout 5 set-player-weapon mg
python .\scripts\lg_control.py --port 28261 --timeout 10 capture --name diagnostic-name
.\scripts\lg-control.ps1 stop -ServerPort 28260 -ControlPort 28261
```

The dev phase hook accepts:

```text
local_weapon_switch_outgoing
local_weapon_switch_apex
local_weapon_switch_incoming
remote_weapon_switch_outgoing
remote_weapon_switch_apex
remote_weapon_switch_incoming
```

It records switch view, actor, normalized time, lift, shown weapon, old weapon,
new weapon, hand toggle, first-person vertical offset, and third-person pitch
in capture frame metadata. Verify that metadata again after the next build.

## Final evidence still required

Do not ask for review until all of this exists and has been checked by the task
author:

- rest, 25 percent down, hidden apex, 75 percent up, and final rest strips for
  Machine Gun, Rocket Launcher, and Revolver;
- all nine weapons at rest with hands on;
- centered, right, left, low FOV, high FOV, motion, recoil, and switch views;
- legal Quake Live switch then fire, fast valid incoming fire, Machine Gun
  sustained fire, Rocket Launcher fire, continuous LG or Freeze use, and no
  stale outgoing flash;
- human standing and running third-person switch;
- bot third-person switch;
- outgoing, apex, incoming, and final Worker frames;
- followed teammate first-person view with the right weapon and hands;
- hands-on and hands-off timing with resource and draw counts.

Final proof images need the UTC naming rule, metadata, an independent `pass`,
and private publication to the project Sites gallery.

## Parallel work limits

Keep this task presentation-only. Do not edit server gameplay, bots, bot state,
weapon rules, `UserCommand`, packet structures, protocol versions, or replay
files.

Likely overlap with the replay branch remains in:

- `src/app/GameApp.cpp`;
- `src/render/Renderer.hpp`;
- `src/render/Scene3D.cpp`;
- `src/render/Scene3D.hpp`.

A future replay path should provide selected-weapon observations, existing
authoritative fire events and visual seeds, explicit replay delta, and a hard
presentation reset after seek. It should not add switch state to the protocol.
