# Rendering

Rendering is presentation-only. The renderer consumes arena data, predicted/interpolated player states, authoritative transient events, HUD state, console state, and cvar-derived `RenderSettings`.

## Scene Construction

`src/app/GameApp.cpp` prepares each frame: predicted local player, interpolated remote players, team/name/health presentation, lingering rail/machine-gun events, hit feedback, damage numbers, HUD, and console state. It then calls `Renderer::render()`.

3D scene geometry is built in `src/render/Scene3D.*`. `buildStaticWorldScene()` creates arena floor/bounds/renderable walls/renderable brushes with material ids, UVs, and static light coloring. Collision-only playerclip solids remain in arena data but are skipped before static lighting and vertex emission. `buildPerspectiveScene()` creates the first-person camera scene: dynamic players, weapons, beams, hitscan traces, projectile visuals, explosions, and optional lag-compensation bounds.

Screen-space UI uses `src/render/ScreenUi.*`, `ConsoleLayout.*`, `ChatLayout.*`, `BitmapFont.*`, and the retained 2D draw-list/overlay pipeline.

## GPU And Fallback Paths

`Renderer::initialize()` tries SDL_GPU only when `LG_DUEL_RENDER_BACKEND` requests `gpu`, `sdl_gpu`, or `vulkan`; otherwise it uses SDL_Renderer fallback. SDL_GPU creates pipelines, font texture, transfer/vertex buffers, depth texture, and static world cache. If GPU setup fails, it falls back to SDL_Renderer.

The SDL_GPU path caches static world geometry in `StaticWorldMesh`, keyed by `arenaStaticWorldFingerprint()`. It batches by material and uploads static world vertices/textures when the arena/material fingerprint changes. Dynamic 3D vertices are rebuilt/uploaded per frame into a bounded scratch path, followed by a 2D overlay pass for HUD, console, chat, scoreboard, settings, crosshair, and screen-space combat UI.

The SDL_Renderer fallback draws immediate geometry and does not have the same static-world GPU cache. It is simpler but less representative of the intended high-performance 3D path.

Opt-in developer captures use the real final render target. SDL_Renderer reads
pixels immediately before present. SDL_GPU schedules a swapchain-to-download
transfer after all render passes, submits with a fence, maps only after that
fence completes, normalizes RGBA/BGRA, and writes PNG. This synchronous readback
exists only for an explicit capture request; ordinary frames allocate no
capture buffer and never wait on a capture fence.

## Player Outlines

SDL_GPU player outlines are object-mask-based screen-space outlines. `Scene3D` records outline eligibility separately from normal player materials, including enemy/teammate group, visibility mode, alpha, pulse, and pixel width. The mask pass redraws the same already-built player body vertex ranges used by the normal world pass; it does not generate expanded outline meshes or rebuild player geometry for outlines.

The SDL_GPU frame renders the world depth/color pass, then uses a bounded screen-space work rectangle for outline work. Style `r_player_outline_style 1` allocates persistent half-resolution mask, dilation, and depth targets sized to `ceil(framebuffer * 0.5)`. The active rectangle is derived from the projected mask input vertices, expanded by outline/filtering/safety margins, and falls back to the full target if projection is unreliable or intersects the camera/near plane.

Within that half-resolution rectangle, the renderer clears mask/depth deterministically, rebuilds opaque depth, redraws eligible player body ranges into the mask with depth testing, runs a fixed 7x7 dilation kernel, and finally composites the contour into the full-resolution scene with a matching full-resolution scissor. Public outline width cvars remain final display pixels and are converted to half-resolution radii internally. The SDL_Renderer fallback does not implement this mask path; style `r_player_outline_style 0` keeps the old approximate geometry fallback as explicit legacy behavior.

## Textures And Materials

Material ids are produced by `arenaMaterialId()`. GPU texture loading scans the `textures` directory, creates aliases with and without `.png`, uploads PNGs, and uses fallback checker/white textures when needed. UV projection comes from `TextureProjection` on arena walls/brush faces, with fallback axis projection when missing.

Texture and light debug output are gated by environment variables such as `LG_DUEL_TEXTURE_DEBUG`, `LG_DUEL_TEXTURE_DEBUG_UV`, `LG_DUEL_TEXTURE_DEBUG_FORCE_MATERIAL`, and `LG_DUEL_LIGHT_DEBUG`.

## Performance Assumptions

- Static world geometry should be rebuilt only when the arena fingerprint changes.
- Collision-only playerclip brushes must not add static world vertices, material references, texture loads, or batches.
- Dynamic scene geometry is per-frame and should stay bounded by player/projectile/effect counts.
- Projectile visuals are cheap boxes/spheres/wire boxes, not unique high-poly assets.
- GPU frames use `kMaxGpuVertices`; exceeding this budget should be treated as a rendering bug or content budget issue.
- Debug overlays/logging must remain gated and off by default.

## Footguns

- Do not rebuild/upload static world buffers every frame on the GPU path.
- Do not add high-vertex dynamic meshes for frequent effects without a budget and caching strategy.
- Static lighting is baked into generated vertex colors at world-scene build time; changing light data should change the arena fingerprint or otherwise trigger rebuild.
- Keep visual-only changes out of server simulation and network authority.
