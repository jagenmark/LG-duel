export const evidenceTableSql = `
CREATE TABLE IF NOT EXISTS evidence_captures (
  capture_id TEXT PRIMARY KEY,
  task_id TEXT NOT NULL,
  status TEXT NOT NULL CHECK (status IN ('pending', 'ready')),
  title TEXT NOT NULL,
  description TEXT NOT NULL,
  captured_at TEXT NOT NULL,
  captured_by TEXT NOT NULL,
  review_status TEXT NOT NULL,
  reviewer TEXT,
  reviewed_at TEXT,
  review_notes TEXT,
  sha256 TEXT NOT NULL,
  source_size_bytes INTEGER NOT NULL,
  source_content_type TEXT NOT NULL,
  review_key TEXT NOT NULL,
  review_content_type TEXT,
  review_size_bytes INTEGER,
  review_width INTEGER,
  review_height INTEGER,
  original_key TEXT,
  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL
)
`;

export const evidenceReadyIndexSql = `
CREATE INDEX IF NOT EXISTS idx_evidence_captures_ready_time
ON evidence_captures(status, captured_at DESC, capture_id DESC)
`;
