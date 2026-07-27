# Visual Overhaul Backlog

## Goal and scope

Keep LG Duel clean, stylised, and led by clear silhouettes. Preserve team-colour clarity and competitive visibility at every quality level. Build changes in small, testable steps, and enable optional work only where the current renderer already supports it.

Priority order:

1. Reliable, readable foundation.
2. Grounded lighting and material response.
3. Better combat response without visual noise.
4. Atmosphere and silhouette support.

## Graphics profiles

All profiles must keep the same team-colour rules, gameplay cues, and readable player silhouettes.

| Profile | Intended use | Required visual set |
| --- | --- | --- |
| Default | Standard play | Stable directional shadows, anti-aliasing, contact shadows where affordable, restrained ambient occlusion, tone mapping, basic grading, and selective bloom. |
| Low | Lowest practical cost | Team colours, silhouettes, anti-aliasing choice appropriate to the renderer, simplified or disabled contact shadows/AO, reduced particles and atmosphere. |
| Competitive | Maximum clarity | Team colours and silhouettes first; stable shadows, minimal grading, restrained or disabled bloom and atmosphere, tightly capped combat effects. |
| High | Best supported look | Default set plus higher shadow and material quality, contact shadows, restrained AO, denser but capped particles, decals, atmospheric depth, and stylised rim light. |

Render scale belongs to settings and benchmark work only. Support and measure a typical 50% to 150% range, with 100% as the default. Do not treat scale as a new visual feature.

## Phase 1 - Foundation and benchmark profiles

### 1.1 Define profile settings and fallbacks

**Priority:** P0
**Scope:** Document and wire settings only where renderer support already exists. Keep unsupported options staged as backlog, not new renderer work.

**Acceptance checks:**

- Default, Low, Competitive, and High have named, documented setting targets.
- Each profile has a safe fallback for unsupported options.
- Switching profiles does not change team colours, player outline rules, HUD readability, or gameplay timing.

### 1.2 Establish benchmark scenes and capture method

**Priority:** P0
**Scope:** Choose repeatable scenes: duel lane, close combat, projectile-heavy exchange, dark/light map areas, and camera pan/turn paths.

**Acceptance checks:**

- Each scene has a fixed start point, camera path, duration, and capture settings.
- Capture frame time, average FPS, 1% low FPS, and frame-time spikes per profile at 50%, 100%, and 150% render scale where available.
- Results record hardware, resolution, build, map, profile, and effect load.

### 1.3 Set readability baseline

**Priority:** P0

**Acceptance checks:**

- Testers can identify team, player facing, weapon fire, impact point, and key projectile path in stills and live play.
- Competitive profile remains clear during close-range fights and camera motion.
- Any change that harms team recognition, target tracking, or hit feedback fails review until corrected or removed.

## Phase 2 - Lighting and grounding

### 2.1 Stable directional shadows

**Priority:** P0
**Scope:** Use stable directional shadows supported by the renderer. Focus on camera-movement stability before higher detail.

**Acceptance checks:**

- Shadows do not visibly crawl, jump, or swim during the benchmark camera paths beyond the agreed renderer limit.
- Moving the camera does not cause distracting shadow changes around players or key cover.
- Low and Competitive use a stable, lower-cost shadow choice; High may raise quality within budget.

### 2.2 Contact shadows and restrained ambient occlusion

**Priority:** P1
**Scope:** Stage only if existing support allows it. Use each to ground players and props, not to darken the whole image.

**Acceptance checks:**

- Contact points read clearly under players, weapons, and nearby props.
- AO does not muddy team colours, crush black detail, or hide targets in corners.
- Competitive can reduce or disable either effect without loss of essential gameplay cues.

### 2.3 Tone mapping, grading, and stylised rim light

**Priority:** P1
**Scope:** Apply restrained tone mapping and grading. Use rim light only to separate silhouettes from similar-value backgrounds.

**Acceptance checks:**

- Bright effects retain detail and dark areas keep readable shape.
- Grading keeps both teams distinct across maps and lighting conditions.
- Rim light improves outline separation without looking like a constant glow or overriding team colour.

## Phase 3 - Materials and final image

### 3.1 Material roughness and normal response

**Priority:** P1
**Scope:** Tune existing material inputs so surfaces show clear stylised form under game lighting.

**Acceptance checks:**

- Roughness and normals add shape without noisy sparkle, shimmer, or harsh highlights.
- Team-coloured materials stay easy to read at near and far combat ranges.
- Material detail holds up during motion and at 50%, 100%, and 150% render scale.

### 3.2 Anti-aliasing and final-image pass

**Priority:** P0

**Acceptance checks:**

- The chosen anti-aliasing mode reduces edge shimmer on players, weapons, and thin level detail.
- It does not blur aiming cues, team colours, UI, or fast projectiles beyond the accepted readability baseline.
- Default and Competitive have a tested stable choice; Low has a lower-cost fallback.

### 3.3 Selective bloom

**Priority:** P1
**Scope:** Bloom only high-value gameplay lights and effects; keep it off broad scene detail.

**Acceptance checks:**

- Muzzle flashes, impacts, and gameplay lights gain emphasis without washing out nearby targets.
- Competitive uses minimal or no bloom as needed for clarity.
- Bright stacked effects stay within the effect budget.

## Phase 4 - Combat response

### 4.1 Temporary gameplay lights

**Priority:** P1
**Scope:** Add only where existing renderer support allows it. Use short-lived lights for key weapon fire and major impacts.

**Acceptance checks:**

- Lights help locate combat events without lighting whole rooms or masking enemy silhouettes.
- Per-frame light count has a hard cap by profile.
- Light-heavy benchmark scenes stay within the frame-time budget.

### 4.2 Layered impacts and muzzle particles

**Priority:** P1
**Scope:** Use short, clear layers: core flash, direction cue, debris/spark cue, and brief fade. Coordinate around the active barrel-effects task; do not overlap or change its owned work.

**Acceptance checks:**

- Each weapon event reads at a glance without obscuring the target or crosshair.
- Particle counts, life, spawn rate, and overdraw have per-profile caps.
- Competitive uses the clearest reduced set, with no loss of hit direction or fire cue.

### 4.3 Decals

**Priority:** P2
**Scope:** Stage only if existing support allows it. Keep decals brief, small, and useful for impact context.

**Acceptance checks:**

- Decals do not make surfaces look dirty or hide important map markers.
- A limit controls active decal count and replacement order.
- Low and Competitive may reduce or disable decals without losing hit confirmation.

## Phase 5 - Atmosphere and silhouettes

### 5.1 Atmospheric depth

**Priority:** P2
**Scope:** Add light, local depth cues only where supported. Do not turn play spaces into foggy scenes.

**Acceptance checks:**

- Atmosphere separates foreground, midground, and background while targets remain easy to spot.
- It does not hide projectiles, team colours, or distant players.
- Competitive reduces it to the minimum useful level; Low can disable it.

### 5.2 Silhouette review across maps

**Priority:** P0

**Acceptance checks:**

- Review player silhouettes against each common map colour and light condition.
- Fix conflicts with lighting, material values, local atmosphere, or rim light before adding more detail.
- Close, mid, and long-range captures meet the Phase 1 readability baseline in every profile.

## Benchmark, readability, and effect-budget gates

Apply these gates to every completed item:

- Run all defined benchmark scenes for each affected graphics profile.
- Compare 50%, 100%, and 150% render scale where the setting exists; retain 100% as default unless data supports a change.
- Record frame time, average FPS, 1% low FPS, spikes, and peak counts for lights, particles, decals, and visible transparent effects.
- Set and enforce per-profile caps for temporary lights, particle instances, particle spawn rate, decal count, particle lifetime, and transparent overdraw.
- Test effect-heavy combat at close range, at distance, and during fast camera movement.
- Review still captures and live play for team recognition, target tracking, crosshair clarity, weapon direction, projectile path, and impact location.
- Reject effects that add visual noise, unstable shadows, visible flicker, blur, or a cost beyond the agreed budget.

## Explicit exclusions

- No ray tracing.
- No heavy cinematic effects.
- No depth of field.
- No motion blur.
- No changes to gameplay rules, weapon balance, HUD meaning, or team-colour system.
- No renderer rewrite to unlock a feature in this backlog.
- No overlap with the active barrel-effects task; coordinate boundaries before any related combat-effect work.

## Deferred backlog - combat polish

Do after the five phases pass their gates and only when there is clear renderer support and budget headroom:

- Weapon-specific impact variants that retain the shared readable core.
- Surface-aware impact particles and decals.
- Small hit-confirmation refinements that do not alter gameplay timing or HUD meaning.
- Profile-tuned particle palettes and lifetimes.
- Better projectile trail shaping with strict overdraw limits.
- Optional environment response to major impacts, such as brief debris or light, within caps.
- Further barrel-effect polish after its active task completes and ownership is clear.
