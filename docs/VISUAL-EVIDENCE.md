# Visual evidence publishing

LG Duel uses a private Sites gallery so images open on a phone. A local file path does not count as delivery. New images and records live in durable object and metadata storage; publishing an image does not rebuild or redeploy the site.

## Publish an image

1. Save the original under `build/captures/<task-id>/`. Name it `YYYYMMDDTHHMMSSZ-<short-task>-<view>-NN.png` (or `.jpg`, `.jpeg`, `.webp`). Use UTC, lower-case ASCII words, and a new two-digit sequence.
2. Put a JSON record beside it. Start from `docs/visual-evidence.example.json`. The `review` block is optional for ordinary image delivery.
3. Check the image, then upload it:

   ```powershell
   python scripts/publish_visual_evidence.py --dry-run build/captures/<task-id>/<capture>.json
   python scripts/publish_visual_evidence.py build/captures/<task-id>/<capture>.json
   ```

4. Give the user the hosted review link returned by the upload. Do not build, save a Sites version, restore an old version, or deploy the gallery for an evidence upload.

The helper checks the path, name, file type, byte limit, sensitivity flag, and hash. One upload request then stores metadata in D1 and image bytes in R2. The upload is idempotent: retrying the same capture and hash is safe, while reusing an id for different bytes is blocked.

The server creates a review image capped at 1600 pixels on its longest edge and uses WebP quality 82. Gallery cards and returned review links use that compact image. This keeps repeated agent reviews small. An independently passed evidence record keeps its original automatically. For another capture that needs an original, set `"retain_original": true`; ordinary delivery defaults to the compact copy only.

The helper reads two private tokens without adding flags to the upload command:

- `LG_VISUAL_EVIDENCE_UPLOAD_TOKEN`, or `%USERPROFILE%/.codex/secrets/lg-duel-visual-evidence-upload-token`, lets the worker accept the upload.
- `LG_VISUAL_EVIDENCE_SITES_TOKEN`, or `%USERPROFILE%/.codex/secrets/lg-duel-visual-evidence-sites-token`, lets the command reach the private Sites worker.

Never put either token in Git, metadata, command output, or a gallery URL.

Any image meant for the user should go through this flow at once. There is no extra publication approval step.

## When an image is evidence

An image needs an independent review only when the task will claim that the image proves visual correctness or completion. Add:

```json
"review": {
  "reviewer": "review-agent",
  "reviewed_at": "2026-07-26T12:05:00Z",
  "verdict": "pass",
  "notes": "The capture proves the task and reads well on a phone."
}
```

The reviewer must differ from `captured_by`. A missing review does not block ordinary image delivery, but it cannot support an evidence or completion claim.

## Private Sites setup

The gallery source lives in `deploy/visual-evidence-gallery`. D1 stores upload records and R2 stores compact review images plus optional originals. The private worker accepts token-authenticated uploads at `/api/evidence`. Sites checks its private access token first; the worker then checks its own upload token. The gallery reads that live endpoint, merging stored uploads with the older checked-in manifest so old evidence remains available.

Keep the Sites project owner-only. After the first private deploy, record its exact origin and project id in `config/visual-evidence-gallery.json`. If owner-only access cannot be checked, do not publish through a public or shared path without the user’s clear approval.

Changing the gallery application still uses the normal private Sites build and deploy flow. Adding evidence does not.

## Failure rule

If validation, upload, or the hosted image check fails, the image is not yet delivered. Keep the local files, fix the cause, and retry the same command. Report the exact block; do not use a local path as a substitute.

## Focused tests

Run:

```powershell
python -m unittest scripts/test_publish_visual_evidence.py
```
