# Project agent rules

## Visual evidence

- Publish each image that a task intends to show the user to the private LG Duel gallery as part of the task. Do not add a second approval step for ordinary image delivery.
- When a task claims that an image proves visual correctness or task completion, treat that evidence claim as unfinished until an independent reviewer gives the capture a `pass` verdict and the image appears in the gallery.
- Do not use a local path as the user-facing result. Give the user the hosted gallery page or hosted image link, with an embedded preview when the client supports it.
- Name captures `YYYYMMDDTHHMMSSZ-<short-task>-<view>-NN.<ext>`. Use UTC, lower-case ASCII words, and a two-digit sequence. Never overwrite an earlier capture.
- Keep the original image and its metadata together. The metadata must name the task and capture, the capture author, the UTC capture time, a clear title and note, and the sensitivity check. Add review data when the task will claim visual proof or completion.
- For evidence claims, the reviewer must not be the capture author. `pass` supports the claim; `fail`, `needs_changes`, missing review, and self-review do not.
- Run `scripts/publish_visual_evidence.py --dry-run <metadata.json>` first, then run it without `--dry-run` to stage the image. Deploy the gallery privately in the same task. Do not stage sensitive, secret, personal, or unrelated files.
- Use only the project-owned Sites gallery. Do not use public image hosts.
- If gallery publication fails, say that the image is not yet delivered. Keep the local original and metadata, retry or report the exact block, and never claim delivery from a local path alone.

See `docs/VISUAL-EVIDENCE.md` for the record format, review flow, setup, and commands.
