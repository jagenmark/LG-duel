# `.lgdemo` format and validation

## Versioning contract

`.lgdemo` is the saved-demo container. `7020ef5` implements format version 1 in
`lg::replay`. Its wire contract is fixed by `ReplayCodec`:

- magic bytes: `LGDM`;
- format version: `1` (`kReplayFormatVersion`);
- fixed tick rate: `125` (`kReplayTickRate`);
- byte order: little endian for every fixed-width value;
- file cap: 64 MiB; chunk cap: 8 MiB; tick cap: 4,194,304; checkpoint cap:
  4,096; and lag-history cap: 256 frames; and
- chunk checksum: CRC-32 of the payload.

Version 1 uses explicit field order, fixed-width values, and a declared byte
order. It never writes C++ struct memory to disk. Padding, host endianness, ABI
layout, pointer size, and enum size must not affect a file.

An old file need not play on a newer build. A reader may support a known earlier
version only when it has an explicit decoder and compatibility check. Otherwise
it fails before restoring any state and says why.

## Preamble and metadata

The 16-byte preamble contains these fields in v1 order:

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

Strings and metadata lists carry a length and a stated maximum. The current v1
stores a gameplay configuration hash, not a complete configuration payload.
Playback requires the caller to configure an equivalent server and rejects a
mismatched hash.

## Chunks

After metadata, the file contains length-delimited chunks. Each v1 chunk holds a
one-byte type, a 32-bit payload length, a 32-bit CRC-32, and the payload. It has
no v1 chunk flags, compression, expansion length, index, or completion record.
The four chunk types are:

- `TickInputs`, one resolved input frame at a tick;
- `Checkpoint`, including the initial and any periodic bot-free checkpoint;
- `StateHash`, a canonical hash at a tick; and
- `LethalEvent`, lethal metadata when a producer supplies it.

V1 has no distinct dynamic roster, name, team, ready, phase, rule, map, or
configuration-change chunk. The core recorder also does not yet supply lethal
events. Those parts of the planned recording contract remain pending.

The writer emits records by type. Tick inputs, checkpoints, hashes, and lethal
events each keep their own valid tick order. A checkpoint’s tick and every input
tick must not precede the initial tick. V1 does not compress records.

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
- command, checkpoint, hash, or event ticks that are out of order;
- map, content, configuration, tick-rate, protocol, or build compatibility
  failures; and
- an incomplete checkpoint, a cross-generation segment, or a required field
  that the version cannot read.

Validation happens before allocation where possible. Bounded allocations and
count checks come before decode loops. The decoder does not repair corrupt data,
skip unknown required records, or apply the valid prefix of a bad checkpoint.

## Compatibility and clean failure

The decoder checks the format version and tick rate. Checkpoint restore checks
map name/content hash/revision, gameplay configuration hash, game mode, player
occupancy, and lag history before it starts playback. Any mismatch ends playback
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

`lg_duel_replay_codec_tests` now covers round trips, truncation with no partial
apply, checksum corruption, wrong magic, non-finite command data, invalid
projectile owner, out-of-order tick input, and trailing data. Version, length,
count, enum, and other malformed-input cases remain required follow-up coverage.
