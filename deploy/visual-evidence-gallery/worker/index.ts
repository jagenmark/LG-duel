import handler from "vinext/server/app-router-entry";

const captureIdPattern = /^\d{8}T\d{6}Z-[a-z0-9]+(?:-[a-z0-9]+)*-\d{2}$/;
const sha256Pattern = /^[0-9a-f]{64}$/;

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
}

interface Env {
  ASSETS: { fetch(request: Request): Promise<Response> };
  EVIDENCE: R2BucketBinding;
}

interface ExecutionContext {
  waitUntil(promise: Promise<unknown>): void;
  passThroughOnException(): void;
}

interface CaptureRecord {
  capture_id: string;
  task_id: string;
  title: string;
  description: string;
  captured_at: string;
  preview_url: string;
  full_size_url: string;
  original_url?: string | null;
  review_width?: number | null;
  review_height?: number | null;
}

interface StoredCapture {
  capture_id: string;
  task_id: string;
  title: string;
  description: string;
  captured_at: string;
  sha256: string;
  source_content_type: string;
  review_sha256: string;
  review_key: string;
  original_key: string | null;
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
    capture_id: record.capture_id,
    task_id: record.task_id,
    title: record.title,
    description: record.description,
    captured_at: record.captured_at,
    preview_url: `${base}/image`,
    full_size_url: `${base}/image`,
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
  for (const record of legacy) {
    merged.set(record.capture_id, {
      capture_id: record.capture_id,
      task_id: record.task_id,
      title: record.title,
      description: record.description,
      captured_at: record.captured_at,
      preview_url: record.preview_url,
      full_size_url: record.full_size_url,
      original_url: record.original_url ?? null,
    });
  }
  for (const record of live) merged.set(record.capture_id, publicCapture(record));
  const captures = [...merged.values()].sort((left, right) => (
    right.captured_at.localeCompare(left.captured_at) || right.capture_id.localeCompare(left.capture_id)
  ));
  return json({ schema_version: 1, captures });
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
  kind: "image" | "review" | "original",
  env: Env,
): Promise<Response> {
  const record = await readObjectJson<StoredCapture>(env.EVIDENCE, `records/${captureId}.json`);
  if (!record) return errorResponse(404, "capture not found");
  const original = kind === "original";
  const key = original ? record.original_key : record.review_key;
  if (!key) return errorResponse(404, "original was not retained");
  return serveObject(
    env.EVIDENCE,
    key,
    original ? record.source_content_type : "image/webp",
    original ? record.sha256 : record.review_sha256,
  );
}

async function evidenceRequest(request: Request, env: Env): Promise<Response | null> {
  const url = new URL(request.url);
  if (url.pathname === "/api/evidence") {
    if (request.method === "GET") return listEvidence(request, env);
    return errorResponse(405, "method not allowed");
  }
  const match = url.pathname.match(/^\/api\/evidence\/([^/]+)\/(image|review|original)$/);
  if (match && request.method === "GET") {
    const captureId = decodeURIComponent(match[1]);
    if (!captureIdPattern.test(captureId)) return errorResponse(404, "capture not found");
    return serveStoredImage(captureId, match[2] as "image" | "review" | "original", env);
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
