# Developer Control And Visual Capture

LG Duel has an opt-in, local developer-control plane for map iteration and
agent-driven visual inspection. It is independent of MCP: the running client
owns a small structured socket service, `lg-control.ps1` is a direct client,
and `lg_mcp_server.py` is a thin MCP-to-socket adapter.

## Architecture And Trust Boundary

The client starts the service only with `--dev-control` or `--control-port`.
Normal launches do not open a control port. The listener binds only to
`127.0.0.1`, rejects non-loopback peers, accepts one bounded JSON-line request
at a time, and has no shell, process-launch, arbitrary file, or generic console
operation. Requests are limited to 64 KiB and use control protocol version 1.

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
`set_collision_debug`, `capture_screenshot`, and `capture_map_views`. The
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

## MCP For Codex

Register the repository-local stdio server with a dynamically resolved path:

```powershell
.\scripts\setup-lg-mcp.ps1
```

The setup script resolves `python.exe` and the MCP server to absolute paths,
registers them, verifies `lg-duel` in `codex mcp list`, and prints the Windows
host, user profile, registration scope, and exact Codex config file. This makes
host-user registration distinct from a different shell, container, or
`CODEX_HOME` profile.

Equivalent command shape (use the absolute paths printed by the script):

```powershell
codex mcp add lg-duel -- C:\absolute\path\to\python.exe C:\absolute\path\to\lg_mcp_server.py
```

Restart Codex or begin a new task after registration. Remove the local entry:

```powershell
.\scripts\setup-lg-mcp.ps1 -Remove
```

Tools are `lg_start`, `lg_status`, `lg_load_map`, `lg_reload_map`, `lg_get_camera`,
`lg_set_camera`, `lg_set_collision_debug`, `lg_capture_screenshot`, and
`lg_capture_map_views`. Each has
a closed typed JSON schema. Results include text plus `structuredContent`;
captures also return existing PNGs as MCP `image/png` content. Visual MCP tools
start or attach through the shared verified GPU launcher. Screenshot and
multi-view capture cannot silently reuse an SDL_Renderer, D3D11, SwiftShader,
or other fallback client. Explicit fallback requires `allow_fallback: true`.

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

Known limitations: requests are serial; output is PNG only; the development
camera is stationary (no remote free-flight input); MCP uses control port
27961; and `eyetoeye/standard` is the only curated preset. Computer Use remains
useful for unusual editor/window work, but routine reloads, camera placement,
and capture do not require it.
