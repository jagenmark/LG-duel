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

`start` uses `build/default`, attaches to an existing server from that exact
repository executable when possible, launches a hidden client with control
enabled, records ownership in `build/dev-control/processes.json`, and writes
logs beside it. `stop` stops only processes launched by the wrapper whose
current executable path still matches. It never kills an attached or unrelated
process.

Manual launch:

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
`capture_screenshot`, and `capture_map_views`. The multi-view request includes
a typed `views` array; CLI and MCP resolve the named repository preset before
making that single runtime request.

## MCP For Codex

Register the repository-local stdio server with a dynamically resolved path:

```powershell
.\scripts\setup-lg-mcp.ps1
```

Equivalent command:

```powershell
codex mcp add lg-duel -- python (Resolve-Path .\scripts\lg_mcp_server.py)
```

Restart Codex or begin a new task after registration. Remove the local entry:

```powershell
.\scripts\setup-lg-mcp.ps1 -Remove
```

Tools are `lg_status`, `lg_load_map`, `lg_reload_map`, `lg_get_camera`,
`lg_set_camera`, `lg_capture_screenshot`, and `lg_capture_map_views`. Each has
a closed typed JSON schema. Results include text plus `structuredContent`;
captures also return existing PNGs as MCP `image/png` content.

## Troubleshooting And Limitations

- **Connection refused:** launch with `--dev-control`, or use the start wrapper.
  Inspect `build/dev-control/client.stderr.log`.
- **Stale map:** keep `watch-maps.ps1` running, verify the build copy, reload,
  and confirm the response revision increased.
- **Map timeout:** local validation passed but the authoritative server did not
  activate the map. Inspect the server log; a remote server may use different
  maps or an incompatible build.
- **Blank/failed capture:** inspect the returned renderer/error. GPU capture
  supports SDL 3.4.10 SDR RGBA/BGRA swapchains. Try wrapper option
  `-Renderer fallback` to isolate GPU-driver issues.
- **MCP tools fail:** MCP intentionally does not supervise processes. Start the
  game first; tool errors remain actionable structured results.

Known limitations: requests are serial; output is PNG only; the development
camera is stationary (no remote free-flight input); MCP uses control port
27961; and `eyetoeye/standard` is the only curated preset. Computer Use remains
useful for unusual editor/window work, but routine reloads, camera placement,
and capture do not require it.
