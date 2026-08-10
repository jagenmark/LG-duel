# `.lgdemo` format and validation

## Versioning contract

`.lgdemo` is the saved-demo container. The first implemented wire shape is
format version 1. The exact magic bytes, header-size constant, checksum
algorithm, and numeric limits are codec constants that remain **pending** until
the replay source task defines them. No reader should accept a file by guessing
those values.

Version 1 uses explicit field order, fixed-width values, and a declared byte
order. It never writes C++ struct memory to disk. Padding, host endianness, ABI
layout, pointer size, and enum size must not affect a file.

An old file need not play on a newer build. A reader may support a known earlier
version only when it has an explicit decoder and compatibility check. Otherwise
it fails before restoring any state and says why.

## Header

The header is bounded and contains these fields in its specified v1 order:

1. replay magic and format version;
2. header byte length, format flags, and declared section limits;
3. fixed server tick rate;
4. gameplay protocol, build, and content-version identifiers;
5. map name, map content hash, and initial map revision;
6. canonical authoritative configuration payload and configuration revision;
7. game mode and match-rule metadata;
8. initial server tick and recording start/end information;
9. player metadata, including stable slot, display name where available, and
   human-or-bot marker;
10. declared record count or index information when used; and
11. header integrity data.

Strings, metadata lists, and configuration payloads carry a length and a stated
maximum. Reserved fields must have their specified value, normally zero. A
reader rejects an unknown required flag instead of assuming a safe meaning.

## Chunks

After the header, the file contains length-delimited chunks. Every chunk carries
its type, flags, sequence or tick range, encoded length, any expansion length,
and integrity data before its payload. Chunk types cover at least:

- an initial bot-free replay checkpoint;
- resolved command spans, including repeated-command runs or deltas when used;
- connection, roster, ready, team, phase, rule, map, and configuration changes;
- periodic checkpoints;
- canonical state hashes;
- lethal markers used to select killcam segments; and
- a final index or completion record when the writer uses one.

The chunk sequence preserves tick order. A checkpoint’s tick and every command
span must be valid within the file range. A writer may compact repeated commands
or use an existing compressor, but only when the reader checks encoded and
expanded bounds before allocation. Compression is optional; it is not a reason
to add a large dependency.

## Strict reader rules

The decoder must validate the whole candidate record before it restores a
checkpoint or gives a partial segment to playback. It must reject:

- wrong magic, unsupported format version, bad required flags, or a header that
  does not fit the file;
- a short, malformed, or trailing payload; bad chunk sequence; or bad checksum;
- declared lengths, counts, strings, expansion sizes, checkpoint counts, or
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

Playback checks the map name, map content hash, initial revision, fixed tick
rate, authoritative configuration, game mode/rules, and required protocol or
build identifiers before it starts. Any mismatch ends playback cleanly with a
specific diagnostic, such as `map content hash mismatch` or `unsupported replay
format version`.

If the verifier finds a state-hash mismatch after a valid load, it reports a
**divergent demo** rather than treating it as file corruption. It stops at the
first divergent tick and reports the first major state group that differs. It
does not continue with an unverified state.

## Required format coverage

The replay tests must cover header, command, checkpoint, and hash round trips;
truncation; magic/version/length/count/enum/index errors; non-finite values;
bad checksums; oversized expansion; bad tick order; trailing data; and unknown
or incompatible versions. These tests are pending with the replay source work.
