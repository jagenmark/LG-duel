# Rendering

Rendering is presentation-only. The renderer consumes arena data, predicted/interpolated player states, authoritative transient events, HUD state, console state, and cvar-derived `RenderSettings`.

## Scene Construction

`src/app/GameApp.cpp` prepares each frame: predicted local player, interpolated remote players, team/name/health presentation, lingering rail/machine-gun events, hit feedback, damage numbers, HUD, and console state. It then calls `Renderer::render()`.

3D scene geometry is built in `src/render/Scene3D.*`. `buildStaticWorldScene()` creates arena floor/bounds/renderable walls/renderable brushes with material ids, UVs, and static light coloring. Collision-only playerclip solids remain in arena data but are skipped before static lighting and vertex emission. `buildPerspectiveScene()` creates the first-person camera scene: dynamic players, weapons, beams, hitscan traces, projectile visuals, explosions, and optional lag-compensation bounds.

Faces marked as sky openings keep their collision planes but do not add
triangles to any static world mesh output. This rule covers the main world
pass, depth, shadows, visibility chunks, material batches, and wire data made
from that static scene.

Screen-space UI uses `src/render/ScreenUi.*`, `ConsoleLayout.*`, `ChatLayout.*`, `BitmapFont.*`, and the retained 2D draw-list/overlay pipeline.

## GPU And Fallback Paths

`Renderer::initialize()` tries Vulkan SDL_GPU only when `LG_DUEL_RENDER_BACKEND` requests `gpu`, `sdl_gpu`, or `vulkan`; otherwise it uses the explicitly selected SDL_Renderer fallback. SDL_GPU creates pipelines, font texture, transfer/vertex buffers, depth texture, and static world cache. An explicit GPU request fails closed if Vulkan or GPU resource initialization fails; it never changes renderer class to SDL_Renderer or another SDL_GPU driver.

The SDL_GPU path caches static world geometry in `StaticWorldMesh`, keyed by `arenaStaticWorldFingerprint()`. It builds a renderer-owned BVH over immutable triangle chunks and packs those chunks material-major without changing collision or gameplay authority. Experimental `r_world_frustum_cull 1` queries that BVH, but adaptively retains full material batches unless triangle savings justify extra ranges. It remains opt-in until the measured flythrough beats the `0` control path. Dynamic 3D vertices are rebuilt/uploaded per frame into a bounded scratch path, followed by a 2D overlay pass for HUD, console, chat, scoreboard, settings, crosshair, and screen-space combat UI.

The SDL_Renderer fallback draws immediate geometry and does not have the same static-world GPU cache. It is simpler but less representative of the intended high-performance 3D path.

## GPU Sky

An arena may pick one client sky cube. SDL_GPU draws it as a full-screen
triangle at the start of the main world pass. The ray data has camera right,
up, forward, focal length, and aspect ratio. It has no camera position, so
moving the camera cannot move the sky. The sky pipeline has no vertex buffer,
blend, depth test, depth write, shadow input, or world light input.

The HDR path writes linear sky color into the scene target before later tone
mapping. The direct path uses its display curve in the sky shader. The HDR
pipeline uses the active main-pass sample count, including MSAA rebuilds.

The renderer loads a selected cube only on first use and keeps one load state
per sky id. It reuses a loaded texture across map changes. It also remembers a
failed load, so a missing or bad asset does not cause file work each frame. A
failed load leaves the normal clear color in place and does not stop the
client.

Opt-in developer captures use the real final render target. SDL_Renderer reads
pixels immediately before present. SDL_GPU schedules a swapchain-to-download
transfer after all render passes, submits with a fence, maps only after that
fence completes, normalizes RGBA/BGRA, and writes PNG. This synchronous readback
exists only for an explicit capture request; ordinary frames allocate no
capture buffer and never wait on a capture fence.

## Player Outlines

SDL_GPU player outlines are object-mask-based screen-space outlines. `Scene3D` records outline eligibility separately from normal player materials, including enemy/teammate group, visibility mode, alpha, pulse, and pixel width. Both screen-space paths redraw already-built player body ranges used by the normal world pass. Native mode also marks the player's already-built opaque held-weapon instances, so body and weapon form one outer silhouette. Compatibility mode keeps its old body-only source. The screen-space paths do not generate expanded outline meshes or rebuild player geometry.

`r_player_outline_mode 0` disables every player outline path. Mode `1` keeps the exact path selected by `r_player_outline_style`: style `0` is the old geometry fallback and style `1` is the old half-resolution screen-space path. Mode `2` forces the new native output-resolution screen-space path and ignores the style selector. The Default profile uses mode `2`. If the backend or native pipeline resources are unavailable, the renderer switches mode `2` to geometry compatibility mode and reports the reason in renderer diagnostics.

The SDL_GPU frame renders the world depth/color pass, then uses a bounded screen-space work rectangle. The old screen path keeps persistent targets at `ceil(framebuffer * 0.5)`. The native path uses the exact output width and height, including odd sizes. The active rectangle comes from projected body mask inputs plus width, filter, and safety margins. Bad projection or a camera/near-plane overlap uses the safe full-target fallback.

The old screen path clears its mask and depth, rebuilds opaque depth, then draws the eligible body mask with `LESS_OR_EQUAL`. Native mode stores and reuses the main full-resolution scene depth, clears only its color mask, and draws the body and weapon mask with the same depth rule. `VisibleOnly` thus stays hidden by opaque scene depth in both paths. The old path takes six render passes. Native mode takes three with single-sample depth reuse and six when MSAA needs a one-sample depth rebuild. A fixed 7x7 pass grows the group IDs, and the final pass draws only pixels outside the source mask. Native mode uses nearest integer mask reads, picks the nearest source group, and clamps coverage as `width + 0.5 - distance`. It skips samples outside the largest active radius because they cannot change the result. `r_player_outline_width` sets both groups in final output pixels. `r_player_outline_debug_mask 1` shows the raw native source group mask.

The half-resolution path remains useful as a cost and compatibility check, but each mask texel covers about four output pixels. Nearest sampling keeps group IDs stable, yet it also turns that coarse binary mask into visible square steps when the contour returns to full size. Native mode removes that rescale while keeping the same mask source and depth rules. The SDL_Renderer fallback has no screen-space mask path.

## Textures And Materials

Material ids are produced by `arenaMaterialId()`. GPU texture loading scans the `textures` directory, creates aliases with and without `.png`, uploads PNGs, and uses fallback checker/white textures when needed. UV projection comes from `TextureProjection` on arena walls/brush faces, with fallback axis projection when missing.

Texture and light debug output are gated by environment variables such as `LG_DUEL_TEXTURE_DEBUG`, `LG_DUEL_TEXTURE_DEBUG_UV`, `LG_DUEL_TEXTURE_DEBUG_FORCE_MATERIAL`, and `LG_DUEL_LIGHT_DEBUG`.

### Authored skinned-model materials

Skinned GLB models may use a `material-manifest.json` beside the model. The
loader validates the model filename, model-local PNG paths, image dimensions,
colour-space intent, optional atlas cells, packed-mask contract, and material
index/name bindings. A bad or missing manifest leaves the GLB loaded through
the flat path. The renderer never selects the authored path by a model path or
by a special material name.

The supported subset is deliberately small: base colour factor, opaque state,
optional sRGB albedo, optional linear packed mask, roughness, metallic,
emissive factor, explicit flat-tint weight, and `texcoord0` or a material-cell
atlas policy. It does not support normal maps, alpha blending, clear coat,
sheen, transmission, or image data inside a GLB.

Worker uses two shared 512 by 512 RGBA8 atlases: sRGB albedo and linear packed
mask. Its reviewed GLB has finite but degenerate `TEXCOORD_0` values, so the
manifest maps each approved GLB material to a padded 4 by 4 atlas cell. This
keeps the GLB, geometry, skeleton, animations, and weapon socket unchanged.
It also means this first slice gives each material a broad authored region,
not a unique painted UV layout.

### Blender, glTF, and game axes

Do not copy a Blender axis name into runtime animation code. Blender uses Z up,
while glTF and the imported Worker data use Y up. Blender's export conversion
maps axes as follows:

| Blender | glTF model space | Worker meaning in `Scene3D` |
| --- | --- | --- |
| `+X` | `+X` | model right |
| `+Y` | `-Z` | model forward |
| `+Z` | `+Y` | model up |

The Worker lean actions are the key example. Blender authors their accepted
34-degree abdomen bend around armature `+Y`. Runtime must therefore apply the
same bend around glTF `-Z`, not glTF `Y`. A glTF `Y` rotation turns the torso
around its upright axis and can look like no lean or a forward bend after pose
blending.

The model-instance matrix then maps glTF `+X`, `+Y`, and `+Z` to the player's
world right, up, and forward axes. Check a change after export in the GLB and
in the GPU client. Do not infer runtime axes from Blender labels alone.

| Quality | Albedo and mask | Team tint and highlights | Metal and environment |
| --- | --- | --- | --- |
| 0 | No authored samples; texture-free flat pipeline | Existing diffuse sun and point lights only | Off |
| 1 | Sample both atlases | Authored tint plus roughness-aware restrained highlights | Metal and environment off |
| 2 | Sample both atlases | Same tint and complete restrained response | Metal on; environment deferred |

The albedo sampler uses an sRGB GPU format, so sampling performs the only
albedo decode. Vertex base colour and team colour are converted to linear in
the shader. The packed mask stays linear. Lighting, tint, haze, and final tone
mapping remain in the existing linear path. The tint operation uses the
albedo value to retain broad light/dark separation instead of multiplying the
whole player by team colour.

The packed-mask channels are R team-tint weight, G perceptual roughness, B
metallic weight, and A reserved emissive weight. Worker keeps A at zero. The
two atlas textures and one sampler belong to the model GPU resource: they load
once, are shared by all Worker instances, are never uploaded per instance or
per frame, and release with that model resource. A missing or rejected texture
keeps the model on the flat pipeline and creates neutral white/zero-tint,
rough, non-metal fallback textures for safe descriptor use. Shadow and outline
passes remain texture-free.

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
- Convert Blender axes to glTF axes before adding model-space animation math.
