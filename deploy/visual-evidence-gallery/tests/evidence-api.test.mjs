import assert from "node:assert/strict";
import test from "node:test";

const imageBytes = new Uint8Array([82, 73, 70, 70, 1, 2, 3, 4]);
const originalBytes = new Uint8Array([0x89, 0x50, 0x4e, 0x47, 1, 2, 3, 4]);

class FakeR2 {
  objects = new Map();

  set(key, value, contentType) {
    const bytes = typeof value === "string" ? new TextEncoder().encode(value) : value;
    this.objects.set(key, { bytes, contentType });
  }

  async get(key) {
    const value = this.objects.get(key);
    if (!value) return null;
    return {
      body: new Blob([value.bytes]).stream(),
      httpEtag: `"${key}"`,
      writeHttpMetadata(headers) {
        headers.set("content-type", value.contentType);
      },
    };
  }

  async list({ prefix = "", cursor } = {}) {
    assert.equal(cursor, undefined);
    return {
      objects: [...this.objects.keys()]
        .filter((key) => key.startsWith(prefix))
        .map((key) => ({ key })),
      truncated: false,
    };
  }
}

function environment(r2) {
  const legacy = {
    schema_version: 1,
    captures: [{
      capture_id: "20260808T120000Z-legacy-proof-01",
      task_id: "legacy",
      title: "Legacy proof",
      description: "Existing static evidence.",
      captured_at: "2026-08-08T12:00:00Z",
      captured_by: "old-agent",
      review_status: "pass",
      reviewer: "review-agent",
      reviewed_at: "2026-08-08T12:05:00Z",
      review_notes: "Pass",
      sha256: "0".repeat(64),
      size_bytes: 8,
      preview_url: "/evidence/assets/legacy/proof.png",
      full_size_url: "/evidence/assets/legacy/proof.png",
    }],
  };
  return {
    EVIDENCE: r2,
    ASSETS: {
      async fetch(request) {
        return new URL(request.url).pathname === "/evidence/manifest.json"
          ? Response.json(legacy)
          : new Response("not found", { status: 404 });
      },
    },
  };
}

async function workerFor(name) {
  const workerUrl = new URL("../dist/server/index.js", import.meta.url);
  workerUrl.searchParams.set("test", `${name}-${process.pid}-${Date.now()}`);
  return (await import(workerUrl.href)).default;
}

function storedCapture() {
  return {
    capture_id: "20260809T033000Z-live-proof-01",
    task_id: "live",
    title: "Live proof",
    description: "Existing stored evidence.",
    captured_at: "2026-08-09T03:30:00Z",
    sha256: "0".repeat(64),
    source_content_type: "image/png",
    review_sha256: "1".repeat(64),
    review_key: "objects/review/live.webp",
    original_key: "objects/original/live.png",
  };
}

test("stored captures remain readable without exposing review policy", async () => {
  const worker = await workerFor("read-only-list");
  const r2 = new FakeR2();
  const record = storedCapture();
  r2.set(`records/${record.capture_id}.json`, JSON.stringify(record), "application/json");
  r2.set(record.review_key, imageBytes, "image/webp");
  r2.set(record.original_key, originalBytes, "image/png");
  const env = environment(r2);

  const listed = await worker.fetch(new Request("http://localhost/api/evidence"), env, {
    waitUntil() {}, passThroughOnException() {},
  });
  assert.equal(listed.status, 200);
  const body = await listed.json();
  assert.equal(body.captures.length, 2);
  assert.equal(body.captures[0].capture_id, record.capture_id);
  assert.equal(body.captures[0].preview_url, `/api/evidence/${record.capture_id}/image`);
  assert.equal("review_status" in body.captures[0], false);
  assert.equal("review_status" in body.captures[1], false);

  const image = await worker.fetch(new Request(
    `http://localhost/api/evidence/${record.capture_id}/image`,
  ), env, { waitUntil() {}, passThroughOnException() {} });
  assert.equal(image.status, 200);
  assert.equal(image.headers.get("content-type"), "image/webp");
  assert.deepEqual(new Uint8Array(await image.arrayBuffer()), imageBytes);

  const original = await worker.fetch(new Request(
    `http://localhost/api/evidence/${record.capture_id}/original`,
  ), env, { waitUntil() {}, passThroughOnException() {} });
  assert.equal(original.status, 200);
  assert.equal(original.headers.get("content-type"), "image/png");
  assert.deepEqual(new Uint8Array(await original.arrayBuffer()), originalBytes);
});

test("the gallery API is read-only", async () => {
  const worker = await workerFor("read-only-write");
  const r2 = new FakeR2();
  const env = environment(r2);
  const form = new FormData();
  form.set("metadata", JSON.stringify(storedCapture()));

  const response = await worker.fetch(new Request("http://localhost/api/evidence", {
    method: "POST",
    body: form,
  }), env, { waitUntil() {}, passThroughOnException() {} });

  assert.equal(response.status, 405);
  assert.equal(r2.objects.size, 0);
});
