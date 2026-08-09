import handler from "vinext/server/app-router-entry";
import { evidenceReadyIndexSql, evidenceTableSql } from "../db/schema";

const maxSourceBytes = 15 * 1024 * 1024;
const maxReviewEdge = 1600;
const reviewQuality = 82;
const captureIdPattern = /^\d{8}T\d{6}Z-[a-z0-9]+(?:-[a-z0-9]+)*-\d{2}$/;
const taskIdPattern = /^[A-Za-z0-9][A-Za-z0-9._-]{1,79}$/;
const sha256Pattern = /^[0-9a-f]{64}$/;
const allowedContentTypes = new Set(["image/png", "image/jpeg", "image/webp"]);
const reviewVerdicts = new Set(["pass", "fail", "needs_changes"]);

interface D1Statement {
  bind(...values: unknown[]): D1Statement;
  first<T = Record<string, unknown>>(): Promise<T | null>;
  all<T = Record<string, unknown>>(): Promise<{ results: T[] }>;
  run(): Promise<unknown>;
}

interface D1DatabaseBinding {
  prepare(sql: string): D1Statement;
  batch(statements: D1Statement[]): Promise<unknown[]>;
}

interface StoredObject {
  body: ReadableStream;
  httpEtag?: string;
  customMetadata?: Record<string, string>;
  writeHttpMetadata?(headers: Headers): void;
}

interface R2BucketBinding {
  get(key: string): Promise<StoredObject | null>;
  put(
    key: string,
    value: ArrayBuffer | ReadableStream,
    options?: {
      httpMetadata?: { contentType?: string };
      customMetadata?: Record<string, string>;
    },
  ): Promise<unknown>;
}

interface ImagesBinding {
  input(stream: ReadableStream): {
    transform(options: Record<string, unknown>): {
      output(options: { format: string; quality: number }): Promise<{
        response(): Response;
      }>;
    };
  };
}

interface Env {
  ASSETS: { fetch(request: Request): Promise<Response> };
  DB: D1DatabaseBinding;
  EVIDENCE: R2BucketBinding;
  IMAGES?: ImagesBinding;
  EVIDENCE_UPLOAD_TOKEN?: string;
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

interface EvidenceRow {
  capture_id: string;
  task_id: string;
  status: "pending" | "ready";
  title: string;
  description: string;
  captured_at: string;
  captured_by: string;
  review_status: ReviewStatus;
  reviewer: string | null;
  reviewed_at: string | null;
  review_notes: string | null;
  sha256: string;
  source_size_bytes: number;
  source_content_type: string;
  review_key: string;
  review_content_type: string | null;
  review_size_bytes: number | null;
  review_width: number | null;
  review_height: number | null;
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
  suppliedSha256: string | null;
  retainOriginal: boolean;
}

interface ImageSize {
  width: number;
  height: number;
}

interface ReviewImage {
  bytes: ArrayBuffer;
  contentType: string;
  width: number;
  height: number;
}

function json(value: unknown, status = 200): Response {
  return Response.json(value, {
    status,
    headers: {
      "cache-control": "no-store",
      "x-content-type-options": "nosniff",
    },
  });
}

function errorResponse(status: number, message: string): Response {
  return json({ error: message }, status);
}

function requiredText(
  value: Record<string, unknown>,
  key: string,
  where = "metadata",
): string {
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
  if (!Number.isFinite(time)) {
    throw new Error(`${field} must be an ISO 8601 time`);
  }
  return new Date(time).toISOString();
}

function checkMetadata(raw: unknown): CheckedMetadata {
  if (raw === null || typeof raw !== "object" || Array.isArray(raw)) {
    throw new Error("metadata must be a JSON object");
  }
  const value = raw as Record<string, unknown>;
  if (value.schema_version !== 1) {
    throw new Error("metadata.schema_version must be 1");
  }
  const captureId = requiredText(value, "capture_id");
  const taskId = requiredText(value, "task_id");
  if (!captureIdPattern.test(captureId)) {
    throw new Error("metadata.capture_id has an unsafe format");
  }
  if (!taskIdPattern.test(taskId)) {
    throw new Error("metadata.task_id has an unsafe format");
  }
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
    if (!reviewVerdicts.has(reviewStatus)) {
      throw new Error("metadata.review.verdict is not valid");
    }
    reviewNotes = requiredText(review, "notes", "metadata.review");
    reviewedAt = utcTimestamp(
      requiredText(review, "reviewed_at", "metadata.review"),
      "metadata.review.reviewed_at",
    );
    if (reviewStatus === "pass" && reviewer.toLowerCase() === capturedBy.toLowerCase()) {
      throw new Error("a passing evidence review must be independent");
    }
  }

  const suppliedSha256 = value.sha256 === undefined
    ? null
    : typeof value.sha256 === "string" && sha256Pattern.test(value.sha256)
      ? value.sha256
      : (() => { throw new Error("metadata.sha256 must be a lower-case SHA-256 value"); })();

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
    suppliedSha256,
    retainOriginal: value.retain_original === true || reviewStatus === "pass",
  };
}

function readUint24Little(bytes: Uint8Array, offset: number): number {
  return bytes[offset] | (bytes[offset + 1] << 8) | (bytes[offset + 2] << 16);
}

function imageSize(bytes: Uint8Array, contentType: string): ImageSize | null {
  if (
    contentType === "image/png" && bytes.length >= 24 &&
    bytes[0] === 0x89 && bytes[1] === 0x50 && bytes[2] === 0x4e && bytes[3] === 0x47
  ) {
    const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    return { width: view.getUint32(16), height: view.getUint32(20) };
  }
  if (contentType === "image/jpeg" && bytes.length >= 4) {
    let offset = 2;
    while (offset + 8 < bytes.length) {
      if (bytes[offset] !== 0xff) {
        ++offset;
        continue;
      }
      const marker = bytes[offset + 1];
      if (marker === 0xd8 || marker === 0xd9) {
        offset += 2;
        continue;
      }
      const length = (bytes[offset + 2] << 8) | bytes[offset + 3];
      if (length < 2 || offset + 2 + length > bytes.length) break;
      if (
        (marker >= 0xc0 && marker <= 0xc3) ||
        (marker >= 0xc5 && marker <= 0xc7) ||
        (marker >= 0xc9 && marker <= 0xcb) ||
        (marker >= 0xcd && marker <= 0xcf)
      ) {
        return {
          height: (bytes[offset + 5] << 8) | bytes[offset + 6],
          width: (bytes[offset + 7] << 8) | bytes[offset + 8],
        };
      }
      offset += 2 + length;
    }
  }
  if (
    contentType === "image/webp" && bytes.length >= 30 &&
    String.fromCharCode(...bytes.slice(0, 4)) === "RIFF" &&
    String.fromCharCode(...bytes.slice(8, 12)) === "WEBP"
  ) {
    const chunk = String.fromCharCode(...bytes.slice(12, 16));
    if (chunk === "VP8X") {
      return {
        width: 1 + readUint24Little(bytes, 24),
        height: 1 + readUint24Little(bytes, 27),
      };
    }
  }
  return null;
}

function cappedSize(size: ImageSize): ImageSize {
  const scale = Math.min(1, maxReviewEdge / Math.max(size.width, size.height));
  return {
    width: Math.max(1, Math.round(size.width * scale)),
    height: Math.max(1, Math.round(size.height * scale)),
  };
}

async function makeReviewImage(
  source: ArrayBuffer,
  contentType: string,
  images: ImagesBinding | undefined,
): Promise<ReviewImage> {
  const dimensions = imageSize(new Uint8Array(source), contentType);
  if (images) {
    const output = await images
      .input(new Blob([source], { type: contentType }).stream())
      .transform({ fit: "scale-down", width: maxReviewEdge, height: maxReviewEdge })
      .output({ format: "image/webp", quality: reviewQuality });
    const response = output.response();
    if (!response.ok) {
      throw new Error(`image optimisation failed with status ${response.status}`);
    }
    const size = dimensions ? cappedSize(dimensions) : { width: 0, height: 0 };
    return {
      bytes: await response.arrayBuffer(),
      contentType: "image/webp",
      width: size.width,
      height: size.height,
    };
  }
  if (
    dimensions && dimensions.width <= maxReviewEdge && dimensions.height <= maxReviewEdge &&
    (contentType === "image/jpeg" || contentType === "image/webp") &&
    source.byteLength <= 2 * 1024 * 1024
  ) {
    return { bytes: source, contentType, width: dimensions.width, height: dimensions.height };
  }
  throw new Error("image optimisation is temporarily unavailable");
}

async function sha256Hex(bytes: ArrayBuffer): Promise<string> {
  const digest = new Uint8Array(await crypto.subtle.digest("SHA-256", bytes));
  return Array.from(digest, (value) => value.toString(16).padStart(2, "0")).join("");
}

async function tokenMatches(request: Request, expected: string | undefined): Promise<boolean> {
  if (!expected) return false;
  const header = request.headers.get("authorization") ?? "";
  const supplied = header.startsWith("Bearer ") ? header.slice(7) : "";
  if (!supplied) return false;
  const [left, right] = await Promise.all([
    crypto.subtle.digest("SHA-256", new TextEncoder().encode(supplied)),
    crypto.subtle.digest("SHA-256", new TextEncoder().encode(expected)),
  ]);
  const a = new Uint8Array(left);
  const b = new Uint8Array(right);
  let difference = a.length ^ b.length;
  for (let index = 0; index < Math.max(a.length, b.length); ++index) {
    difference |= (a[index] ?? 0) ^ (b[index] ?? 0);
  }
  return difference === 0;
}

async function ensureSchema(db: D1DatabaseBinding): Promise<void> {
  await db.batch([
    db.prepare(evidenceTableSql),
    db.prepare(evidenceReadyIndexSql),
  ]);
}

function rowToCapture(row: EvidenceRow): CaptureRecord {
  const base = `/api/evidence/${encodeURIComponent(row.capture_id)}`;
  return {
    capture_id: row.capture_id,
    task_id: row.task_id,
    title: row.title,
    description: row.description,
    captured_at: row.captured_at,
    captured_by: row.captured_by,
    review_status: row.review_status,
    reviewer: row.reviewer,
    reviewed_at: row.reviewed_at,
    review_notes: row.review_notes,
    sha256: row.sha256,
    size_bytes: row.review_size_bytes ?? row.source_size_bytes,
    preview_url: `${base}/review`,
    full_size_url: `${base}/review`,
    original_url: row.original_key ? `${base}/original` : null,
    review_width: row.review_width,
    review_height: row.review_height,
  };
}

async function legacyManifest(request: Request, env: Env): Promise<CaptureRecord[]> {
  const url = new URL("/evidence/manifest.json", request.url);
  const response = await env.ASSETS.fetch(new Request(url));
  if (!response.ok) return [];
  const value = await response.json() as { captures?: unknown };
  if (!Array.isArray(value.captures)) return [];
  return value.captures.filter((entry): entry is CaptureRecord => (
    entry !== null && typeof entry === "object" &&
    captureIdPattern.test(String((entry as CaptureRecord).capture_id ?? ""))
  ));
}

function legacyReviewCapture(entry: CaptureRecord): CaptureRecord {
  const original = entry.original_url ?? entry.full_size_url;
  return {
    ...entry,
    preview_url: `/api/evidence/legacy/${encodeURIComponent(entry.capture_id)}/review`,
    full_size_url: `/api/evidence/legacy/${encodeURIComponent(entry.capture_id)}/review`,
    original_url: original,
  };
}

function sameCaptureIdentity(
  row: Pick<EvidenceRow, "task_id" | "title" | "description" | "captured_at" | "captured_by" | "source_content_type">,
  metadata: CheckedMetadata,
  contentType: string,
): boolean {
  return row.task_id === metadata.taskId &&
    row.title === metadata.title &&
    row.description === metadata.description &&
    Date.parse(row.captured_at) === Date.parse(metadata.capturedAt) &&
    row.captured_by === metadata.capturedBy &&
    row.source_content_type === contentType;
}

function preserveLegacyReview(metadata: CheckedMetadata, legacy: CaptureRecord): CheckedMetadata {
  if (
    legacy.review_status === "not_reviewed" || !legacy.reviewed_at ||
    (metadata.reviewedAt && Date.parse(metadata.reviewedAt) >= Date.parse(legacy.reviewed_at))
  ) {
    return metadata;
  }
  return {
    ...metadata,
    reviewStatus: legacy.review_status,
    reviewer: legacy.reviewer,
    reviewedAt: new Date(legacy.reviewed_at).toISOString(),
    reviewNotes: legacy.review_notes,
    retainOriginal: metadata.retainOriginal || legacy.review_status === "pass",
  };
}

function reviewAdvances(row: EvidenceRow, metadata: CheckedMetadata): boolean {
  return metadata.reviewedAt !== null &&
    (row.reviewed_at === null || Date.parse(metadata.reviewedAt) > Date.parse(row.reviewed_at));
}

function originalNeedsRepair(row: EvidenceRow, metadata: CheckedMetadata): boolean {
  return (metadata.retainOriginal || row.review_status === "pass") && row.original_key === null;
}

async function listEvidence(request: Request, env: Env): Promise<Response> {
  await ensureSchema(env.DB);
  const [legacy, live] = await Promise.all([
    legacyManifest(request, env),
    env.DB.prepare(
      "SELECT * FROM evidence_captures WHERE status = 'ready' ORDER BY captured_at DESC, capture_id DESC",
    ).all<EvidenceRow>(),
  ]);
  const merged = new Map<string, CaptureRecord>();
  for (const entry of legacy) merged.set(entry.capture_id, legacyReviewCapture(entry));
  for (const row of live.results) merged.set(row.capture_id, rowToCapture(row));
  const captures = Array.from(merged.values()).sort((left, right) => (
    right.captured_at.localeCompare(left.captured_at) ||
    right.capture_id.localeCompare(left.capture_id)
  ));
  return json({ schema_version: 1, captures });
}

async function insertPending(
  env: Env,
  metadata: CheckedMetadata,
  digest: string,
  sourceSize: number,
  sourceContentType: string,
  reviewKey: string,
): Promise<void> {
  const now = new Date().toISOString();
  await env.DB.prepare(`
    INSERT INTO evidence_captures (
      capture_id, task_id, status, title, description, captured_at, captured_by,
      review_status, reviewer, reviewed_at, review_notes, sha256,
      source_size_bytes, source_content_type, review_key, original_key,
      created_at, updated_at
    ) VALUES (?, ?, 'pending', ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    ON CONFLICT(capture_id) DO NOTHING
  `).bind(
    metadata.captureId,
    metadata.taskId,
    metadata.title,
    metadata.description,
    metadata.capturedAt,
    metadata.capturedBy,
    metadata.reviewStatus,
    metadata.reviewer,
    metadata.reviewedAt,
    metadata.reviewNotes,
    digest,
    sourceSize,
    sourceContentType,
    reviewKey,
    null,
    now,
    now,
  ).run();
}

async function uploadEvidence(request: Request, env: Env): Promise<Response> {
  if (!(await tokenMatches(request, env.EVIDENCE_UPLOAD_TOKEN))) {
    return errorResponse(401, "upload authorization failed");
  }
  await ensureSchema(env.DB);
  let form: FormData;
  try {
    form = await request.formData();
  } catch {
    return errorResponse(400, "request must be multipart form data");
  }
  const metadataPart = form.get("metadata");
  const imagePart = form.get("image");
  if (typeof metadataPart !== "string" || !(imagePart instanceof File)) {
    return errorResponse(400, "multipart fields metadata and image are required");
  }

  let metadata: CheckedMetadata;
  try {
    metadata = checkMetadata(JSON.parse(metadataPart));
  } catch (error) {
    return errorResponse(400, error instanceof Error ? error.message : "metadata is invalid");
  }
  if (!allowedContentTypes.has(imagePart.type)) {
    return errorResponse(400, "image must be PNG, JPEG, or WebP");
  }
  if (imagePart.size <= 0 || imagePart.size > maxSourceBytes) {
    return errorResponse(400, `image must be 1..${maxSourceBytes} bytes`);
  }

  const source = await imagePart.arrayBuffer();
  const digest = await sha256Hex(source);
  if (metadata.suppliedSha256 && metadata.suppliedSha256 !== digest) {
    return errorResponse(400, "metadata.sha256 does not match the image");
  }
  const legacy = (await legacyManifest(request, env)).find(
    (entry) => entry.capture_id === metadata.captureId,
  );
  if (legacy) {
    if (legacy.sha256 !== digest) {
      return errorResponse(409, "capture id belongs to existing gallery evidence");
    }
    const legacyIdentity = {
      task_id: legacy.task_id,
      title: legacy.title,
      description: legacy.description,
      captured_at: legacy.captured_at,
      captured_by: legacy.captured_by,
      source_content_type: imagePart.type,
    };
    if (!sameCaptureIdentity(legacyIdentity, metadata, imagePart.type)) {
      return errorResponse(409, "capture metadata differs from existing gallery evidence");
    }
    metadata = preserveLegacyReview(metadata, legacy);
  }

  const existing = await env.DB.prepare(
    "SELECT * FROM evidence_captures WHERE capture_id = ?",
  ).bind(metadata.captureId).first<EvidenceRow>();
  if (existing && existing.sha256 !== digest) {
    return errorResponse(409, "capture id already exists with different image bytes");
  }
  if (existing && !sameCaptureIdentity(existing, metadata, imagePart.type)) {
    return errorResponse(409, "capture metadata differs from the stored upload");
  }
  if (
    existing?.status === "ready" && !reviewAdvances(existing, metadata) &&
    !originalNeedsRepair(existing, metadata)
  ) {
    return json({ status: "already_uploaded", capture: rowToCapture(existing) });
  }

  let review: ReviewImage;
  try {
    review = await makeReviewImage(source, imagePart.type, env.IMAGES);
  } catch (error) {
    return errorResponse(503, error instanceof Error ? error.message : "image optimisation failed");
  }
  const extension = imagePart.type === "image/png"
    ? "png"
    : imagePart.type === "image/webp" ? "webp" : "jpg";
  const reviewExtension = review.contentType === "image/webp" ? "webp" : "jpg";
  const prefix = `captures/${metadata.captureId}`;
  const reviewKey = `${prefix}/review.${reviewExtension}`;
  await insertPending(
    env,
    metadata,
    digest,
    source.byteLength,
    imagePart.type,
    reviewKey,
  );

  // The insert can lose a race to another upload with the same capture id.
  // Re-read before writing shared object keys so different bytes cannot replace it.
  const claimed = await env.DB.prepare(
    "SELECT * FROM evidence_captures WHERE capture_id = ?",
  ).bind(metadata.captureId).first<EvidenceRow>();
  if (!claimed || claimed.sha256 !== digest) {
    return errorResponse(409, "capture id already exists with different image bytes");
  }
  if (!sameCaptureIdentity(claimed, metadata, imagePart.type)) {
    return errorResponse(409, "capture metadata differs from the stored upload");
  }
  if (
    claimed.status === "ready" && !reviewAdvances(claimed, metadata) &&
    !originalNeedsRepair(claimed, metadata)
  ) {
    return json({ status: "already_uploaded", capture: rowToCapture(claimed) });
  }
  const originalKey = metadata.retainOriginal || claimed.review_status === "pass"
    ? `${prefix}/original.${extension}`
    : null;

  await env.EVIDENCE.put(reviewKey, review.bytes, {
    httpMetadata: { contentType: review.contentType },
    customMetadata: { sha256: digest, role: "review" },
  });
  if (originalKey) {
    await env.EVIDENCE.put(originalKey, source, {
      httpMetadata: { contentType: imagePart.type },
      customMetadata: { sha256: digest, role: "original" },
    });
  }
  await env.DB.prepare(`
    UPDATE evidence_captures SET
      status = 'ready', review_key = ?, review_content_type = ?, review_size_bytes = ?,
      review_width = ?, review_height = ?,
      review_status = CASE WHEN ? IS NOT NULL AND (reviewed_at IS NULL OR ? >= reviewed_at)
        THEN ? ELSE review_status END,
      reviewer = CASE WHEN ? IS NOT NULL AND (reviewed_at IS NULL OR ? >= reviewed_at)
        THEN ? ELSE reviewer END,
      review_notes = CASE WHEN ? IS NOT NULL AND (reviewed_at IS NULL OR ? >= reviewed_at)
        THEN ? ELSE review_notes END,
      reviewed_at = CASE WHEN ? IS NOT NULL AND (reviewed_at IS NULL OR ? >= reviewed_at)
        THEN ? ELSE reviewed_at END,
      original_key = COALESCE(original_key, ?), updated_at = ?
    WHERE capture_id = ? AND sha256 = ?
  `).bind(
    reviewKey,
    review.contentType,
    review.bytes.byteLength,
    review.width,
    review.height,
    metadata.reviewedAt,
    metadata.reviewedAt,
    metadata.reviewStatus,
    metadata.reviewedAt,
    metadata.reviewedAt,
    metadata.reviewer,
    metadata.reviewedAt,
    metadata.reviewedAt,
    metadata.reviewNotes,
    metadata.reviewedAt,
    metadata.reviewedAt,
    metadata.reviewedAt,
    originalKey,
    new Date().toISOString(),
    metadata.captureId,
    digest,
  ).run();
  const ready = await env.DB.prepare(
    "SELECT * FROM evidence_captures WHERE capture_id = ?",
  ).bind(metadata.captureId).first<EvidenceRow>();
  if (!ready || ready.status !== "ready") {
    return errorResponse(500, "upload did not reach ready state");
  }
  return json({ status: "uploaded", capture: rowToCapture(ready) }, 201);
}

async function serveObject(
  bucket: R2BucketBinding,
  key: string,
  fallbackType: string,
  fallbackEtag: string,
): Promise<Response> {
  const object = await bucket.get(key);
  if (!object) return errorResponse(404, "image not found");
  const headers = new Headers({
    "cache-control": "private, max-age=31536000, immutable",
    "content-type": fallbackType,
    "x-content-type-options": "nosniff",
  });
  object.writeHttpMetadata?.(headers);
  headers.set("etag", object.httpEtag ?? `"${fallbackEtag}"`);
  return new Response(object.body, { headers });
}

async function serveStoredImage(
  captureId: string,
  kind: "review" | "original",
  env: Env,
): Promise<Response> {
  await ensureSchema(env.DB);
  const row = await env.DB.prepare(
    "SELECT * FROM evidence_captures WHERE capture_id = ? AND status = 'ready'",
  ).bind(captureId).first<EvidenceRow>();
  if (!row) return errorResponse(404, "capture not found");
  const key = kind === "review" ? row.review_key : row.original_key;
  if (!key) return errorResponse(404, "original was not retained");
  return serveObject(
    env.EVIDENCE,
    key,
    kind === "review" ? row.review_content_type ?? "image/webp" : row.source_content_type,
    row.sha256,
  );
}

async function serveLegacyReview(
  request: Request,
  captureId: string,
  env: Env,
): Promise<Response> {
  const key = `legacy/${captureId}/review.webp`;
  const cached = await env.EVIDENCE.get(key);
  if (cached) {
    const headers = new Headers({
      "cache-control": "private, max-age=31536000, immutable",
      "content-type": "image/webp",
      "x-content-type-options": "nosniff",
    });
    cached.writeHttpMetadata?.(headers);
    if (cached.httpEtag) headers.set("etag", cached.httpEtag);
    return new Response(cached.body, { headers });
  }
  const entry = (await legacyManifest(request, env)).find(
    (item) => item.capture_id === captureId,
  );
  if (!entry || !entry.preview_url.startsWith("/evidence/assets/")) {
    return errorResponse(404, "legacy capture not found");
  }
  const sourceResponse = await env.ASSETS.fetch(
    new Request(new URL(entry.preview_url, request.url)),
  );
  if (!sourceResponse.ok) return errorResponse(404, "legacy image not found");
  const contentType = (sourceResponse.headers.get("content-type") ?? "").split(";")[0];
  if (!allowedContentTypes.has(contentType)) {
    return errorResponse(415, "legacy image type is not supported");
  }
  const source = await sourceResponse.arrayBuffer();
  let review: ReviewImage;
  try {
    review = await makeReviewImage(source, contentType, env.IMAGES);
  } catch (error) {
    return errorResponse(503, error instanceof Error ? error.message : "image optimisation failed");
  }
  await env.EVIDENCE.put(key, review.bytes, {
    httpMetadata: { contentType: review.contentType },
    customMetadata: { role: "legacy-review", source: entry.preview_url },
  });
  return serveObject(env.EVIDENCE, key, review.contentType, entry.sha256);
}

async function evidenceRequest(request: Request, env: Env): Promise<Response | null> {
  const url = new URL(request.url);
  if (url.pathname === "/api/evidence") {
    if (request.method === "GET") return listEvidence(request, env);
    if (request.method === "POST") return uploadEvidence(request, env);
    return errorResponse(405, "method not allowed");
  }
  const liveMatch = url.pathname.match(/^\/api\/evidence\/([^/]+)\/(review|original)$/);
  if (liveMatch && request.method === "GET") {
    const captureId = decodeURIComponent(liveMatch[1]);
    if (!captureIdPattern.test(captureId)) return errorResponse(404, "capture not found");
    return serveStoredImage(captureId, liveMatch[2] as "review" | "original", env);
  }
  const legacyMatch = url.pathname.match(/^\/api\/evidence\/legacy\/([^/]+)\/review$/);
  if (legacyMatch && request.method === "GET") {
    const captureId = decodeURIComponent(legacyMatch[1]);
    if (!captureIdPattern.test(captureId)) return errorResponse(404, "capture not found");
    return serveLegacyReview(request, captureId, env);
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
