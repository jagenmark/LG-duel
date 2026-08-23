"use client";

import { useEffect, useState } from "react";

type Capture = {
  capture_id: string;
  task_id: string;
  title: string;
  description: string;
  captured_at: string;
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

export function Gallery() {
  const [captures, setCaptures] = useState<Capture[]>([]);
  const [status, setStatus] = useState<"loading" | "ready" | "error">("loading");

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
        Loading captures…
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

  if (captures.length === 0) {
    return (
      <section className="empty">
        <p className="eyebrow">Gallery</p>
        <h2>No captures yet.</h2>
      </section>
    );
  }

  return (
    <section className="gallery" aria-label="Captures">
      <div className="section-heading">
        <div>
          <p className="eyebrow">Gallery</p>
          <h2>
            {captures.length} {captures.length === 1 ? "capture" : "captures"}
          </h2>
        </div>
        <p>Newest first</p>
      </div>
      <div className="capture-grid">
        {captures.map((capture) => (
          <article className="capture-card" key={capture.capture_id}>
            <a
              className="preview-link"
              href={capture.full_size_url}
              aria-label={`Open full-size image: ${capture.title}`}
            >
              <img src={capture.preview_url} alt={capture.description} loading="lazy" />
              <span className="open-label">Open full size</span>
            </a>
            <div className="capture-body">
              <div className="capture-topline">
                <span className="task-chip">{capture.task_id}</span>
                <span>{formatDate(capture.captured_at)}</span>
              </div>
              <h3>{capture.title}</h3>
              <p>{capture.description}</p>
              <div className="capture-actions">
                <a href={capture.full_size_url}>Open image</a>
                {capture.original_url ? <a href={capture.original_url}>Original</a> : null}
              </div>
            </div>
          </article>
        ))}
      </div>
    </section>
  );
}
