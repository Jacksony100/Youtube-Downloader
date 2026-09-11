# Video Downloader Pro 5 — Testing & Acceptance Gates

## Test philosophy

Core behaviour must be testable without contacting YouTube or downloading large binaries.

Prefer pure parsers/state machines and dependency injection around process/network boundaries.

Keep a small number of packaging smoke tests for integration confidence.

## Required unit test groups

## A. Task state machine

Cover at least:

1. queued -> started -> completed;
2. queued -> cancelled without starting;
3. running -> user cancellation -> process non-zero -> cancelled, not failed;
4. process failed to start -> failed and parallel slot released;
5. process crash -> failed exactly once;
6. duplicate finish/error signals cannot decrement running count twice;
7. retry creates a valid new run;
8. completed task cannot transition back to downloading accidentally.

## B. Progress parser

Input examples:

```text
download:42.5%|5.1MiB/s|00:12|10485760|20971520
vdppath:C:/Downloads/Test Video.mp4
```

Assert:

- 42.5 parsed correctly;
- speed/ETA parsed;
- output path retained;
- malformed/partial lines do not throw;
- buffered line split across two reads is reassembled correctly;
- a line with missing optional bytes still does not crash.

## C. Checksum parser

Test exact artifact selection.

Cases:

```text
111...  wrong.exe
222... *yt-dlp.exe
```

Request `yt-dlp.exe` -> returns `222...`.

Also test:

- missing target filename -> no checksum;
- malformed hash -> no checksum;
- upper/lowercase hex;
- CRLF and LF;
- single-artifact `Hash: <sha>` accepted only when explicitly marked single-artifact;
- `SHA256 (filename) = <sha>` form.

## D. Atomic installer helpers

Use temporary directories.

Cases:

- install succeeds and backup removed;
- copy fails -> original restored;
- post-install validation fails -> original restored;
- FFmpeg pair transaction failure restores both previous files.

No actual external binaries are needed; tiny executable/test files or injected validator are sufficient.

## E. Queue persistence

Test:

- queued tasks saved and loaded;
- running task from previous session restores as queued/recovered;
- terminal tasks not restored to pending queue;
- corrupt JSON does not crash app;
- old/missing schema handled safely;
- atomic save leaves valid previous file when commit fails, where testable.

## F. Metadata queue

With fake/injected process runner:

- active jobs never exceed configured maximum 3;
- 10 requests eventually complete;
- identical URL requests deduplicate/cache;
- failed request releases capacity;
- cancellation or object destruction does not emit into deleted UI targets.

## G. Format normalization

Feed representative yt-dlp format JSON.

Assert:

- duplicate height/codec choices are normalized;
- audio-only is separated;
- filesize uses `filesize`, then `filesize_approx` fallback;
- entries without useful media are ignored;
- simple presets remain available even when metadata formats are absent.

## H. Error sanitization

Retain current friendly mappings and add tests for:

- missing JS runtime;
- 403;
- 429;
- private/auth-required;
- unavailable;
- geo/age restriction;
- process start failure;
- cancelled;
- empty/unknown error.

Do not classify user cancellation from raw stderr string matching; it must come from task lifecycle state.

## Existing build commands

Use the project's current CMake/Ninja flow. The following baseline shape should remain valid:

```bash
cmake -S . -B build-cpp -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$Qt6_DIR" \
  -DBUILD_TESTING=ON
cmake --build build-cpp
ctest --test-dir build-cpp --output-on-failure
```

On Windows use equivalent PowerShell escaping and the existing release script.

## Manual acceptance — P0/P1

The agent must perform everything feasible locally and document anything requiring external credentials.

### Gate 1 — baseline launch

- app opens;
- sidebar/pages render;
- settings load;
- no immediate crash;
- managed runtime status appears.

### Gate 2 — queue lifecycle

- add at least two test tasks or fake-process tasks;
- queued count correct;
- running count never exceeds configured limit;
- cancel queued task -> `Отменено`;
- cancel active task -> `Отменено`, not `Ошибка`;
- clearing terminal card removes it from UI and memory ownership.

### Gate 3 — UI responsiveness

Start a runtime download/update using a controllable test/fake endpoint where possible.

During transfer:

- window can move;
- navigation can change;
- repaint continues;
- cancel works;
- progress changes.

No nested blocking event loop in production updater path.

### Gate 4 — secure updater

- wrong checksum prevents install;
- old runtime remains usable after failed update;
- exact checksum filename test passes;
- FFmpeg strategy is verified/pinned;
- runtime manifest reflects installed state only after success.

### Gate 5 — persistence

- enqueue items;
- close app safely;
- reopen;
- unfinished items restored;
- no completed item is incorrectly requeued.

### Gate 6 — dynamic formats

With metadata fixture or live link when appropriate:

- simple presets remain;
- advanced choices appear when metadata has formats;
- labels do not expose cryptic raw format IDs alone;
- estimated size is shown only when data exists.

### Gate 7 — packaging

Windows:

- release script completes;
- tests pass;
- portable archive exists;
- installer exists;
- packaged smoke test passes.

macOS:

- release script completes on macOS runner/local machine;
- tests pass;
- `.app` package exists;
- zip exists;
- smoke test passes.

### Gate 8 — CI configuration

- PR events enabled;
- push-to-main remains enabled;
- build artifacts names derive from canonical version where practical;
- tag release workflow does not publish on ordinary pushes.

## Regression gates

Verify current user-visible features still work:

- Paste button;
- Check link;
- Add to queue;
- preset selection;
- thumbnail load;
- title/uploader/duration preview;
- output folder setting;
- parallel setting;
- auto-open option;
- history search;
- tool status page;
- runtime repair/update;
- Ctrl+L / Ctrl+O / Ctrl+D / Ctrl+I shortcuts as applicable.

## Final report requirements

Write `docs/production/VIDEO-DOWNLOADER-PRO-5-IMPLEMENTATION-REPORT.md` with:

```text
Release readiness: READY / READY_WITH_BLOCKERS / NOT_READY
Branch:
HEAD SHA:
Platforms validated:

Implemented:
- ...

Tests:
- command
- result

Packaging:
- Windows: ...
- macOS: ...

Security gates:
- ...

External blockers:
- signing/notarization credentials if unavailable
- other genuine external-only dependencies

Deferred P2:
- ...
```

Do not mark `READY` if a mandatory P0/P1 acceptance gate is failing.
