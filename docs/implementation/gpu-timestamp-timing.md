# GPU timestamp timing

LG Duel uses a small SDL 3.4.10 patch to read GPU timestamps from Vulkan. The
patch stays off in normal local builds. It is on in the `perf` and
`gpu-timing` presets.

## SDL API

The patch adds these public types:

- `SDL_LGGPUTimestampQueryPool` is an opaque pool handle.
- `SDL_LGGPUTimestampQueryInfo` gives `timestamp_valid_bits` and
  `timestamp_period_nanoseconds`.
- `SDL_LGGPUTimestampResult` reports `UNSUPPORTED`, `ERROR`, `NOT_READY`, or
  `AVAILABLE`.
- `SDL_LGGPUTimestampLocation` selects `TOP_OF_PIPE` or `BOTTOM_OF_PIPE`.

It also adds these calls:

- `SDL_LG_GetGPUTimestampQueryInfo(device, info)`
- `SDL_LG_CreateGPUTimestampQueryPool(device, query_count)`
- `SDL_LG_ReleaseGPUTimestampQueryPool(device, pool)`
- `SDL_LG_ResetGPUTimestampQueries(command_buffer, pool, first, count)`
- `SDL_LG_WriteGPUTimestamp(command_buffer, pool, index, location)`
- `SDL_LG_GetGPUTimestampQueryResults(device, pool, first, count, values)`

The result call writes one packed `Uint64` value for each query. The caller
must use the values only when the call returns `AVAILABLE`. It never asks the
driver to wait.

Raw values use the low `timestamp_valid_bits`. Code that takes a time span must
mask the values, handle wrap, then multiply the tick span by
`timestamp_period_nanoseconds`.

## Ownership and ring state

The device owns each pool. A pool works only with the device and Vulkan queue
that made it. Query ranges must stay within the pool.

The caller must not release a pool while a recording or queued command buffer
uses it. SDL tracks live pools and destroys any pool left at device shutdown,
after its device waits have made that safe. This shutdown cleanup does not
make early release safe.

Frame timing should use a ring with one start and one end query per slot. Each
slot should move through these states:

1. Free
2. Recorded
3. Submitted
4. Pending
5. Available

Do not reset a slot until its old result has become available and the slot can
return to `Free`. If a pending slot is not ready, skip that sample. Do not wait
for it. The ring size should cover the set frame flight limit plus enough room
for late reads.

## Timestamp placement

Record the reset and the `TOP_OF_PIPE` write after command buffer acquire and
before the frame's GPU work. Record the `BOTTOM_OF_PIPE` write after the last
GPU command for the frame and before submit.

This span covers work in that command buffer. It does not claim to time display
scanout or work outside the measured command buffer.

## Back-end behavior

Only Vulkan implements the added driver calls. It uses the timestamp bit count
from SDL's chosen queue, the device timestamp period, and Vulkan timestamp
query pools. Reads use `VK_QUERY_RESULT_64_BIT` without
`VK_QUERY_RESULT_WAIT_BIT`.

Metal, Direct3D 12, and other back ends leave the new driver calls unset. SDL
still creates those devices. The info and result calls return `UNSUPPORTED`;
pool create and command calls fail without changing device setup.

## Patch selection and identity

The patch file is `third_party/sdl3-gpu-timestamps.patch`. It applies only to
SDL commit `8e37db5e797b6167f3a00d697d816a684bd259c7`, the SDL 3.4.10 base used by
this project.

`LG_DUEL_USE_PATCHED_SDL3` is off by default. When it is on, CMake does not use
an installed SDL package. It uses `LG_DUEL_SDL3_SOURCE_DIR`, or fetches the
exact base commit when `LG_DUEL_FETCH_SDL3` is on. Configure checks the commit,
applies the patch once, accepts a prior application, and stops on a wrong base
or a patch conflict.

The target gets these compile definitions:

- `LG_DUEL_SDL_GPU_TIMESTAMP_EXT` is `1` for the patch and `0` otherwise.
- `LG_DUEL_SDL_BASE_REVISION` holds the base commit for a patched build.
- `LG_DUEL_SDL_PATCH_IDENTITY` holds the patch text's SHA-256 identity after
  line endings have been set to LF.

Unpatched builds set the two identity strings to empty values.
