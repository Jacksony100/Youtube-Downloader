# Video Downloader Pro 5 — Mission & Scope

## Goal

Turn the current compact Video Downloader Pro 4.0.2 codebase into a production-grade 5.x release without changing its core technology or making the interface harder for ordinary users.

The priorities are:

1. reliability;
2. correct task lifecycle;
3. responsive UI;
4. secure runtime updates;
5. maintainable architecture;
6. better format selection;
7. persistent queue/history UX;
8. release engineering.

## Existing baseline to preserve

The application already has valuable working behaviour. Preserve it unless a change is explicitly required by this spec:

- native C++20 / Qt 6 application;
- current dark branded QSS visual language;
- URL validation;
- metadata inspection;
- thumbnail preview;
- presets: Best / 1080p / 720p / 480p / MP3;
- configurable parallel download count;
- per-task progress and speed;
- cancel button;
- history;
- managed `yt-dlp`, Deno, FFmpeg and ffprobe runtime;
- runtime repair/update controls;
- Windows portable package and installer;
- macOS build/package;
- smoke test path through `VDP_SMOKE_TEST`.

## Known problems to solve

### P0 — correctness / security

1. Active cancellation currently kills the process, then normal failure handling can classify the result as `failed` rather than `cancelled`.
2. Queue counters depend on process completion paths and need exactly-once finalization.
3. `QProcess::errorOccurred` needs first-class handling for failed startup/crash cases.
4. Tool updates use synchronous network/event-loop logic from UI-triggered actions and can freeze the window.
5. Runtime downloads are read into memory before being written to disk.
6. checksum parsing must select the checksum for the exact artifact filename, not merely the first 64-char hash in a document.
7. FFmpeg install/update integrity must be covered by a pinned checksum or upstream checksum strategy.
8. Update/install must keep rollback guarantees: a broken new tool must not destroy a known-good previous tool.

### P1 — maintainability / product

1. `MainWindow` owns too many responsibilities.
2. metadata extraction spawns independent `yt-dlp` processes without a bounded metadata queue.
3. completed cards are hidden, not actually removed from task ownership/memory.
4. static quality presets do not expose formats already returned by `yt-dlp` metadata.
5. queue state disappears after closing the app.
6. tests are too narrow for task lifecycle, parser and updater behaviour.
7. version `4.0.2` is duplicated across build/release files.
8. CI should validate pull requests.
9. macOS build artefact should be part of a coherent release flow, not only a workflow artefact.

### P2 — follow-up features

Implement only after P0/P1 is stable:

- playlist/batch workflow;
- subtitles;
- chapter/metadata embedding;
- audio formats beyond MP3;
- clip/time-range download;
- opt-in browser cookie import;
- proxy settings;
- automatic update-and-retry for known transient yt-dlp failures;
- app self-update UX;
- richer history operations.

P2 must never destabilize P0/P1.

## Product principles

### Simple by default

A user should still be able to:

1. paste URL;
2. choose a familiar preset;
3. click download.

Advanced formats should be discoverable, not mandatory.

### No fake progress

Display actual state:

- queued;
- preparing metadata;
- downloading;
- post-processing;
- completed;
- cancelled;
- failed.

Do not report a killed task as a network failure.

### No fragile magic

Every external tool must have:

- known install path;
- version visibility;
- integrity status;
- clear update/repair result;
- safe failure path.

### Preserve user data

Do not remove existing settings/history formats without a migration path. If schemas change, support reading the old schema and upgrade it atomically.

## Out of scope

- replacing Qt Widgets with QML;
- replacing C++ with another language;
- cloud accounts or server backend;
- adware/telemetry;
- monetization changes;
- DRM bypass;
- downloading content the user is not authorized to access;
- bypassing platform payments or access controls.
