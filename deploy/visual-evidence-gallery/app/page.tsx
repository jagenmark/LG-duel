import type { Metadata } from "next";
import { Gallery } from "./Gallery";

export const metadata: Metadata = {
  title: "LG Duel Visual Evidence",
  description: "Approved LG Duel screenshots with full-size originals.",
};

export default function Home() {
  return (
    <main>
      <header className="hero">
        <p className="eyebrow">LG Duel · private evidence</p>
        <h1>Reviewed captures, ready on any screen.</h1>
        <p className="lede">
          Each image here passed the project’s file checks. Evidence cards also
          show their independent review. Open a preview for the full-size original.
        </p>
        <div className="trust-row" aria-label="Gallery safeguards">
          <span>Review state shown</span>
          <span>Original files kept</span>
          <span>Private access</span>
        </div>
      </header>
      <Gallery />
      <footer>
        <span>LG Duel visual evidence</span>
        <span>Only approved, task-linked captures belong here.</span>
      </footer>
    </main>
  );
}
