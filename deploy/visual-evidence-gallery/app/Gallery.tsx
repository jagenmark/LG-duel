"use client";

import { useEffect, useState } from "react";

type Capture = {
  capture_id: string;
  task_id: string;
  title: string;
  description: string;
  captured_at: string;
  captured_by: string;
  review_status: "not_reviewed" | "pass" | "fail" | "needs_changes";
  reviewer: string | null;
  reviewed_at: string | null;
  review_notes: string | null;
  sha256: string;
  size_bytes: number;
  preview_url: string;
  full_size_url: string;
  original_url?: string | null;
};

function formatDate(value: string) {
  return new Intl.DateTimeFormat(undefined, {
    dateStyle: "medium",
    timeStyle: "short",
  }).format(new Date(value));
}

function formatBytes(value: number) {
  if (value < 1024 * 1024) return `${Math.ceil(value / 1024)} KB`;
  return `${(value / (1024 * 1024)).toFixed(1)} MB`;
}

export function Gallery() {
  const [captures, setCaptures] = useState<Capture[]>([]);
  const [status, setStatus] = useState<"loading" | "ready" | "error">("loading");
  const approvedCaptures = captures.filter((capture) => capture.review_status === "pass");

  useEffect(() => {
    const controller = new AbortController();
    fetch("/api/evidence", { signal: controller.signal, cache: "no-store" })
      .then(async (response) => {
        if (!response.ok) throw new Error("The gallery could not load.");
        return (await response.json()) as { captures: Capture[] };
      })
      .then((value) => {
        setCaptures(value.captures);
        setStatus("ready");
      })
      .catch((error: unknown) => {
        if (error instanceof DOMException && error.name === "AbortError") return;
        setStatus("error");
      });
    return () => controller.abort();
  }, []);

  if (status === "loading") {
    return (
      <section className="gallery-state" aria-live="polite">
        <span className="status-dot" />
        Loading approved captures…
      </section>
    );
  }

  if (status === "error") {
    return (
      <section className="gallery-state error" role="alert">
        The gallery could not load. Try again in a moment.
      </section>
    );
  }

  if (approvedCaptures.length === 0) {
    return (
      <section className="empty">
        <p className="eyebrow">No approved captures yet</p>
        <h2>The first reviewed image will appear here.</h2>
        <p>Local files stay out of this gallery until review and publication pass.</p>
      </section>
    );
  }

  return (
    <section className="gallery" aria-label="Approved captures">
      <div className="section-heading">
        <div>
          <p className="eyebrow">Published evidence</p>
          <h2>
            {approvedCaptures.length} approved {approvedCaptures.length === 1 ? "capture" : "captures"}
          </h2>
        </div>
        <p>Newest first</p>
      </div>
      <div className="capture-grid">
        {approvedCaptures.map((capture) => (
          <article className="capture-card" key={capture.capture_id}>
            <a
              className="preview-link"
              href={capture.full_size_url}
              aria-label={`Open full-size image: ${capture.title}`}
            >
              {/* Review links always use the compact derivative. */}
              <img src={capture.preview_url} alt={capture.description} loading="lazy" />
              <span className="open-label">Open full size</span>
            </a>
            <div className="capture-body">
              <div className="capture-topline">
                <span className="task-chip">{capture.task_id}</span>
                <span>{formatBytes(capture.size_bytes)}</span>
              </div>
              <h3>{capture.title}</h3>
              <p>{capture.description}</p>
              <dl>
                <div>
                  <dt>Captured</dt>
                  <dd>{formatDate(capture.captured_at)} · {capture.captured_by}</dd>
                </div>
                <div>
                  <dt>Review</dt>
                  <dd>
                    {capture.review_status === "pass"
                      ? `Passed by ${capture.reviewer}`
                      : capture.review_status === "not_reviewed"
                        ? "Not claimed as reviewed evidence"
                        : capture.review_status.replace("_", " ")}
                  </dd>
                </div>
              </dl>
              {capture.review_notes ? (
                <p className="review-note">“{capture.review_notes}”</p>
              ) : null}
              <div className="capture-actions">
                <a href={capture.full_size_url}>Compact review image</a>
                {capture.original_url ? <a href={capture.original_url}>Original</a> : null}
                <span title={capture.sha256}>SHA-256 {capture.sha256.slice(0, 10)}…</span>
              </div>
            </div>
          </article>
        ))}
      </div>
    </section>
  );
}
