# Developer Control And Visual Capture

LG Duel has an opt-in, local developer-control plane for map iteration and
agent-driven visual inspection. It is independent of MCP: the running client
owns a small structured socket service, `lg-control.ps1` is a direct client,
and `lg_mcp_server.py` is a thin MCP-to-socket adapter.

## Agent Workflow: Tools Before Window Control

Use the local developer-control tools for every game action that they cover.
They address one explicit loopback endpoint, return structured state, and do
not depend on window focus. Do not inject keys or use Computer Use for routine
game control. Use it only after a capture, when a person needs visual
confirmation, or when no developer-control operation can perform the task.

1. Work from the intended worktree. Build it, then choose a unique server and
   control port for that worktree. Do not reuse another worktree's default
   endpoint.
2. Launch that worktree's owned client with the chosen ports. Pass the same
   control port to every later request.
3. Check `status` before and after a state change. Use typed map, input,
   camera, and capture operations first. Run console commands through
   `exec-console`, never by typing into the game window.
4. Give every call a short, known timeout. A map load or capture can take a few
   seconds; do not leave an agent waiting on an unbounded wrapper.
5. On a failure, read `status` and the worktree's logs, then retry only the
   failed bounded request. Stop the owned session when done.

### Isolated Worktree Session

The default control port is `27961`, so it conflicts when several worktrees
run at once. Pick unused port pairs and keep them with the task. For example,
this worktree uses server port `28060` and control port `28061`:

```powershell
.\scripts\lg-control.ps1 start -ServerPort 28060 -ControlPort 28061 -Timeout 20
.\scripts\lg-control.ps1 status -ControlPort 28061 -Timeout 3
```

The launcher owns only the processes it starts and stores its state under that
worktree's `build/dev-control/`. A request with `--port 28061` reaches only
that client. Never assume that a client on `27961` belongs to the current
worktree.

For the normal action loop, use the direct bounded client. It will launch or
verify the local owned client when needed, but each request has its own cap:

```powershell
python .\scripts\lg_control.py --port 28061 --timeout 5 status
python .\scripts\lg_control.py --port 28061 --timeout 10 load-map eyetoeye
python .\scripts\lg_control.py --port 28061 --timeout 5 wait-frames 2
python .\scripts\lg_control.py --port 28061 --timeout 10 capture --name eyetoeye-check
```

`lg-control.ps1` remains useful for lifecycle actions. Older wrapper-driven
state changes have waited too long when a map transition did not finish. For
map, input, camera, console, status, and capture work, prefer the direct form
above with `--timeout`. Retry after checking status only if the first request
timed out and the client still answers. Do not send the same map command again
while its first request may still run.

### Command Discovery And Direct Console Use

The project command list lives in [CONSOLE-BIBLE.md](CONSOLE-BIBLE.md). Use
the game commands `cmdlist`, `cvarlist`, and `help <name>` through developer
control when the list does not answer the question:

```powershell
python .\scripts\lg_control.py --port 28061 --timeout 5 exec-console cmdlist
python .\scripts\lg_control.py --port 28061 --timeout 5 exec-console "help settings"
python .\scripts\lg_control.py --port 28061 --timeout 5 get-cvar cl_fov
```

F10 is bound to the `settings` console command. To open the same settings menu,
run the command directly; do not focus the game and inject F10:

```powershell
python .\scripts\lg_control.py --port 28061 --timeout 5 exec-console settings
```

F11 is bound to the `misc` console command. It opens the tools and debug menu.
The default config does not bind a quit key.

Use `exec-console toggleconsole` only when testing the console UI itself. It
does not make key injection safer or needed.

### Deterministic Map, Input, And Capture Steps

Use this order for a reproducible visual check:

```powershell
python .\scripts\lg_control.py --port 28061 --timeout 10 load-map eyetoeye
python .\scripts\lg_control.py --port 28061 --timeout 5 set-camera --position 0,22.4,-12.85 --yaw -90 --pitch 0 --fov 100
python .\scripts\lg_control.py --port 28061 --timeout 5 send-input --ticks 125 --forward 1
python .\scripts\lg_control.py --port 28061 --timeout 5 wait-frames 2
python .\scripts\lg_control.py --port 28061 --timeout 10 capture --name central-overview
python .\scripts\lg_control.py --port 28061 --timeout 10 capture-map-views --map eyetoeye --preset standard
python .\scripts\lg_control.py --port 28061 --timeout 3 status
```

`load-map` and `reload-map` wait for an authoritative map revision. `send-input`
uses fixed ticks, `wait-frames` uses fixed rendered frames, and capture returns
the saved PNG path and renderer data. For scenario coverage, run
`python .\scripts\lg_live_scenario.py <name> --timeout <seconds>`;
it reserves distinct local ports and records its own run files. Do not try to
drive a live scenario through a window.

When a request fails, first run the bounded `status` call above. Then inspect
`build/dev-control/client.stderr.log`, `client.stdout.log`, and the server logs
in the same folder. If the endpoint refuses connections, restart only the
current worktree-owned session. If status answers but a map change timed out,
inspect its reported map and revision before retrying. If no developer tool can
perform the needed visual-only check, take a developer-control PNG first, then
use Computer Use only to confirm what that PNG cannot show.

End with:

```powershell
.\scripts\lg-control.ps1 stop
```

This stops only launcher-owned processes. It does not terminate a client that
another worktree or person owns.

## Architecture And Trust Boundary

The client starts the service only with `--dev-control` or `--control-port`.
Normal launches do not open a control port. The listener binds only to
`127.0.0.1`, rejects non-loopback peers, and accepts one bounded JSON-line
request at a time. It can run bounded game-console text, but it has no shell or
process launch path. Requests are limited to 64 KiB and use control protocol
version 1.

`exec-console settings` uses the same toggle as the default F10 binding: it
opens the Settings / Video menu when closed and closes it when open. Closing by
that toggle or with Escape discards unapplied draft values; only **Apply
changes** writes them.

The socket thread parses requests and queues them. State changes and renderer
capture run on the SDL client thread. The socket thread waits for an explicit
result with a 60-second ceiling, so it cannot block the authoritative 125 Hz
server tick. Map loading continues through the existing client command and
`ServerGame::loadRequestedMap()` path. The control response is not successful
until a newer authoritative map revision is received. No control or image data
is added to gameplay packets or snapshots.

The development camera replaces only the `PlayerState` passed to rendering.
It does not move, spawn, damage, or otherwise authorize a gameplay body. A
capture waits for at least one completed rendered frame after a map/camera
transition. SDL_Renderer uses `SDL_RenderReadPixels`; SDL_GPU downloads the
acquired swapchain texture through a download transfer buffer and waits for its
submission fence before writing the PNG. Both paths capture the real game view.

When no request is active, the control service performs no per-frame socket or
filesystem work. The only client-loop cost is a mutex-protected empty queue
check when explicitly enabled. Capture deliberately stalls one requested frame
for GPU readback and PNG output; it does not affect the server simulation and
is never active in normal play.

## Build And Launch

```powershell
cmake --preset default
cmake --build --preset default
ctest --preset default
```

The owned-process workflow is:

```powershell
.\scripts\lg-control.ps1 start
.\scripts\lg-control.ps1 status
.\scripts\lg-control.ps1 stop
```

`start` defaults to a verified `SDL_GPU/vulkan` session. The shared launcher
selects the same Intel ICD accepted by a valid local benchmark, checks its
manifest hash, probes the configured device with `vulkaninfo`, and passes that
single-manifest loader environment to the client. Once control answers, it
requires the exact renderer, GPU, driver, Vulkan version, manifest path/hash,
and `software_renderer: false` before reporting readiness. It performs the same
attestation before attaching to an already-running client.

The launcher uses `build/default`, launches hidden processes with control
enabled, records ownership and verified launch metadata in
`build/dev-control/processes.json`, and writes logs beside it. `stop` stops only
processes launched by the wrapper whose current executable path still matches.
It never kills an attached or unrelated process. If GPU startup falls back or
fails verification, only launcher-owned processes are terminated and the error
includes the selected ICD and client/Vulkan diagnostics.

Vulkan selection contains no tracked machine path. Resolution checks, in order:

1. An ignored JSON file named by `LG_DUEL_VULKAN_CONFIG`.
2. `build/dev-control/vulkan.json` or `build/vulkan.json`.
3. The newest valid local Intel Vulkan benchmark `aggregate.json` under
   `build/benchmarks/`.
4. On Windows only, an enabled Intel ICD registered by the Vulkan loader under
   `HKLM\SOFTWARE\Khronos\Vulkan\Drivers`. The launcher validates the manifest,
   driver library, and Vulkan probe, then records the verified choice in this
   worktree's ignored `build/dev-control/vulkan.json`.
   If that registry source is unavailable, it asks the installed Vulkan loader
   for its active Intel ICD and applies the same checks and local record.

A direct local JSON document supplies `icd_path`, `icd_sha256`, `gpu_name`, and
optionally the expected `graphics_driver_version` and `vulkan_api_version`.
Because `build/` is ignored, machine-specific paths and hashes remain local.
Fallback is diagnostic-only and requires `-Renderer fallback` on the start
wrapper or `--allow-fallback` on direct control commands.

The repository-root `Start LG Duel Client GPU.bat` uses this verified launcher
with `-ExternalServer`, so it owns only the client started alongside the
separate server batch. It never starts an unverified direct client or accepts
SDL_Renderer readiness.

Unverified manual launch (not accepted by GPU-required control/MCP workflows):

```powershell
Start-Process .\build\default\lg_duel_server.exe -ArgumentList 27960
$env:LG_DUEL_RENDER_BACKEND = 'gpu'
.\build\default\lg_duel_client.exe 127.0.0.1 27960 --dev-control --control-port 27961
```

The client refuses to start control if another process owns the port. Without
`--dev-control` (or `--control-port`) no listener exists.

## Map Editing Loop

Keep the existing synchronizer running in another terminal:

```powershell
.\scripts\watch-maps.ps1
```

It copies changed source maps from `maps/` into `build/default/maps/`. The
control layer intentionally does not replace that job. After an edit:

```powershell
.\scripts\lg-control.ps1 reload-map eyetoeye
.\scripts\lg-control.ps1 set-camera --position 0,22.4,-12.85 --yaw -90 --pitch 0 --fov 100
.\scripts\lg-control.ps1 set-collision-debug 2
.\scripts\lg-control.ps1 set-player-weapon railgun
.\scripts\lg-control.ps1 set-player-view --yaw -90 --pitch 0
.\scripts\lg-control.ps1 send-input --ticks 125 --forward 1
.\scripts\lg-control.ps1 wait-frames 2
.\scripts\lg-control.ps1 capture --name central-overview
.\scripts\lg-control.ps1 capture-map-views --map eyetoeye --preset standard
```

Add `--json` anywhere for compact automation output. Map names may contain only
letters, digits, `_`, `-`, and an optional `.map` extension. Capture names use
the same safe stem characters and always write PNG under `build/captures/`.

## Camera Presets

Repository presets live in `config/dev-camera-presets.json`. A viewpoint has a
safe name, optional label, three-number LG-unit position, yaw/pitch in degrees,
optional `30..140` FOV, and optional `hide_hud`/`hide_overlays` booleans. Pitch
is limited to `-89.9..89.9`.

The supplied `eyetoeye/standard` preset demonstrates three real viewpoints.
A multi-view response and its `*-manifest.json` contain the map/revision,
map content hash, renderer backend, preset, timestamps, every camera, PNG path,
dimensions, and individual success/error state. This binds the captures to both
the loaded structural map and the actual rendering path. A failed view does not
suppress the other views.

## Underlying Protocol

One TCP connection carries one UTF-8 JSON request terminated by a newline and
receives one response:

```json
{"id":"1","control_protocol":1,"operation":"set_camera","position":[12,8,4],"yaw":140,"pitch":-20,"fov":100}
```

Success and failure shapes are explicit:

```json
{"id":"1","ok":true,"result":{"mode":"development_camera","position":[12,8,4],"yaw":140,"pitch":-20,"fov":100}}
```

```json
{"id":"1","ok":false,"error":{"code":"invalid_request","message":"pitch must be between -89.9 and 89.9 degrees"}}
```

Operations are `status`, `load_map`, `reload_map`, `get_camera`, `set_camera`,
`exec_console`, `get_cvar`, `set_cvar`, `send_input`, `wait_frames`,
`set_player_view`, `set_player_weapon`, `set_collision_debug`,
`capture_screenshot`, and `capture_map_views`. The
`set_collision_debug` request requires an integer `mode`: `0` disables the
overlay, `1` shows all collision, `2` shows visible solids, `3` shows
`playerclip`, `4` shows `weapclip`, and `5` shows triggers. This bounded
operation changes only renderer visualization; it does not change authoritative
collision, movement, hit registration, or traces. Collision visualization
requires verified `SDL_GPU/vulkan`; the explicit SDL_Renderer fallback returns
`renderer_unsupported` rather than claiming the overlay is active. Status exposes
the requested mode, effective mode, and backend support independently. The
multi-view request includes a typed `views` array; CLI and MCP resolve the named
repository preset before making that single runtime request.

`send_input` uses `ticks` from `1..1250`. Its `forward`, `right`, and `up` axes
range from `-1..1`. Optional absolute yaw and pitch set the view, while attack,
jump, dash, crouch, sneak, and zoom use booleans. Supply yaw and pitch together.
An optional weapon token uses
the same weapon lookup as normal play. Input enters the usual client command
path; it does not write server state. `wait_frames` accepts `1..600` frames.
Console commands and cvar changes use the game's console checks. Console text
is limited to 1024 printable characters; cvar values allow 256. Neither can run
system shell text.

## MCP For Codex

Register the repository-local stdio server with a dynamically resolved path:

```powershell
.\scripts\setup-lg-mcp.ps1
```

The setup script resolves `python.exe` and the MCP server to absolute paths,
creates an isolated Python runtime under `build/lg-mcp-python`, installs the
MCP image package, registers the tool, verifies `lg-duel` in `codex mcp list`,
and prints the Windows host, user profile, registration scope, and exact Codex
config file. This makes host-user registration distinct from a different
shell, container, or `CODEX_HOME` profile.

Equivalent command shape (use the absolute paths printed by the script):

```powershell
codex mcp add lg-duel -- C:\absolute\path\to\python.exe C:\absolute\path\to\lg_mcp_server.py
```

Restart Codex or begin a new task after registration. Remove the local entry:

```powershell
.\scripts\setup-lg-mcp.ps1 -Remove
```

Tools include lifecycle (`lg_start`, `lg_stop`, `lg_restart`, `lg_status`), maps
and camera, `lg_exec_console`, `lg_get_cvar`, `lg_set_cvar`, `lg_send_input`,
`lg_wait_frames`, `lg_set_player_view`, `lg_set_player_weapon`, collision view,
screenshot, map-view capture, and benchmark tools. Each has
a closed typed JSON schema. Results include text plus `structuredContent`;
captures return compact WebP agent copies by default. The saved PNG stays at
full size and uses lossless PNG compression. The default agent copy uses quality 92
and at most 1,440,000 pixels (1600x900 for a 16:9 frame), records its source
and delivered sizes and dimensions, and can shrink further to stay within the
shared 1 MiB base64 budget. That budget covers all images in one reply, so a
map-view call can return several compact images when their combined data fits.
`inline_image_format`, `inline_image_max_pixels`, and
`inline_image_quality` tune the compact copy. Set `inline_image_mode: full` to
send the saved PNG unchanged when it fits the reply budget. A capture that
still cannot fit succeeds and returns its checked path, byte size, and
`inline_image_omitted: size_limit` instead of putting a large base64 value on
the MCP stream. Visual MCP tools start or attach through the shared verified
GPU launcher. Screenshot and multi-view capture cannot silently reuse an
SDL_Renderer, D3D11, SwiftShader, or other fallback client. Explicit fallback
requires `allow_fallback: true`.

The launcher reports success only after control protocol 1, the client and
server, the network connection, a named map, and a positive map revision are
ready. Normal control calls refuse to attach to a benchmark client, so captures,
input, and console commands cannot disturb an active run. Saved launcher state
uses atomic writes, a `starting` or `ready` phase, and process creation times.
A short file lock serializes start, restart, stop, and state repair. A second
change returns `lifecycle_busy`. Broken state blocks these changes instead of
being treated as no state. A stale or unverifiable process record never grants
permission to stop a PID. If a status probe fails while saved live state marks
a benchmark session, the launcher preserves both the state and its processes.
Cleanup signals all verified owned processes, waits up to five seconds once,
then forces survivors and waits up to two seconds once. If the processes stop
but the saved state file cannot be removed, `lg_stop` returns
`state_clear_failed: true` and `state_preserved: true`.

Normal visual MCP keeps server port `27960`, control port `27961`, and state in
`build/dev-control`. Benchmark MCP defaults to server port `28960`, control port
`28961`, and state in `build/benchmark-control/28960-28961`. A different valid
pair gets a different folder named `<server-port>-<control-port>`. Benchmark
tools reject equal, invalid, busy, or conflicting ports before launch. They
also reject an existing or broken state file instead of attaching to it. An
atomic pair claim blocks two benchmark runners from starting on the same pair.

`lg_run_benchmark` and `lg_create_benchmark_baseline` accept typed
`server_port` and `control_port` fields. The old `port` field remains an alias
for `control_port`; if both appear, their values must match. An owned benchmark
stops only the processes recorded in its pair-specific state folder before the
tool returns. A call which sets `start_client=False` is an internal external
session mode and never claims or stops a process. If owned cleanup fails, the
tool marks the saved aggregate invalid before it reports the error.

MCP live calls run in supervised child processes. Status has a 5-second limit,
stop 10 seconds, start 30 seconds, restart 40 seconds, normal control and
capture calls 90 seconds. A live scenario gets its accepted scenario timeout
plus 60 seconds. A benchmark gets one timeout for startup, one for each of up
to 100 runs, five seconds of per-step margin, and 60 seconds for setup and
summary work. The accepted per-step timeout is greater than zero and no more
than 3600 seconds. A
worker, tool, protocol, output, or timeout failure after a state-changing
worker starts returns `outcome: unknown`; check status or stop before trying
that change again.

The stdio reader stays active while a worker runs. Send
`notifications/cancelled` with the active `requestId` to cancel its worker; the
original request then returns JSON-RPC error `-32800`. Each worker runs in a
Windows Job Object or a POSIX process group, so cancel and stop end only that
worker and its children. A spawn-capable call does not start if this bound
worker tree cannot be set up. A cancelled call that could have changed state
reports `outcome: unknown`. The server permits one normal live call at a time
and returns `server_busy` for another. An accepted `lg_stop` cancels and joins
the normal worker, then runs in its own worker. A second concurrent stop
returns `server_busy`.

Worker stdout is capped at 4 MiB and stderr at 64 KiB. Structured tool output
is capped at 256 KiB before MCP framing. If a result is larger, it keeps short
status, summary, aggregate, and path fields and adds
`structured_output_omitted: size_limit`. The whole MCP result is capped at
2 MiB; inline images remain subject to the separate 1 MiB base64 budget. The
compact image limit applies to each reply, while the full PNG on disk keeps its
original pixels.

### MCP Map Editing API

The `lg_map_*` tools provide a small, typed edit path for source maps. Use
`lg_map_list` and `lg_map_get` for metadata, then `lg_map_create` to make a new
map from the known `initial` template. `lg_map_add_cuboid`,
`lg_map_copy_cuboid`, `lg_map_translate_cuboid`, `lg_map_resize_cuboid`, and
`lg_map_delete_cuboid` manage six-face, axis-aligned cuboids. Stable IDs identify
objects created by this API. `lg_map_set_material` changes all faces of a
managed cuboid to an allowed material. `lg_map_set_entity_properties` only
changes supported template entity fields (origin, angle/yaw, and typed bounds).
`lg_map_set_world_lighting` sets map ambient color and strength and can add,
edit, or remove the one sun. `lg_map_add_point_light`,
`lg_map_update_point_light`, and `lg_map_remove_point_light` manage local
lights. `lg_map_list_point_lights` and `lg_map_get_world_lighting` return typed
lighting state and the map revision.
`lg_map_add_teleport`, `lg_map_update_teleport`, and
`lg_map_remove_teleport` manage teleport trigger bounds, exit points, and exit
yaw. `lg_map_list_teleports` returns all managed teleports and the map revision.
Each teleport has one public stable ID. The writer makes its
`trigger_teleport`, linked `target_position`, internal `lg_agent_id` values, and
target name. Callers cannot supply raw target links. Trigger brushes always use
`common/trigger`. Bounds and exit points use TrenchBroom map units, must stay
inside world bounds, and the map can hold at most 16 teleports.

Lighting API colors use `0..255` channels. Canonical map text writes them as
normalized `0..1` values, so an API channel value of `1` stays distinct from
`255`. Positions, radius, and source radius use TrenchBroom map units.
Point-light intensity must be greater than 0 and at most 16; radius must be
greater than 0 and at most 4096. Each light can set a shadow flag, source radius,
priority from `-1000..1000`, and fixed-seed flicker. Flicker frequency is
`0.1..30` Hz when on; if left out it defaults to 8 Hz. Its min and max strength
factors use `0..4`. A managed map can hold at most 96 point lights. The API
writes the runtime keys `casts_shadows`, `source_radius`, `priority`, `flicker`,
`flicker_seed`, `flicker_frequency`, `flicker_min`, and `flicker_max`.

`lg_map_apply_batch` applies up to 128 closed, typed operations. Canonical
managed maps support cuboids, template entities, world light, point lights, and
teleports. Hand-authored maps support world light, point lights, and teleports.
The tool checks all steps against a private working copy, then makes one source
write and returns one rollback token. One bad step, a stale revision, or a cap
breach leaves the map unchanged.

The API reads and writes only the repository `maps/` source area and the fixed
runtime mirror `build/default/maps/`; callers cannot provide other paths or raw
map text. Each write uses an exact content revision and an `expected_revision`
precondition. Canonical managed maps use full canonical serialization.
Hand-authored maps use brace-, quote-, and comment-aware span patches. Ambient
light patches worldspawn values. Sun edits may add, replace, or remove the one
`light_sun` entity. New typed point lights and teleports append tagged
entities. Later updates replace only the matching typed entity spans. All other
text, entities, comments, brushes, and line endings stay intact. Pass
`dry_run: true` to validate and preview the structural/text diff without
writing. A successful write returns a rollback token; `lg_map_rollback`
restores the exact prior bytes only when its revision precondition still
matches. Writes check structure, bounds, limits, IDs, and typed fields before
an atomic replace.

New API-created maps use managed format v2. On its first successful write, the
API upgrades an exact canonical v1 managed map to v2 and adds the default white
0.3 ambient state. The rollback token restores the exact v1 bytes.
Hand-authored project maps do not need adoption and never gain a managed state
marker. `lg_map_get` reports `editing_mode: direct_non_lossy`, lists tagged
objects, and reports counts of unowned point lights and teleports.

Use `lg_map_validate` before loading. `lg_map_validate_sync_reload` then runs
validation, syncs the source to `build/default/maps/`, loads or reloads it, and
returns validation data, source/runtime hashes, and the authoritative loaded map
revision. Git actions remain outside this API.

Recommended agent check: validate, validate-sync-reload, wait for frames and
capture a screenshot or map views, enable collision debug, then check movement
and projectile behavior with the existing input and weapon tools.

On hand-authored maps, existing untagged point lights and teleports stay
read-only because position or entity order is not a stable edit key. The API
reports their counts and preserves their bytes. A mapper may adopt an
importer-compatible point light by adding a safe, unique `lg_agent_id` in
TrenchBroom. A legacy teleport cannot be adopted by a tag alone: the typed pair
needs its own trigger and target IDs plus link data. Recreate it with
`lg_map_add_teleport`. Hand maps also do not support cuboid, material, spawn, or
world-bound mutations through the API. Non-axis-aligned brushes, face-level
projection changes, other trigger and point classes, arbitrary map text, and
arbitrary filesystem paths remain outside the typed tools. Use supervised
TrenchBroom for those edits.

## Troubleshooting And Limitations

- **Connection refused:** launch with `--dev-control`, or use the start wrapper.
  Inspect `build/dev-control/client.stderr.log`.
- **Stale map:** keep `watch-maps.ps1` running, verify the build copy, reload,
  and confirm the response revision increased.
- **Map timeout:** local validation passed but the authoritative server did not
  activate the map. Inspect the server log; a remote server may use different
  maps or an incompatible build.
- **GPU verification failed:** inspect the selected ICD, expected/actual device,
  and Vulkan/client errors returned by the launcher. Repair the ignored local
  selection or create a valid benchmark baseline; do not review fallback PNGs
  as GPU output.
- **Blank/failed capture:** inspect the returned renderer/error. GPU capture
  supports SDL 3.4.10 SDR RGBA/BGRA swapchains. Use wrapper option
  `-Renderer fallback` only as an explicitly labelled driver diagnostic.
- **MCP tools fail:** visual tools supervise or verify the local client through
  the shared launcher. Structured errors distinguish launch, attachment,
  renderer attestation, and control-operation failures.
- **MCP call reaches its worker limit:** read the structured error and its
  `outcome`. If a state change has an unknown outcome, check status before
  repeating it. Use `lg_stop` to recover a launcher-owned session. If the MCP
  process itself is unavailable, use
  `python scripts/lg_launch.py --json stop`; this path does not probe the
  control socket.
- **Capture returns no inline image:** check
  `inline_image_omitted`. `size_limit` means the copy did not fit;
  `encoder_unavailable` means the MCP runtime needs setup again. Inspect the
  returned local path. The capture itself succeeded.

Known limitations: normal live requests use one worker slot; output is PNG
on disk; MCP uses control port 27961; and `eyetoeye/standard` is the only curated
preset. Cancellation cannot roll back a game change that already ran. Player
input needs a live match and follows the same server rules as local input.
Computer Use remains useful for unusual editor/window work, but routine
movement, console work, camera placement, and capture do not require it.
