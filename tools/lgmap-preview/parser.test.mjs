import assert from "node:assert/strict";
import fs from "node:fs";
import { createRequire } from "node:module";
import path from "node:path";
import { fileURLToPath } from "node:url";

const require = createRequire(import.meta.url);
const parser = require("./parser.js");
const here = path.dirname(fileURLToPath(import.meta.url));
const repoRoot = path.resolve(here, "../..");

const thunderstruck = fs.readFileSync(path.join(repoRoot, "maps", "thunderstruck.lgmap"), "utf8");
const parsed = parser.parseMap(thunderstruck);
assert.equal(parsed.ok, true);
assert.equal(parsed.boxes.length, 19);
assert.equal(parsed.spawns.length, 6);
assert.equal(parsed.bounds.min.x, -15);
assert.equal(parsed.bounds.max.z, 10);

const exported = parser.parseMap(parser.serializeMap(parsed));
assert.equal(exported.ok, true);
assert.equal(exported.boxes.length, parsed.boxes.length);
assert.equal(exported.spawns.length, parsed.spawns.length);

const inverted = parser.parseMap(`version 1
bounds min=-4,-4,0 max=4,4,4
box bad 2,0,0 1,1,1
spawn p1 -2,0,0
spawn p2 2,0,0
`);
assert.equal(inverted.ok, false);
assert.match(inverted.errors.join("\n"), /box 0 has inverted bounds/);

const overlapping = parser.parseMap(`version 1
bounds min=-4,-4,0 max=4,4,4
box first -2,-2,0 1,1,1
box second 0,0,0 2,2,1
spawn p1 -2,0,0
spawn p2 2,0,0
`);
assert.equal(overlapping.ok, false);
assert.match(overlapping.errors.join("\n"), /box 0 overlaps box 1/);

console.log("lgmap preview parser tests passed");
