# Playtest tooling verification — 2026-09-05

This work changes developer controls and playtest tooling. Gameplay, bot, arena,
and HUD work belongs to the separate Glide Duel task.

## Delivered

- Explicit attachment for ordinary CLI/MCP controls: no implicit relaunch after
  a timeout. Launch-on-demand is an explicit CLI option.
- Listener lifetime IDs, checked by the native server before queuing a pinned
  request. A restarted listener cannot accept a stale playtest action.
- Native consecutive input segments with one final neutral release and server
  acknowledgement. The ordinary player command path handles the input.
- A reusable Python/JSONL interface and MCP observe/step tools, local state,
  HUD captures, timing diagnostics, assertions, and durable JSON/HTML evidence.
- Launcher recognition of a running Linux executable unlinked during rebuild,
  while still verifying the saved process creation time before stopping it.

## Checks run

`python -m unittest discover -s scripts -p 'test_lg_*.py'`: 302 tests passed.
`build/default/tests/lg_duel_dev_control_tests`: passed, including sequence
parsing, invalid input rejection, and uninterrupted timeline transitions.
The client and developer-control test binary built successfully.

Live check used the real SDL_GPU/Vulkan client on Intel LNL graphics, connected
to local server 28160 with controls at 28161. A deliberately stale listener ID
was rejected as `session_changed`. The example plan completed a 296-tick,
six-segment request with the final release acknowledged. Action plus
acknowledgement took 2384 ms (nominal input duration 2368 ms).

Local state round trips averaged 8.3 ms across five reads. Endpoint observations
with screenshots took about 0.5–1.5 seconds. These are tool wall times, not game
frame-time measurements. Captures differed from the preceding state sample by
two snapshot ticks in the saved sequence report.

Evidence: `build/playtests/native-sequence-validation/index.html`, with original
PNG copies, `result.json`, and `events.jsonl`. End checks passed: health 100,
8.09 world units displaced, zero endpoint speed. Visual inspection found that
the final view faces nearby cover; the zero speed does not distinguish braking
from collision. These checks validate tool execution and endpoint observations,
not intermediate slide/jump state or human movement feel.

The main gameplay task was given the rebuilt endpoint and owns remaining live
controls and session cleanup. Use [Developer Control](DEVELOPER-CONTROL.md#attached-playtest-sessions)
for commands and [the example plan](../examples/playtests/glide-movement.json).

The gameplay task subsequently ran the same wrapper on a corrected open route:
30 ticks of strafe, 45 neutral ticks to settle, then the run/slide/jump/brake
sequence. Its report is `build/playtests/glide-open-lane/index.html`: 306 ticks,
seven segments, 2467 ms action/ack, health 100, 16.21 units displacement, zero
endpoint speed. That task inspected the final image and confirmed open floor.
The reusable example now uses this route. Its screenshot observations took
about 1.8 seconds, illustrating that capture cost varies between calls.

## Held-input animation capture follow-up

Added native `input_tick` phase capture and `lg_animation_review.py` for fixed
front/side body review. On the live Vulkan client, standing and crouch shots
completed at input ticks 41/41 and 60/61 respectively. Each capture attested a
visible connected body; crouch shots recorded `crouched=true` in predicted,
authoritative, and rendered body state, with `DUCKING` at weight 1. Temporary
presentation settings and first-person view were restored afterward.

Evidence: `build/animation-review-tool-validation/index.html`. Visual inspection
of the side shot showed an upright Pip/Fuse model despite the crouched state.
The separate animation task confirmed that the active loader uses Pip/Fuse and
is correcting that asset. These images therefore serve as a baseline, not proof
that the animation is correct.

The scenario launcher follow-up fixes Linux executable selection and GPU setup.
An existing material scenario completed startup, input, capture and cleanup on
Linux; its hardcoded 1280x720 screenshot assertion failed against the actual
618x764 tiled window. That is retained as a failed run, not counted as passing.
