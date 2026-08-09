import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";

test("the approved gallery only renders independently passed captures", async () => {
  const source = await readFile(new URL("../app/Gallery.tsx", import.meta.url), "utf8");
  assert.match(source, /captures\.filter\(\(capture\) => capture\.review_status === "pass"\)/);
  assert.match(source, /approvedCaptures\.map\(\(capture\) =>/);
  assert.doesNotMatch(source, /\{captures\.map\(\(capture\) =>/);
  assert.match(source, /fetch\("\/api\/evidence"/);
  assert.match(source, /href=\{capture\.full_size_url\}>Compact review image/);
});
