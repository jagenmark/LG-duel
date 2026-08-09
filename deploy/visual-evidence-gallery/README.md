# LG Duel visual gallery

This is one private Sites page. It reads live evidence metadata and image bytes from R2, and merges older read-only records from `public/evidence/manifest.json`.

Sites owner-only access protects both the page and the R2 upload route.

Use `scripts/publish_visual_evidence.py` from the repository root to check and upload an image in one command. Evidence uploads appear at once and never require a site build or deploy. See `docs/VISUAL-EVIDENCE.md`.

Local checks:

```powershell
npm install
npm run build
node --test tests/rendered-html.test.mjs
```
