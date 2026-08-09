import assert from "node:assert/strict";
import { File } from "node:buffer";
import test from "node:test";

const reviewBytes = new Uint8Array([
  82, 73, 70, 70, 12, 0, 0, 0, 87, 69, 66, 80,
  86, 80, 56, 88, 0, 0, 0, 0,
]);
const originalBytes = new Uint8Array([0x89, 0x50, 0x4e, 0x47, 1, 2, 3, 4]);

async function digest(bytes) {
  return Array.from(new Uint8Array(await crypto.subtle.digest("SHA-256", bytes)),
    (value) => value.toString(16).padStart(2, "0")).join("");
}

class FakeR2 {
  objects = new Map();
  claimRace = null;

  async put(key, value, options = {}) {
    const bytes = typeof value === "string"
      ? new TextEncoder().encode(value).buffer
      : value instanceof ReadableStream
        ? await new Response(value).arrayBuffer()
        : value;
    if (options.onlyIf?.etagDoesNotMatch === "*") {
      if (this.claimRace) {
        this.objects.set(key, this.claimRace);
        this.claimRace = null;
      }
      if (this.objects.has(key)) return null;
    }
    this.objects.set(key, {
      bytes,
      contentType: options.httpMetadata?.contentType ?? "application/octet-stream",
      customMetadata: options.customMetadata ?? {},
    });
    return { key };
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
      objects: [...this.objects.keys()].filter((key) => key.startsWith(prefix)).map((key) => ({ key })),
      truncated: false,
    };
  }
}

async function metadata(captureId, review = null, retainOriginal = false) {
  return {
    schema_version: 1,
    task_id: "upload-flow-test",
    capture_id: captureId,
    captured_by: "capture-agent",
    captured_at: "2026-08-09T03:30:00Z",
    title: "Upload flow test",
    description: "Checks the compact review image path.",
    image: `${captureId}.png`,
    contains_sensitive_data: false,
    review,
    retain_original: retainOriginal || review?.verdict === "pass",
    sha256: await digest(originalBytes),
    content_type: "image/png",
    review_sha256: await digest(reviewBytes),
    review_width: 1280,
    review_height: 720,
  };
}

async function upload(worker, env, record, review = reviewBytes, original = null) {
  const form = new FormData();
  form.set("metadata", JSON.stringify(record));
  form.set("review", new File([review], `${record.capture_id}-review.webp`, { type: "image/webp" }));
  if (original) {
    form.set("original", new File([original], `${record.capture_id}.png`, { type: "image/png" }));
  }
  return worker.fetch(new Request("http://localhost/api/evidence", {
    method: "POST",
    body: form,
  }), env, { waitUntil() {}, passThroughOnException() {} });
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

test("R2 upload appears in the live list without a rebuild", async () => {
  const workerUrl = new URL("../dist/server/index.js", import.meta.url);
  workerUrl.searchParams.set("test", `${process.pid}-${Date.now()}`);
  const { default: worker } = await import(workerUrl.href);
  const r2 = new FakeR2();
  const env = environment(r2);
  const captureId = "20260809T033000Z-upload-flow-live-01";
  const record = await metadata(captureId);

  const uploaded = await upload(worker, env, record);
  assert.equal(uploaded.status, 201);
  const uploadBody = await uploaded.json();
  assert.equal(uploadBody.capture.review_status, "not_reviewed");
  assert.equal(uploadBody.capture.original_url, null);
  assert.equal(uploadBody.capture.size_bytes, reviewBytes.byteLength);

  const listed = await worker.fetch(new Request("http://localhost/api/evidence"), env, {
    waitUntil() {}, passThroughOnException() {},
  });
  const listBody = await listed.json();
  assert.equal(listBody.captures.length, 2);
  assert.equal(listBody.captures[0].capture_id, captureId);
  assert.equal(listBody.captures[1].capture_id, "20260808T120000Z-legacy-proof-01");

  const review = await worker.fetch(new Request(
    `http://localhost/api/evidence/${captureId}/review`,
  ), env, { waitUntil() {}, passThroughOnException() {} });
  assert.equal(review.status, 200);
  assert.equal(review.headers.get("content-type"), "image/webp");
  assert.equal((await review.arrayBuffer()).byteLength, reviewBytes.byteLength);

  const repeated = await upload(worker, env, record);
  assert.equal(repeated.status, 200);
  assert.equal((await repeated.json()).status, "already_uploaded");

  const laterPass = await metadata(captureId, {
    reviewer: "review-agent",
    reviewed_at: "2026-08-09T03:31:00Z",
    verdict: "pass",
    notes: "Different metadata must use a new capture id.",
  });
  assert.equal((await upload(worker, env, laterPass, reviewBytes, originalBytes)).status, 409);

  const changedReview = new Uint8Array([...reviewBytes, 1]);
  const changed = { ...record, review_sha256: await digest(changedReview) };
  assert.equal((await upload(worker, env, changed, changedReview)).status, 409);

  const legacyRecord = await metadata("20260808T120000Z-legacy-proof-01");
  assert.equal((await upload(worker, env, legacyRecord)).status, 409);
});

test("a passed capture keeps its optional original", async () => {
  const workerUrl = new URL("../dist/server/index.js", import.meta.url);
  workerUrl.searchParams.set("original", `${process.pid}-${Date.now()}`);
  const { default: worker } = await import(workerUrl.href);
  const r2 = new FakeR2();
  const env = environment(r2);
  const captureId = "20260809T033100Z-upload-flow-proof-02";
  const record = await metadata(captureId, {
    reviewer: "review-agent",
    reviewed_at: "2026-08-09T03:31:30Z",
    verdict: "pass",
    notes: "Independent pass.",
  });
  const response = await upload(worker, env, record, reviewBytes, originalBytes);
  assert.equal(response.status, 201);
  const body = await response.json();
  assert.equal(body.capture.review_status, "pass");
  assert.match(body.capture.original_url, /\/original$/);
  const original = await worker.fetch(new Request(
    `http://localhost${body.capture.original_url}`,
  ), env, { waitUntil() {}, passThroughOnException() {} });
  assert.equal(original.status, 200);
  assert.equal((await original.arrayBuffer()).byteLength, originalBytes.byteLength);
});

test("a same-image metadata race cannot overwrite another capture", async () => {
  const workerUrl = new URL("../dist/server/index.js", import.meta.url);
  workerUrl.searchParams.set("race", `${process.pid}-${Date.now()}`);
  const { default: worker } = await import(workerUrl.href);
  const r2 = new FakeR2();
  const env = environment(r2);
  const captureId = "20260809T033200Z-upload-flow-race-03";
  const record = await metadata(captureId);
  const competing = {
    capture_id: captureId,
    task_id: "other-task",
    title: "Other metadata",
    description: "The same bytes do not make this the same capture record.",
    captured_at: "2026-08-09T03:30:00.000Z",
    captured_by: "other-agent",
    review_status: "pass",
    reviewer: "other-reviewer",
    reviewed_at: "2026-08-09T03:31:00.000Z",
    review_notes: "Competing review.",
    sha256: record.sha256,
    size_bytes: reviewBytes.byteLength,
    preview_url: "",
    full_size_url: "",
    original_url: null,
    review_width: 1280,
    review_height: 720,
    source_content_type: "image/png",
    review_sha256: record.review_sha256,
    review_key: `objects/review/${record.review_sha256}.webp`,
    original_key: null,
  };
  r2.claimRace = {
    bytes: new TextEncoder().encode(JSON.stringify(competing)).buffer,
    contentType: "application/json",
  };
  const response = await upload(worker, env, record);
  assert.equal(response.status, 409);
  const stored = JSON.parse(await new Response((await r2.get(`records/${captureId}.json`)).body).text());
  assert.equal(stored.task_id, "other-task");
  assert.equal(stored.sha256, record.sha256);
});
