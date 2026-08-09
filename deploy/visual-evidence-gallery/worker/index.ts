import handler from "vinext/server/app-router-entry";

const captureIdPattern = /^\d{8}T\d{6}Z-[a-z0-9]+(?:-[a-z0-9]+)*-\d{2}$/;
const taskIdPattern = /^[A-Za-z0-9][A-Za-z0-9._-]{1,79}$/;
const sha256Pattern = /^[0-9a-f]{64}$/;
const sourceTypes = new Set(["image/png", "image/jpeg", "image/webp"]);
const reviewVerdicts = new Set(["pass", "fail", "needs_changes"]);
const maxSourceBytes = 15 * 1024 * 1024;
const maxReviewBytes = 2 * 1024 * 1024;

interface StoredObject {
  body: ReadableStream;
  httpEtag?: string;
  writeHttpMetadata?(headers: Headers): void;
}

interface R2BucketBinding {
  get(key: string): Promise<StoredObject | null>;
  list(options?: { prefix?: string; cursor?: string; limit?: number }): Promise<{
    objects: Array<{ key: string }>;
    truncated: boolean;
    cursor?: string;
  }>;
  put(
    key: string,
    value: ArrayBuffer | ReadableStream | string,
    options?: {
      httpMetadata?: { contentType?: string };
      customMetadata?: Record<string, string>;
      onlyIf?: { etagDoesNotMatch?: string };
    },
  ): Promise<unknown | null>;
}

interface Env {
  ASSETS: { fetch(request: Request): Promise<Response> };
  EVIDENCE: R2BucketBinding;
}

interface ExecutionContext {
  waitUntil(promise: Promise<unknown>): void;
  passThroughOnException(): void;
}

type ReviewStatus = "not_reviewed" | "pass" | "fail" | "needs_changes";

interface CaptureRecord {
  capture_id: string;
  task_id: string;
  title: string;
  description: string;
  captured_at: string;
  captured_by: string;
  review_status: ReviewStatus;
  reviewer: string | null;
  reviewed_at: string | null;
  review_notes: string | null;
  sha256: string;
  size_bytes: number;
  preview_url: string;
  full_size_url: string;
  original_url?: string | null;
  review_width?: number | null;
  review_height?: number | null;
}

interface StoredCapture extends CaptureRecord {
  source_content_type: string;
  review_sha256: string;
  review_key: string;
  original_key: string | null;
}

interface CheckedMetadata {
  captureId: string;
  taskId: string;
  title: string;
  description: string;
  capturedAt: string;
  capturedBy: string;
  reviewStatus: ReviewStatus;
  reviewer: string | null;
  reviewedAt: string | null;
  reviewNotes: string | null;
  sourceSha256: string;
  reviewSha256: string;
  sourceContentType: string;
  reviewWidth: number;
  reviewHeight: number;
  retainOriginal: boolean;
}

function json(value: unknown, status = 200): Response {
  return Response.json(value, {
    status,
    headers: { "cache-control": "no-store", "x-content-type-options": "nosniff" },
  });
}

function errorResponse(status: number, message: string): Response {
  return json({ error: message }, status);
}

function requiredText(value: Record<string, unknown>, key: string, where = "metadata"): string {
  const field = value[key];
  if (typeof field !== "string" || !field.trim()) {
    throw new Error(`${where}.${key} must be non-empty text`);
  }
  return field.trim();
}

function utcTimestamp(value: string, field: string): string {
  if (!/^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d+)?Z$/.test(value)) {
    throw new Error(`${field} must use UTC`);
  }
  const time = Date.parse(value);
  if (!Number.isFinite(time)) throw new Error(`${field} must be an ISO 8601 time`);
  return new Date(time).toISOString();
}

function positiveInteger(value: unknown, field: string): number {
  if (!Number.isInteger(value) || Number(value) <= 0) {
    throw new Error(`${field} must be a positive integer`);
  }
  return Number(value);
}

function checkedSha(value: unknown, field: string): string {
  if (typeof value !== "string" || !sha256Pattern.test(value)) {
    throw new Error(`${field} must be a lower-case SHA-256 value`);
  }
  return value;
}

function checkMetadata(raw: unknown): CheckedMetadata {
  if (raw === null || typeof raw !== "object" || Array.isArray(raw)) {
    throw new Error("metadata must be a JSON object");
  }
  const value = raw as Record<string, unknown>;
  if (value.schema_version !== 1) throw new Error("metadata.schema_version must be 1");
  const captureId = requiredText(value, "capture_id");
  const taskId = requiredText(value, "task_id");
  if (!captureIdPattern.test(captureId)) throw new Error("metadata.capture_id has an unsafe format");
  if (!taskIdPattern.test(taskId)) throw new Error("metadata.task_id has an unsafe format");
  if (value.contains_sensitive_data !== false) {
    throw new Error("metadata.contains_sensitive_data must be false");
  }

  const capturedBy = requiredText(value, "captured_by");
  let reviewStatus: ReviewStatus = "not_reviewed";
  let reviewer: string | null = null;
  let reviewedAt: string | null = null;
  let reviewNotes: string | null = null;
  if (value.review !== null && value.review !== undefined) {
    if (typeof value.review !== "object" || Array.isArray(value.review)) {
      throw new Error("metadata.review must be an object when present");
    }
    const review = value.review as Record<string, unknown>;
    reviewer = requiredText(review, "reviewer", "metadata.review");
    reviewStatus = requiredText(review, "verdict", "metadata.review").toLowerCase() as ReviewStatus;
    if (!reviewVerdicts.has(reviewStatus)) throw new Error("metadata.review.verdict is not valid");
    reviewNotes = requiredText(review, "notes", "metadata.review");
    reviewedAt = utcTimestamp(
      requiredText(review, "reviewed_at", "metadata.review"),
      "metadata.review.reviewed_at",
    );
    if (reviewStatus === "pass" && reviewer.toLowerCase() === capturedBy.toLowerCase()) {
      throw new Error("a passing evidence review must be independent");
    }
  }

  const sourceContentType = requiredText(value, "content_type");
  if (!sourceTypes.has(sourceContentType)) throw new Error("metadata.content_type is not supported");
  return {
    captureId,
    taskId,
    title: requiredText(value, "title"),
    description: requiredText(value, "description"),
    capturedAt: utcTimestamp(requiredText(value, "captured_at"), "metadata.captured_at"),
    capturedBy,
    reviewStatus,
    reviewer,
    reviewedAt,
    reviewNotes,
    sourceSha256: checkedSha(value.sha256, "metadata.sha256"),
    reviewSha256: checkedSha(value.review_sha256, "metadata.review_sha256"),
    sourceContentType,
    reviewWidth: positiveInteger(value.review_width, "metadata.review_width"),
    reviewHeight: positiveInteger(value.review_height, "metadata.review_height"),
    retainOriginal: value.retain_original === true || reviewStatus === "pass",
  };
}

async function sha256Hex(bytes: ArrayBuffer): Promise<string> {
  const digest = new Uint8Array(await crypto.subtle.digest("SHA-256", bytes));
  return Array.from(digest, (value) => value.toString(16).padStart(2, "0")).join("");
}

function isWebp(bytes: ArrayBuffer): boolean {
  const value = new Uint8Array(bytes);
  if (value.length < 20) return false;
  const declaredSize = new DataView(value.buffer, value.byteOffset, value.byteLength).getUint32(4, true) + 8;
  const chunk = String.fromCharCode(...value.slice(12, 16));
  return declaredSize <= value.length &&
    String.fromCharCode(...value.slice(0, 4)) === "RIFF" &&
    String.fromCharCode(...value.slice(8, 12)) === "WEBP" &&
    (chunk === "VP8 " || chunk === "VP8L" || chunk === "VP8X");
}

async function readObjectJson<T>(bucket: R2BucketBinding, key: string): Promise<T | null> {
  const object = await bucket.get(key);
  if (!object) return null;
  try {
    return JSON.parse(await new Response(object.body).text()) as T;
  } catch {
    return null;
  }
}

async function legacyManifest(request: Request, env: Env): Promise<CaptureRecord[]> {
  const response = await env.ASSETS.fetch(new Request(new URL("/evidence/manifest.json", request.url)));
  if (!response.ok) return [];
  const value = await response.json() as { captures?: unknown };
  if (!Array.isArray(value.captures)) return [];
  return value.captures.filter((entry): entry is CaptureRecord => (
    entry !== null && typeof entry === "object" &&
    captureIdPattern.test(String((entry as CaptureRecord).capture_id ?? ""))
  ));
}

function publicCapture(record: StoredCapture): CaptureRecord {
  const base = `/api/evidence/${encodeURIComponent(record.capture_id)}`;
  return {
    ...record,
    preview_url: `${base}/review`,
    full_size_url: `${base}/review`,
    original_url: record.original_key ? `${base}/original` : null,
  };
}

async function liveCaptures(env: Env): Promise<StoredCapture[]> {
  const keys: string[] = [];
  let cursor: string | undefined;
  do {
    const page = await env.EVIDENCE.list({ prefix: "records/", cursor, limit: 1000 });
    keys.push(...page.objects.map((object) => object.key).filter((key) => key.endsWith(".json")));
    cursor = page.truncated ? page.cursor : undefined;
  } while (cursor);
  const records = await Promise.all(keys.map((key) => readObjectJson<StoredCapture>(env.EVIDENCE, key)));
  return records.filter((record): record is StoredCapture => (
    record !== null && captureIdPattern.test(record.capture_id) && sha256Pattern.test(record.sha256)
  ));
}

async function listEvidence(request: Request, env: Env): Promise<Response> {
  const [legacy, live] = await Promise.all([legacyManifest(request, env), liveCaptures(env)]);
  const merged = new Map<string, CaptureRecord>();
  for (const record of legacy) merged.set(record.capture_id, record);
  for (const record of live) merged.set(record.capture_id, publicCapture(record));
  const captures = [...merged.values()].sort((left, right) => (
    right.captured_at.localeCompare(left.captured_at) || right.capture_id.localeCompare(left.capture_id)
  ));
  return json({ schema_version: 1, captures });
}

function sourceExtension(contentType: string): string {
  if (contentType === "image/png") return "png";
  if (contentType === "image/webp") return "webp";
  return "jpg";
}

function sameStoredCapture(left: StoredCapture, right: StoredCapture): boolean {
  return left.capture_id === right.capture_id &&
    left.task_id === right.task_id &&
    left.title === right.title &&
    left.description === right.description &&
    Date.parse(left.captured_at) === Date.parse(right.captured_at) &&
    left.captured_by === right.captured_by &&
    left.review_status === right.review_status &&
    left.reviewer === right.reviewer &&
    (left.reviewed_at === null ? right.reviewed_at === null : (
      right.reviewed_at !== null && Date.parse(left.reviewed_at) === Date.parse(right.reviewed_at)
    )) &&
    left.review_notes === right.review_notes &&
    left.sha256 === right.sha256 &&
    left.source_content_type === right.source_content_type &&
    left.review_sha256 === right.review_sha256 &&
    left.review_width === right.review_width &&
    left.review_height === right.review_height &&
    left.review_key === right.review_key &&
    left.original_key === right.original_key;
}

async function uploadEvidence(request: Request, env: Env): Promise<Response> {
  let form: FormData;
  try {
    form = await request.formData();
  } catch {
    return errorResponse(400, "request must be multipart form data");
  }
  const metadataPart = form.get("metadata");
  const reviewPart = form.get("review");
  const originalPart = form.get("original");
  if (typeof metadataPart !== "string" || !(reviewPart instanceof File)) {
    return errorResponse(400, "multipart fields metadata and review are required");
  }

  let metadata: CheckedMetadata;
  try {
    metadata = checkMetadata(JSON.parse(metadataPart));
  } catch (error) {
    return errorResponse(400, error instanceof Error ? error.message : "metadata is invalid");
  }
  if (reviewPart.type !== "image/webp" || reviewPart.size <= 0 || reviewPart.size > maxReviewBytes) {
    return errorResponse(400, `review must be a WebP image of 1..${maxReviewBytes} bytes`);
  }
  if (metadata.retainOriginal !== (originalPart instanceof File)) {
    return errorResponse(400, "original field does not match retain_original");
  }
  if (originalPart instanceof File) {
    if (!sourceTypes.has(originalPart.type) || originalPart.type !== metadata.sourceContentType) {
      return errorResponse(400, "original content type does not match metadata");
    }
    if (originalPart.size <= 0 || originalPart.size > maxSourceBytes) {
      return errorResponse(400, `original must be 1..${maxSourceBytes} bytes`);
    }
  }

  const reviewBytes = await reviewPart.arrayBuffer();
  if (!isWebp(reviewBytes)) {
    return errorResponse(400, "review bytes are not a WebP image");
  }
  if (await sha256Hex(reviewBytes) !== metadata.reviewSha256) {
    return errorResponse(400, "metadata.review_sha256 does not match the review image");
  }
  let originalBytes: ArrayBuffer | null = null;
  if (originalPart instanceof File) {
    originalBytes = await originalPart.arrayBuffer();
    if (await sha256Hex(originalBytes) !== metadata.sourceSha256) {
      return errorResponse(400, "metadata.sha256 does not match the original image");
    }
  }

  if ((await legacyManifest(request, env)).some((entry) => entry.capture_id === metadata.captureId)) {
    return errorResponse(409, "capture id belongs to read-only legacy evidence");
  }
  const metadataKey = `records/${metadata.captureId}.json`;
  const reviewKey = `objects/review/${metadata.reviewSha256}.webp`;
  const originalKey = originalBytes
    ? `objects/original/${metadata.sourceSha256}.${sourceExtension(metadata.sourceContentType)}`
    : null;
  const stored: StoredCapture = {
    capture_id: metadata.captureId,
    task_id: metadata.taskId,
    title: metadata.title,
    description: metadata.description,
    captured_at: metadata.capturedAt,
    captured_by: metadata.capturedBy,
    review_status: metadata.reviewStatus,
    reviewer: metadata.reviewer,
    reviewed_at: metadata.reviewedAt,
    review_notes: metadata.reviewNotes,
    sha256: metadata.sourceSha256,
    size_bytes: reviewBytes.byteLength,
    preview_url: "",
    full_size_url: "",
    original_url: null,
    review_width: metadata.reviewWidth,
    review_height: metadata.reviewHeight,
    source_content_type: metadata.sourceContentType,
    review_sha256: metadata.reviewSha256,
    review_key: reviewKey,
    original_key: originalKey,
  };
  const existing = await readObjectJson<StoredCapture>(env.EVIDENCE, metadataKey);
  if (existing) {
    if (sameStoredCapture(existing, stored)) {
      return json({ status: "already_uploaded", capture: publicCapture(existing) });
    }
    return errorResponse(409, "capture id already exists with different metadata or bytes");
  }

  await env.EVIDENCE.put(reviewKey, reviewBytes, {
    httpMetadata: { contentType: "image/webp" },
    customMetadata: { sha256: metadata.reviewSha256, role: "review" },
  });
  if (originalBytes && originalKey) {
    await env.EVIDENCE.put(originalKey, originalBytes, {
      httpMetadata: { contentType: metadata.sourceContentType },
      customMetadata: { sha256: metadata.sourceSha256, role: "original" },
    });
  }
  const claimed = await env.EVIDENCE.put(metadataKey, JSON.stringify(stored), {
    httpMetadata: { contentType: "application/json" },
    customMetadata: { sha256: metadata.sourceSha256, role: "metadata" },
    onlyIf: { etagDoesNotMatch: "*" },
  });
  if (claimed === null) {
    const winner = await readObjectJson<StoredCapture>(env.EVIDENCE, metadataKey);
    if (winner && sameStoredCapture(winner, stored)) {
      return json({ status: "already_uploaded", capture: publicCapture(winner) });
    }
    return errorResponse(409, "capture id was claimed by another upload");
  }
  return json({ status: "uploaded", capture: publicCapture(stored) }, 201);
}

async function serveObject(bucket: R2BucketBinding, key: string, fallbackType: string, etag: string) {
  const object = await bucket.get(key);
  if (!object) return errorResponse(404, "image not found");
  const headers = new Headers({
    "cache-control": "private, max-age=31536000, immutable",
    "content-type": fallbackType,
    "x-content-type-options": "nosniff",
    etag: object.httpEtag ?? `"${etag}"`,
  });
  object.writeHttpMetadata?.(headers);
  return new Response(object.body, { headers });
}

async function serveStoredImage(
  captureId: string,
  kind: "review" | "original",
  env: Env,
): Promise<Response> {
  const record = await readObjectJson<StoredCapture>(env.EVIDENCE, `records/${captureId}.json`);
  if (!record) return errorResponse(404, "capture not found");
  const key = kind === "review" ? record.review_key : record.original_key;
  if (!key) return errorResponse(404, "original was not retained");
  return serveObject(
    env.EVIDENCE,
    key,
    kind === "review" ? "image/webp" : record.source_content_type,
    kind === "review" ? record.review_sha256 : record.sha256,
  );
}

async function evidenceRequest(request: Request, env: Env): Promise<Response | null> {
  const url = new URL(request.url);
  if (url.pathname === "/api/evidence") {
    if (request.method === "GET") return listEvidence(request, env);
    if (request.method === "POST") return uploadEvidence(request, env);
    return errorResponse(405, "method not allowed");
  }
  const match = url.pathname.match(/^\/api\/evidence\/([^/]+)\/(review|original)$/);
  if (match && request.method === "GET") {
    const captureId = decodeURIComponent(match[1]);
    if (!captureIdPattern.test(captureId)) return errorResponse(404, "capture not found");
    return serveStoredImage(captureId, match[2] as "review" | "original", env);
  }
  return null;
}

const worker = {
  async fetch(request: Request, env: Env, ctx: ExecutionContext): Promise<Response> {
    const evidenceResponse = await evidenceRequest(request, env);
    if (evidenceResponse) return evidenceResponse;
    return handler.fetch(request, env, ctx);
  },
};

export default worker;
