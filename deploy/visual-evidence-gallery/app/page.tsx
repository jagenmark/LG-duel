import type { Metadata } from "next";
import { Gallery } from "./Gallery";

export const metadata: Metadata = {
  title: "LG Duel Gallery",
  description: "Private LG Duel screenshots.",
};

export default function Home() {
  return (
    <main>
      <header className="hero">
        <p className="eyebrow">LG Duel · private</p>
        <h1>Project captures.</h1>
        <p className="lede">
          Saved screenshots from LG Duel. Open an image to see it at full size.
        </p>
      </header>
      <Gallery />
      <footer>
        <span>LG Duel</span>
        <span>Private gallery</span>
      </footer>
    </main>
  );
}
