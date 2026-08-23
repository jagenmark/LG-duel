import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";

test("the gallery shows every stored capture without a review gate", async () => {
  const source = await readFile(new URL("../app/Gallery.tsx", import.meta.url), "utf8");
  assert.match(source, /captures\.map\(\(capture\) =>/);
  assert.doesNotMatch(source, /review_status|reviewer|review_notes|visibleCaptures/);
  assert.match(source, /fetch\("\/api\/evidence"/);
  assert.match(source, /href=\{capture\.full_size_url\}>Open image/);
});
