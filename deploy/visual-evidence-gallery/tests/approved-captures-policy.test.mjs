import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";

test("the gallery shows passed and not-reviewed captures with review state", async () => {
  const source = await readFile(new URL("../app/Gallery.tsx", import.meta.url), "utf8");
  assert.match(source, /capture\.review_status === "pass" \|\| capture\.review_status === "not_reviewed"/);
  assert.match(source, /visibleCaptures\.map\(\(capture\) =>/);
  assert.doesNotMatch(source, /\{captures\.map\(\(capture\) =>/);
  assert.match(source, /"Not claimed as reviewed evidence"/);
  assert.match(source, /fetch\("\/api\/evidence"/);
  assert.match(source, /href=\{capture\.full_size_url\}>Compact review image/);
});
