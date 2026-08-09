import assert from "node:assert/strict";
import { File } from "node:buffer";
import test from "node:test";

const png = (() => {
  const bytes = new Uint8Array(32);
  bytes.set([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a], 0);
  const view = new DataView(bytes.buffer);
  view.setUint32(16, 1920);
  view.setUint32(20, 1080);
  return bytes;
})();

class FakeStatement {
  constructor(db, sql) {
    this.db = db;
    this.sql = sql.replace(/\s+/g, " ").trim();
    this.values = [];
  }

  bind(...values) {
    this.values = values;
    return this;
  }

  async first() {
    if (!this.sql.startsWith("SELECT * FROM evidence_captures WHERE capture_id = ?")) {
      throw new Error(`unexpected first SQL: ${this.sql}`);
    }
    const row = this.db.rows.get(this.values[0]) ?? null;
    if (this.sql.includes("status = 'ready'") && row?.status !== "ready") return null;
    return row ? { ...row } : null;
  }

  async all() {
    if (!this.sql.includes("WHERE status = 'ready'")) {
      throw new Error(`unexpected all SQL: ${this.sql}`);
    }
    return {
      results: [...this.db.rows.values()]
        .filter((row) => row.status === "ready")
        .sort((a, b) => b.captured_at.localeCompare(a.captured_at))
        .map((row) => ({ ...row })),
    };
  }

  async run() {
    if (this.sql.startsWith("CREATE TABLE") || this.sql.startsWith("CREATE INDEX")) {
      return {};
    }
    if (this.sql.startsWith("INSERT INTO evidence_captures")) {
      const [
        capture_id, task_id, title, description, captured_at, captured_by,
        review_status, reviewer, reviewed_at, review_notes, sha256,
        source_size_bytes, source_content_type, review_key, original_key,
        created_at, updated_at,
      ] = this.values;
      if (this.db.insertRace) {
        const raced = this.db.insertRace(capture_id);
        this.db.insertRace = null;
        if (raced) this.db.rows.set(capture_id, raced);
      }
      if (!this.db.rows.has(capture_id)) {
        this.db.rows.set(capture_id, {
          capture_id, task_id, status: "pending", title, description, captured_at,
          captured_by, review_status, reviewer, reviewed_at, review_notes, sha256,
          source_size_bytes, source_content_type, review_key,
          review_content_type: null, review_size_bytes: null,
          review_width: null, review_height: null, original_key,
          created_at, updated_at,
        });
      }
      return {};
    }
    if (this.sql.startsWith("UPDATE evidence_captures SET")) {
      const [
        review_key, review_content_type, review_size_bytes, review_width, review_height,
        reviewed_at_1, reviewed_at_2, review_status,
        reviewed_at_3, reviewed_at_4, reviewer,
        reviewed_at_5, reviewed_at_6, review_notes,
        reviewed_at_7, reviewed_at_8, reviewed_at,
        original_key, updated_at, capture_id, sha256,
      ] = this.values;
      const row = this.db.rows.get(capture_id);
      if (row && row.sha256 === sha256) {
        const advanceReview = reviewed_at !== null &&
          (row.reviewed_at === null || reviewed_at >= row.reviewed_at);
        Object.assign(row, {
          status: "ready", review_key, review_content_type, review_size_bytes,
          review_width, review_height, updated_at,
        });
        if (original_key) row.original_key = original_key;
        if (advanceReview) {
          Object.assign(row, { review_status, reviewer, review_notes, reviewed_at });
        }
        assert.equal(reviewed_at_1, reviewed_at_2);
        assert.equal(reviewed_at_3, reviewed_at_4);
        assert.equal(reviewed_at_5, reviewed_at_6);
        assert.equal(reviewed_at_7, reviewed_at_8);
      }
      return {};
    }
    throw new Error(`unexpected run SQL: ${this.sql}`);
  }
}

class FakeD1 {
  rows = new Map();
  insertRace = null;

  prepare(sql) {
    return new FakeStatement(this, sql);
  }

  async batch(statements) {
    return Promise.all(statements.map((statement) => statement.run()));
  }
}

class FakeR2 {
  objects = new Map();

  async put(key, value, options = {}) {
    const bytes = value instanceof ReadableStream
      ? await new Response(value).arrayBuffer()
      : value;
    this.objects.set(key, {
      bytes,
      contentType: options.httpMetadata?.contentType ?? "application/octet-stream",
      customMetadata: options.customMetadata ?? {},
    });
  }

  async get(key) {
    const value = this.objects.get(key);
    if (!value) return null;
    return {
      body: new Blob([value.bytes]).stream(),
      httpEtag: `"${key}"`,
      customMetadata: value.customMetadata,
      writeHttpMetadata(headers) {
        headers.set("content-type", value.contentType);
      },
    };
  }
}

function metadata(captureId, review = null) {
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
  };
}

async function upload(worker, env, record, bytes = png) {
  const form = new FormData();
  form.set("metadata", JSON.stringify(record));
  form.set("image", new File([bytes], record.image, { type: "image/png" }));
  return worker.fetch(new Request("http://localhost/api/evidence", {
    method: "POST",
    headers: { authorization: "Bearer upload-secret" },
    body: form,
  }), env, { waitUntil() {}, passThroughOnException() {} });
}

test("upload becomes live through the compact review path without a rebuild", async () => {
  const workerUrl = new URL("../dist/server/index.js", import.meta.url);
  workerUrl.searchParams.set("test", `${process.pid}-${Date.now()}`);
  const { default: worker } = await import(workerUrl.href);
  const db = new FakeD1();
  const r2 = new FakeR2();
  let transformCount = 0;
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
      size_bytes: png.byteLength,
      preview_url: "/evidence/assets/legacy/proof.png",
      full_size_url: "/evidence/assets/legacy/proof.png",
    }],
  };
  const env = {
    DB: db,
    EVIDENCE: r2,
    EVIDENCE_UPLOAD_TOKEN: "upload-secret",
    IMAGES: {
      input() {
        return {
          transform(options) {
            assert.deepEqual(options, { fit: "scale-down", width: 1600, height: 1600 });
            return {
              async output(output) {
                assert.deepEqual(output, { format: "image/webp", quality: 82 });
                ++transformCount;
                return { response: () => new Response(new Uint8Array([82, 73, 70, 70]), {
                  headers: { "content-type": "image/webp" },
                }) };
              },
            };
          },
        };
      },
    },
    ASSETS: {
      async fetch(request) {
        const path = new URL(request.url).pathname;
        if (path === "/evidence/manifest.json") return Response.json(legacy);
        if (path === "/evidence/assets/legacy/proof.png") {
          return new Response(png, { headers: { "content-type": "image/png" } });
        }
        return new Response("not found", { status: 404 });
      },
    },
  };

  const unauthorized = await worker.fetch(new Request("http://localhost/api/evidence", {
    method: "POST",
  }), env, { waitUntil() {}, passThroughOnException() {} });
  assert.equal(unauthorized.status, 401);

  const captureId = "20260809T033000Z-upload-flow-live-01";
  const uploaded = await upload(worker, env, metadata(captureId));
  assert.equal(uploaded.status, 201);
  const uploadBody = await uploaded.json();
  assert.equal(uploadBody.capture.full_size_url, `/api/evidence/${captureId}/review`);
  assert.equal(uploadBody.capture.original_url, null);
  assert.equal(uploadBody.capture.review_width, 1600);
  assert.equal(uploadBody.capture.review_height, 900);
  assert.equal(transformCount, 1);

  const listed = await worker.fetch(new Request("http://localhost/api/evidence"), env, {
    waitUntil() {}, passThroughOnException() {},
  });
  const listBody = await listed.json();
  assert.equal(listBody.captures.length, 2);
  assert.equal(listBody.captures[0].capture_id, captureId);
  assert.match(listBody.captures[1].preview_url, /\/api\/evidence\/legacy\/.*\/review$/);

  const review = await worker.fetch(new Request(
    `http://localhost/api/evidence/${captureId}/review`,
  ), env, { waitUntil() {}, passThroughOnException() {} });
  assert.equal(review.status, 200);
  assert.equal(review.headers.get("content-type"), "image/webp");
  assert.equal((await review.arrayBuffer()).byteLength, 4);

  const repeated = await upload(worker, env, metadata(captureId));
  assert.equal(repeated.status, 200);
  assert.equal((await repeated.json()).status, "already_uploaded");
  assert.equal(transformCount, 1);

  const conflict = await upload(worker, env, metadata(captureId), new Uint8Array([...png, 1]));
  assert.equal(conflict.status, 409);

  const legacyReview = await worker.fetch(new Request(
    "http://localhost/api/evidence/legacy/20260808T120000Z-legacy-proof-01/review",
  ), env, { waitUntil() {}, passThroughOnException() {} });
  assert.equal(legacyReview.status, 200);
  assert.equal(legacyReview.headers.get("content-type"), "image/webp");
  assert.equal(transformCount, 2);

  const reviewedId = "20260809T033100Z-upload-flow-proof-02";
  const passed = await upload(worker, env, metadata(reviewedId, {
    reviewer: "review-agent",
    reviewed_at: "2026-08-09T03:31:30Z",
    verdict: "pass",
    notes: "Independent pass.",
  }));
  assert.equal(passed.status, 201);
  const passedBody = await passed.json();
  assert.match(passedBody.capture.original_url, /\/original$/);
  const original = await worker.fetch(new Request(
    `http://localhost${passedBody.capture.original_url}`,
  ), env, { waitUntil() {}, passThroughOnException() {} });
  assert.equal(original.status, 200);
  assert.equal((await original.arrayBuffer()).byteLength, png.byteLength);

  const upgradedId = "20260809T033200Z-upload-flow-upgrade-03";
  assert.equal((await upload(worker, env, metadata(upgradedId))).status, 201);
  const upgraded = await upload(worker, env, metadata(upgradedId, {
    reviewer: "review-agent",
    reviewed_at: "2026-08-09T03:32:30Z",
    verdict: "pass",
    notes: "This later review must not be lost.",
  }));
  assert.equal(upgraded.status, 201);
  const upgradedBody = await upgraded.json();
  assert.equal(upgradedBody.capture.review_status, "pass");
  assert.match(upgradedBody.capture.original_url, /\/original$/);

  const legacyId = legacy.captures[0].capture_id;
  legacy.captures[0].sha256 = Array.from(
    new Uint8Array(await crypto.subtle.digest("SHA-256", png)),
    (value) => value.toString(16).padStart(2, "0"),
  ).join("");
  const migrated = await upload(worker, env, {
    ...metadata(legacyId),
    task_id: "legacy",
    title: "Legacy proof",
    description: "Existing static evidence.",
    captured_at: "2026-08-08T12:00:00Z",
    captured_by: "old-agent",
  });
  assert.equal(migrated.status, 201);
  const migratedBody = await migrated.json();
  assert.equal(migratedBody.capture.review_status, "pass");
  assert.match(migratedBody.capture.original_url, /\/original$/);
  const blockedLegacyReplacement = await upload(
    worker,
    env,
    {
      ...metadata(legacyId),
      task_id: "legacy",
      title: "Legacy proof",
      description: "Existing static evidence.",
      captured_at: "2026-08-08T12:00:00Z",
      captured_by: "old-agent",
    },
    new Uint8Array([...png, 1]),
  );
  assert.equal(blockedLegacyReplacement.status, 409);
});

test("a lost insert race cannot write objects for a competing capture", async () => {
  const workerUrl = new URL("../dist/server/index.js", import.meta.url);
  workerUrl.searchParams.set("race", `${process.pid}-${Date.now()}`);
  const { default: worker } = await import(workerUrl.href);
  const db = new FakeD1();
  const r2 = new FakeR2();
  const captureId = "20260809T034000Z-upload-race-proof-04";
  db.insertRace = (requestedId) => requestedId === captureId ? {
    capture_id: captureId,
    task_id: "other-task",
    status: "pending",
    title: "Other upload",
    description: "This upload won the capture id.",
    captured_at: "2026-08-09T03:40:00.000Z",
    captured_by: "other-agent",
    review_status: "not_reviewed",
    reviewer: null,
    reviewed_at: null,
    review_notes: null,
    sha256: "f".repeat(64),
    source_size_bytes: 1,
    source_content_type: "image/png",
    review_key: `captures/${captureId}/review.webp`,
    review_content_type: null,
    review_size_bytes: null,
    review_width: null,
    review_height: null,
    original_key: null,
  } : null;
  const env = {
    DB: db,
    EVIDENCE: r2,
    EVIDENCE_UPLOAD_TOKEN: "upload-secret",
    IMAGES: {
      input() {
        return {
          transform() {
            return {
              async output() {
                return { response: () => new Response(new Uint8Array([1, 2, 3])) };
              },
            };
          },
        };
      },
    },
    ASSETS: { fetch: async () => Response.json({ schema_version: 1, captures: [] }) },
  };

  const response = await upload(worker, env, metadata(captureId));
  assert.equal(response.status, 409);
  assert.equal(r2.objects.size, 0);
  assert.equal(db.rows.get(captureId).sha256, "f".repeat(64));
});

test("an unreviewed retry completes a passed same-image upload", async () => {
  const workerUrl = new URL("../dist/server/index.js", import.meta.url);
  workerUrl.searchParams.set("original-race", `${process.pid}-${Date.now()}`);
  const { default: worker } = await import(workerUrl.href);
  const db = new FakeD1();
  const r2 = new FakeR2();
  const captureId = "20260809T034100Z-upload-original-race-05";
  const digest = Array.from(
    new Uint8Array(await crypto.subtle.digest("SHA-256", png)),
    (value) => value.toString(16).padStart(2, "0"),
  ).join("");
  db.insertRace = (requestedId) => requestedId === captureId ? {
    capture_id: captureId,
    task_id: "upload-flow-test",
    status: "pending",
    title: "Upload flow test",
    description: "Checks the compact review image path.",
    captured_at: "2026-08-09T03:30:00.000Z",
    captured_by: "capture-agent",
    review_status: "pass",
    reviewer: "review-agent",
    reviewed_at: "2026-08-09T03:41:30.000Z",
    review_notes: "The first request passed review but stalled before R2.",
    sha256: digest,
    source_size_bytes: png.byteLength,
    source_content_type: "image/png",
    review_key: `captures/${captureId}/review.webp`,
    review_content_type: null,
    review_size_bytes: null,
    review_width: null,
    review_height: null,
    original_key: null,
  } : null;
  const env = {
    DB: db,
    EVIDENCE: r2,
    EVIDENCE_UPLOAD_TOKEN: "upload-secret",
    IMAGES: {
      input() {
        return {
          transform() {
            return {
              async output() {
                return { response: () => new Response(new Uint8Array([1, 2, 3])) };
              },
            };
          },
        };
      },
    },
    ASSETS: { fetch: async () => Response.json({ schema_version: 1, captures: [] }) },
  };

  const response = await upload(worker, env, metadata(captureId));
  assert.equal(response.status, 201);
  const body = await response.json();
  assert.equal(body.capture.review_status, "pass");
  assert.match(body.capture.original_url, /\/original$/);
  assert.ok(r2.objects.has(`captures/${captureId}/original.png`));
});
