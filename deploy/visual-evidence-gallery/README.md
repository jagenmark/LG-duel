# LG Duel visual gallery

This is one private Sites page. It reads live evidence metadata from D1, image bytes from R2, and merges older checked-in records from `public/evidence/manifest.json`.

Sites owner-only access protects the page. The upload route also checks a separate worker token before it writes D1 or R2.

Use `scripts/publish_visual_evidence.py` from the repository root to check and upload an image in one command. Evidence uploads appear at once and never require a site build or deploy. See `docs/VISUAL-EVIDENCE.md`.

Local checks:

```powershell
npm install
npm run build
node --test tests/rendered-html.test.mjs
```
