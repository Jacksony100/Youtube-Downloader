# Claude Code Execution Checklist

Use this as the short operational checklist while implementing the detailed plan.

## Phase 0 — Baseline

- [ ] Inspect current git status; preserve unrelated user changes.
- [ ] Read README, CHANGELOG, CMake, current core/main window/tests/build workflows.
- [ ] Run existing unit tests.
- [ ] Run normal build.
- [ ] Record baseline failures before changing code.

## Phase 1 — Correct task lifecycle

- [ ] Introduce `TaskState`.
- [ ] Separate cancellation intent from process exit code.
- [ ] Add exactly-once finalization.
- [ ] Handle `QProcess::errorOccurred`.
- [ ] Do not count a task running before process start is confirmed.
- [ ] Add retry.
- [ ] Add lifecycle tests.

## Phase 2 — Extract download core

- [ ] Move task data out of UI widget struct.
- [ ] Introduce `DownloadManager`.
- [ ] Move progress parsing into testable component.
- [ ] Make MainWindow react to manager signals.
- [ ] Ensure terminal cards can be destroyed cleanly.

## Phase 3 — Metadata service

- [ ] Add bounded metadata queue, max 3.
- [ ] Add session cache/deduplication by URL.
- [ ] Introduce typed metadata/format model.
- [ ] Preserve current preview UX.

## Phase 4 — Async tool updater

- [ ] Remove nested event-loop based download path from UI update flow.
- [ ] Use asynchronous streaming downloads.
- [ ] Add progress/cancel/timeout.
- [ ] Implement exact checksum parser.
- [ ] Keep atomic rollback.
- [ ] Make FFmpeg + ffprobe replacement transactional.
- [ ] Add integrity tests.

## Phase 5 — Dynamic formats

- [ ] Preserve simple presets.
- [ ] Parse useful formats from metadata.
- [ ] Normalize duplicates.
- [ ] Add advanced quality UI.
- [ ] Show estimated size only when known.
- [ ] Test normalization using fixtures.

## Phase 6 — Persistent queue/history

- [ ] Add atomic `queue.json` store.
- [ ] Restore non-terminal work safely after restart.
- [ ] Move history access behind repository class.
- [ ] Add re-download/delete/copy URL actions where practical.
- [ ] Preserve old history compatibility.

## Phase 7 — Build/release hardening

- [ ] One canonical app version.
- [ ] Remove duplicated hard-coded release version strings.
- [ ] Run CI on pull requests.
- [ ] Add tag-only release workflow.
- [ ] Publish Windows + macOS artifacts in release workflow.
- [ ] Generate distributable SHA256SUMS.
- [ ] Add third-party notices structure.
- [ ] Document optional Windows signing/macOS notarization.

## Phase 8 — Final verification

- [ ] Full test suite green.
- [ ] Normal app build green.
- [ ] Windows release script green where Windows environment is available.
- [ ] macOS release script green where macOS environment is available.
- [ ] No `TODO`/`TBD` placeholders introduced.
- [ ] No secrets in repo/log output.
- [ ] Update README/CHANGELOG for completed 5.x changes.
- [ ] Create final implementation report.

## Stop conditions

Do not quietly work around these failures:

- checksum mismatch;
- runtime installer rollback failure;
- queue counter corruption;
- test regression in current functionality;
- user data migration failure;
- packaging smoke failure.

Fix them before continuing to dependent phases, or document a genuine external-only blocker.
