# `.lgdemo` format and validation

## Versioning contract

`.lgdemo` is the saved-demo container. Format version 5 is the only
accepted format in `lg::replay`. Versions 1 through 4 are historical only and the
decoder rejects them. The v5 wire contract is fixed by `ReplayCodec`:

- magic bytes: `LGDM`;
- format version: `5` (`kReplayFormatVersion`);
- fixed tick rate: `125` (`kReplayTickRate`);
- byte order: little endian for every fixed-width value;
- saved-file cap: 512 MiB; chunk cap: 8 MiB; tick cap: 4,194,304; checkpoint cap:
  4,096; and lag-history cap: 256 frames; and
- chunk checksum: CRC-32 of the payload.

Version 5 uses explicit field order, fixed-width values, and a declared byte
order. It never writes C++ struct memory to disk. Padding, host endianness, ABI
layout, pointer size, and enum size must not affect a file.

Version 5 stores each player score as a signed 16-bit value. This keeps negative
Free For All scores and their exact two-byte form across checkpoints.

An old file need not play on a newer build. The current reader does not decode
versions 1 through 4. It fails before restoring any state and says why.

## Preamble and metadata

The 16-byte preamble contains these fields in v5 order:

1. `LGDM` magic;
2. 16-bit format version;
3. 16-bit tick rate;
4. 32-bit metadata byte length; and
5. 32-bit format flags.

The following bounded metadata payload repeats its flags and then stores, in
order: protocol revision, build fingerprint, gameplay configuration hash,
replay simulation revision, initial server tick, map revision, map name, map
content hash, game mode, match rules, visibility policy, stop reason,
configuration revision, the complete `ReplayGameplayConfig`, and fixed-slot
player metadata. Player metadata holds slot, occupied marker, bot marker, team,
and bounded name.

Strings and metadata lists carry a length and a stated maximum. V5 stores every
authoritative balance/runtime configuration field with explicit fixed-width
encoding. The canonical config hash covers those encoded fields. Playback
applies the payload to its replay-only server and rejects a hash mismatch.

## Chunks

After metadata, the file contains length-delimited chunks. Each v5 chunk holds a
one-byte type, a 32-bit payload length, a 32-bit CRC-32, and the payload. It has
no v5 chunk flags, compression, expansion length, index, or completion record.
The five chunk types are:

- `TickInputs`, one resolved input frame at a tick;
- `Checkpoint`, including the initial and any periodic bot-free checkpoint;
- `StateHash`, a canonical hash at a tick; and
- `LethalEvent`, lethal metadata with generation, sequence, cause, and projectile sequence;
- `AuthorityBoundary`, a pre-simulation checkpoint plus the authority/config
  state needed for a roster, rule, mode, reset, or configuration change.

### Sparse tick inputs

A v5 `TickInputs` payload starts with its 32-bit tick and a 16-bit present-slot
mask. It then encodes a `ReplaySlotInput` only for each set bit, in ascending
slot order. A clear bit has no input payload; decoding leaves that slot at its
default state with `present == false`.

The mask may not set bits outside the fixed player-slot range. Absent slots are
default-only: validation rejects non-default command, edge, or timing data for
an absent slot instead of silently treating it as an actor. This keeps an empty
slot from carrying stale input across a disconnect or restore.

Authority boundaries apply before simulation of their tick. Periodic
checkpoints remain seek aids; playback does not apply them as control changes.
Map changes end the current replay generation and do not create a cross-map
boundary in one demo.

The writer emits records by type. Tick inputs, checkpoints, hashes, and lethal
events each keep their own valid tick order. A checkpoint’s tick and every input
tick must not precede the initial tick. V5 does not compress records.

## Strict reader rules

The decoder must validate the whole candidate file before it changes the
destination `ReplayDemo`. It must reject:

- wrong magic, unsupported format version, wrong tick rate, or a preamble that
  does not fit the file;
- a short, malformed, or trailing payload; bad chunk type; or bad checksum;
- declared lengths, counts, strings, checkpoint counts, history length, or
  total file sizes over their configured bounds;
- non-finite floats, invalid enum values, invalid player/projectile indices, or
  impossible slot or sequence values;
- a present-slot mask with bits outside the player range, or non-default input
  state for an absent slot;
- command, checkpoint, hash, or event ticks that are out of order;
- map, content, configuration, tick-rate, protocol, or build compatibility
  failures; and
- an incomplete checkpoint, a cross-generation segment, or a required field
  that the version cannot read.

Validation happens before allocation where possible. Bounded allocations and
count checks come before decode loops. The decoder does not repair corrupt data,
skip unknown required records, or apply the valid prefix of a bad checkpoint.

## File helpers

`ReplayFile` provides `saveDemoFile` and `loadDemoFile`. Saving encodes a
`ReplayDemo`, creates a uniquely named temporary file with exclusive creation,
flushes it, then publishes the final name without replacing an existing
recording. A collision or failed publish removes the temporary file and reports
a clean error. Loading reads the file and uses the strict decoder above. These
calls can allocate and block on disk, so `ServerGame::tick` must not call them.

The saved `.lgdemo` cap is 512 MiB. The recorder’s native resident cap is also
512 MiB. A long full recording can approach that amount, but the recorder stops
cleanly before it exceeds its configured cap and preserves no partial final
recording.

No app command, console control, automatic match recording setting, or
background save/load job calls these helpers yet. The helpers therefore do not
make saved demos a player-facing feature.

## Compatibility and clean failure

The decoder checks the format version and tick rate. Checkpoint restore validates
the complete checkpoint before it changes server state. It checks map
name/content hash/revision, gameplay configuration hash, game mode, player
occupancy, lag history, and bounded spawn state. Any mismatch ends playback
cleanly with a specific diagnostic, such as `replay version is incompatible` or
`replay checkpoint does not match the loaded map or metadata`.

Metadata stores protocol revision and build fingerprint. Current checkpoint
restore does not yet reject them itself; broader build/protocol compatibility
policy remains pending.

If the verifier finds a state-hash mismatch after a valid load, it reports a
**divergent demo** rather than treating it as file corruption. It stops at the
first divergent tick and reports the first major state group that differs. It
does not continue with an unverified state.

## Required format coverage

`lg_duel_replay_codec_tests` covers v5 round trips, full custom configuration,
authority boundaries, lethal provenance/sequence, truncation with no partial
apply, checksum corruption, wrong magic, non-finite command data, invalid
projectile owner, missing lag history, out-of-range spawn cursor, out-of-order
tick input, and trailing data. Version, sparse absent-slot, length, count, and
enum validation remain part of the strict reader contract.
