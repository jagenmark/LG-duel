# `.lgdemo` format and validation

## Versioning contract

`.lgdemo` is the saved-demo container. `e5b6c3f` makes format version 2 the only
accepted format in `lg::replay`. Version 1 is historical only and the decoder
rejects it. The v2 wire contract is fixed by `ReplayCodec`:

- magic bytes: `LGDM`;
- format version: `2` (`kReplayFormatVersion`);
- fixed tick rate: `125` (`kReplayTickRate`);
- byte order: little endian for every fixed-width value;
- saved-file cap: 512 MiB; chunk cap: 8 MiB; tick cap: 4,194,304; checkpoint cap:
  4,096; and lag-history cap: 256 frames; and
- chunk checksum: CRC-32 of the payload.

Version 2 uses explicit field order, fixed-width values, and a declared byte
order. It never writes C++ struct memory to disk. Padding, host endianness, ABI
layout, pointer size, and enum size must not affect a file.

An old file need not play on a newer build. The current reader does not decode
v1. It fails before restoring any state and says why.

## Preamble and metadata

The 16-byte preamble contains these fields in v2 order:

1. `LGDM` magic;
2. 16-bit format version;
3. 16-bit tick rate;
4. 32-bit metadata byte length; and
5. 32-bit format flags.

The following bounded metadata payload repeats its flags and then stores, in
order: protocol revision, build fingerprint, gameplay configuration hash,
initial server tick, map revision, map name, map content hash, game mode, match
rules, visibility policy, and fixed-slot player metadata. Player metadata holds
slot, occupied marker, bot marker, team, and bounded name.

Strings and metadata lists carry a length and a stated maximum. The current v2
stores a gameplay configuration hash, not a complete configuration payload.
Playback requires the caller to configure an equivalent server and rejects a
mismatched hash.

## Chunks

After metadata, the file contains length-delimited chunks. Each v2 chunk holds a
one-byte type, a 32-bit payload length, a 32-bit CRC-32, and the payload. It has
no v2 chunk flags, compression, expansion length, index, or completion record.
The four chunk types are:

- `TickInputs`, one resolved input frame at a tick;
- `Checkpoint`, including the initial and any periodic bot-free checkpoint;
- `StateHash`, a canonical hash at a tick; and
- `LethalEvent`, lethal metadata when a producer supplies it.

### Sparse tick inputs

A v2 `TickInputs` payload starts with its 32-bit tick and a 16-bit present-slot
mask. It then encodes a `ReplaySlotInput` only for each set bit, in ascending
slot order. A clear bit has no input payload; decoding leaves that slot at its
default state with `present == false`.

The mask may not set bits outside the fixed player-slot range. Absent slots are
default-only: validation rejects non-default command, edge, or timing data for
an absent slot instead of silently treating it as an actor. This keeps an empty
slot from carrying stale input across a disconnect or restore.

V2 has no distinct dynamic roster, name, team, ready, phase, rule, map, or
configuration-change chunk. The core recorder also does not yet supply lethal
events. Those parts of the planned recording contract remain pending.

The writer emits records by type. Tick inputs, checkpoints, hashes, and lethal
events each keep their own valid tick order. A checkpoint’s tick and every input
tick must not precede the initial tick. V2 does not compress records.

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

`lg_duel_replay_codec_tests` covers v2 round trips, truncation with no partial
apply, checksum corruption, wrong magic, non-finite command data, invalid
projectile owner, missing lag history, out-of-range spawn cursor, out-of-order
tick input, and trailing data. Version, sparse absent-slot, length, count, enum,
and other malformed-input cases remain required follow-up coverage.
