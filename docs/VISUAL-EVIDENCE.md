# Visual evidence publishing

LG Duel uses a private static Sites gallery so images open on a phone. A local file path does not count as delivery.

## Publish an image

1. Save the original under `build/captures/<task-id>/`. Name it `YYYYMMDDTHHMMSSZ-<short-task>-<view>-NN.png` (or `.jpg`, `.jpeg`, `.webp`). Use UTC, lower-case ASCII words, and a new two-digit sequence.
2. Put a JSON record beside it. Start from `docs/visual-evidence.example.json`. The `review` block is optional for ordinary image delivery.
3. Check and stage the image:

   ```powershell
   python scripts/publish_visual_evidence.py --dry-run build/captures/<task-id>/<capture>.json
   python scripts/publish_visual_evidence.py build/captures/<task-id>/<capture>.json
   ```

4. Build and deploy `deploy/visual-evidence-gallery` to its existing private Sites project in the same task. Give the user the hosted preview or full-size link.

The helper checks the path, name, file type, byte limit, sensitivity flag, and hash. It then copies the exact image and a clean record into the gallery and updates its static manifest. It does not send files to another host or need an upload token.

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

The gallery source lives in `deploy/visual-evidence-gallery`. It has no data store, object store, upload route, or custom sign-in code. The site serves checked image assets and `public/evidence/manifest.json`.

Keep the Sites project owner-only. After the first private deploy, record its exact origin and project id in `config/visual-evidence-gallery.json`. If owner-only access cannot be checked, do not publish through a public or shared path without the user’s clear approval.

This static design is the default. Add a storage service only if capture volume makes checked assets too large to keep with the gallery source.

## Failure rule

If staging, build, private deployment, or the hosted image check fails, the image is not yet delivered. Keep the local files, fix the cause, and retry. Report the exact block; do not use a local path as a substitute.

## Focused tests

Run:

```powershell
python -m unittest scripts/test_publish_visual_evidence.py
```
