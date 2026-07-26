# LG Duel visual gallery

This is one private static Sites page. It reads `public/evidence/manifest.json` and shows each staged image with a short caption and a full-size link.

Do not add a database, upload route, or sign-in code. Sites owner-only access protects the page.

Use `scripts/publish_visual_evidence.py` from the repository root to check and stage an image, then build and privately deploy this site. See `docs/VISUAL-EVIDENCE.md`.

Local checks:

```powershell
npm install
npm run build
node --test tests/rendered-html.test.mjs
```
