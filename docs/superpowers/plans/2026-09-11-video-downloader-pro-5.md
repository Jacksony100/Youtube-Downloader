# Video Downloader Pro 5 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Harden Video Downloader Pro 4.0.2 into a responsive, secure, maintainable Video Downloader Pro 5.x with correct task lifecycle, asynchronous runtime updates, bounded metadata processing, dynamic formats, persistent queue state, stronger tests, and coherent CI/release automation.

**Architecture:** Keep the native C++20/Qt 6 Widgets application and existing product UI. Incrementally extract task/process orchestration from `MainWindow` into focused Qt services and pure parsers, then add persistent storage and release hardening without changing the core download engine (`yt-dlp` + Deno + FFmpeg).

**Tech Stack:** C++20, Qt 6.6+ Core/Widgets/Network/Test, CMake 3.21+, Ninja, yt-dlp, Deno, FFmpeg/ffprobe, Inno Setup, GitHub Actions.

**Spec:** `docs/claude/00-MISSION.md`, `docs/claude/01-ARCHITECTURE.md`, `docs/claude/02-FEATURE-SPEC.md`, `docs/claude/03-SECURITY-RELEASE.md`, `docs/claude/04-TESTING-ACCEPTANCE.md`

## Global Constraints

- Preserve C++20 + Qt 6 Widgets; no Electron/Python rewrite.
- Preserve existing settings/history/runtime directories and migrate schemas safely.
- Keep existing simple download presets even after dynamic formats are added.
- Network tool updates must be asynchronous, streaming, cancellable, integrity-checked, and rollback-safe.
- Never log cookies, tokens, proxy passwords, or authorization headers.
- Do not add DRM/paywall/authorization bypass behaviour.
- No TODO/TBD/placeholder implementations in final code.
- Keep the project buildable and tests runnable after every completed task.

---

### Task 1: Baseline and regression harness

**Files:**
- Modify: `tests_cpp/test_core.cpp`
- Create as needed: `tests_cpp/test_progress_parser.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: current `formatPreset`, `isVersionNewer`, `buildMetadataArguments`, `buildDownloadArguments`, `sanitizeError`.
- Produces: expanded test target(s) that later refactors must keep green.

- [ ] **Step 1: Inspect and run the unmodified baseline**

Run the current configure/build/test commands and record failures before modifications.

```bash
cmake -S . -B build-cpp -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$Qt6_DIR" -DBUILD_TESTING=ON
cmake --build build-cpp
ctest --test-dir build-cpp --output-on-failure
```

- [ ] **Step 2: Add regression tests for current argument generation**

Assert that download arguments retain:

```text
--ignore-config
--no-playlist
--js-runtimes deno:<path>
--progress-template
--print after_move:vdppath:%(filepath)s
--ffmpeg-location
```

Also assert audio preset contains `-x --audio-format mp3`.

- [ ] **Step 3: Add regression tests for `sanitizeError`**

Cover 403, 429, unavailable, private/auth-ish, network and JS runtime messages.

- [ ] **Step 4: Run the tests**

Expected: PASS before structural refactoring begins.

- [ ] **Step 5: Commit coherent baseline test additions**

Use a commit message similar to:

```bash
git commit -m "test: expand downloader regression coverage"
```

---

### Task 2: Introduce explicit task state and exactly-once finalization

**Files:**
- Create: `src/downloads/download_task.hpp`
- Create: `src/downloads/download_task.cpp` if non-trivial methods are needed
- Modify: `src/main_window.hpp`
- Modify: `src/main_window.cpp`
- Create: `tests_cpp/test_task_state.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `enum class TaskState` and serializable `DownloadTaskData` model.
- Produces: a finalization path that accepts an intended terminal state rather than inferring cancellation from process exit code.

- [ ] **Step 1: Write failing lifecycle tests**

Test these rules through a small pure helper/state object if necessary:

```text
Queued -> Downloading -> Completed
Queued -> Cancelled
Downloading + cancellationRequested + nonzero exit -> Cancelled
Downloading + nonzero exit without cancellation -> Failed
Terminal state cannot finalize twice
```

- [ ] **Step 2: Add the task state model**

Implement:

```cpp
enum class TaskState {
    Queued,
    Preparing,
    Downloading,
    PostProcessing,
    Completed,
    Cancelled,
    Failed
};
```

Add helpers such as `isTerminal(TaskState)` and a Russian UI-label mapping in one place.

- [ ] **Step 3: Add cancellation intent before process termination**

When the user cancels a running task, set cancellation intent/state before `terminate()`/`kill()`.

Do not classify the resulting non-zero process exit as `Failed`.

- [ ] **Step 4: Add exactly-once finalization guard**

A task must release a parallel slot at most once, regardless of which order `errorOccurred` and `finished` arrive.

- [ ] **Step 5: Handle process start failures**

Connect `QProcess::errorOccurred` and specifically handle `FailedToStart` with a friendly runtime/process-start error.

Do not count a task as actively running until `QProcess::started` fires.

- [ ] **Step 6: Run focused lifecycle tests and full suite**

Expected: cancellation tests PASS and existing tests remain green.

---

### Task 3: Extract progress parsing into a pure component

**Files:**
- Create: `src/downloads/progress_parser.hpp`
- Create: `src/downloads/progress_parser.cpp`
- Create: `tests_cpp/test_progress_parser.cpp`
- Modify: `src/main_window.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: stdout byte stream from yt-dlp.
- Produces: parsed events for progress and final output path.

Suggested event model:

```cpp
struct DownloadProgress {
    std::optional<double> percent;
    QString speed;
    QString eta;
    std::optional<qint64> downloadedBytes;
    std::optional<qint64> totalBytes;
};

struct ProgressParseResult {
    std::optional<DownloadProgress> progress;
    std::optional<QString> finalPath;
};
```

- [ ] **Step 1: Add tests for complete lines and split buffers**

Feed one progress line, one `vdppath` line, malformed input, and a line split across two byte chunks.

- [ ] **Step 2: Implement parser/buffer class**

It must never throw/crash on malformed values.

- [ ] **Step 3: Replace parsing code in `MainWindow` with the parser**

Preserve current UI progress behaviour.

- [ ] **Step 4: Run parser tests and full suite**

---

### Task 4: Create DownloadManager and remove process ownership from MainWindow

**Files:**
- Create: `src/downloads/download_manager.hpp`
- Create: `src/downloads/download_manager.cpp`
- Modify: `src/main_window.hpp`
- Modify: `src/main_window.cpp`
- Create: `tests_cpp/test_download_manager.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: toolchain paths, output directory, format selection.
- Produces: task lifecycle signals and queue counts.

Required public shape:

```cpp
class DownloadManager final : public QObject {
    Q_OBJECT
public:
    QString enqueue(const DownloadRequest& request);
    void cancel(const QString& taskId);
    void retry(const QString& taskId);
    void removeTerminal(const QString& taskId);
    void setParallelLimit(int limit);
    const DownloadTaskData* task(const QString& taskId) const;

signals:
    void taskAdded(const QString& taskId);
    void taskChanged(const QString& taskId);
    void taskRemoved(const QString& taskId);
    void queueChanged(int running, int queued);
};
```

- [ ] **Step 1: Write tests with an injectable/fake process runner or process factory**

Verify max concurrency, start failure, normal completion, cancellation and exactly-once slot release.

- [ ] **Step 2: Move pending/running bookkeeping to DownloadManager**

`MainWindow` must no longer own `pending_` and `runningCount_` as authoritative queue state.

- [ ] **Step 3: Move QProcess ownership for downloads into DownloadManager**

UI only reacts to task changes.

- [ ] **Step 4: Implement terminal task removal**

Clearing a card must remove the UI widget and allow manager data for that terminal task to be released when appropriate.

- [ ] **Step 5: Re-run manual queue behaviour**

Verify current simple workflow remains unchanged from the user's perspective.

---

### Task 5: Add bounded MetadataService with typed metadata and caching

**Files:**
- Create: `src/metadata/metadata_models.hpp`
- Create: `src/metadata/metadata_service.hpp`
- Create: `src/metadata/metadata_service.cpp`
- Modify: `src/main_window.cpp`
- Create: `tests_cpp/test_metadata_service.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: URL and toolchain paths.
- Produces: `VideoMetadata` including normalized `FormatOption` list.

- [ ] **Step 1: Create metadata models**

Include title, uploader, duration, thumbnail URL and raw/normalized format information.

- [ ] **Step 2: Write concurrency/cache tests**

Use a fake process provider so 10 metadata requests demonstrate no more than 3 active workers.

Test duplicate URL requests reuse an in-flight/cache result.

- [ ] **Step 3: Implement queue with max active metadata processes = 3**

Release capacity on both success and failure.

- [ ] **Step 4: Replace `checkUrl` and per-task `hydrateTaskMetadata` direct process creation**

Use MetadataService requests instead.

- [ ] **Step 5: Preserve thumbnail and preview behaviour**

Metadata failure must show a friendly message but should not destroy a syntactically valid URL workflow.

---

### Task 6: Implement dynamic format normalization while preserving simple presets

**Files:**
- Modify: `src/metadata/metadata_models.hpp`
- Create: `src/metadata/format_normalizer.hpp`
- Create: `src/metadata/format_normalizer.cpp`
- Modify: `src/main_window.cpp` and/or extracted downloads page code
- Create: `tests_cpp/test_format_normalizer.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: yt-dlp `formats` JSON array.
- Produces: concise user-facing `FormatOption` vector.

- [ ] **Step 1: Add fixture-driven normalization tests**

Include video-only, audio-only, combined, duplicate resolution/codec, missing filesize, `filesize_approx` fallback.

- [ ] **Step 2: Normalize useful video options**

Group by meaningful combinations such as resolution + fps + codec family/container.

Do not dump every raw yt-dlp format row into the UI.

- [ ] **Step 3: Normalize audio options**

Keep current MP3 presets and expose original/best audio in advanced mode.

- [ ] **Step 4: Add advanced selector UI**

Simple preset combo remains the primary control.

Advanced choices appear only when metadata is known.

- [ ] **Step 5: Ensure `buildDownloadArguments` can consume either preset selector or normalized explicit selector**

Do not regress MP3 extraction.

---

### Task 7: Replace synchronous runtime downloads with asynchronous ToolUpdater

**Files:**
- Create: `src/runtime/tool_updater.hpp`
- Create: `src/runtime/tool_updater.cpp`
- Move/refactor from: `src/core.cpp`, `src/core.hpp`
- Modify: `src/main_window.cpp`
- Create: `tests_cpp/test_checksum_parser.cpp`
- Create: `tests_cpp/test_tool_install.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: asynchronous update job signals `progress`, `finished`, `failed`, `cancelled`.
- Consumes: tool descriptors from ToolchainManager/lock manifest.

- [ ] **Step 1: Extract checksum parsing into a pure function and make tests fail first**

Test exact filename selection, GNU hash lines, BSD-style `SHA256 (file) =`, malformed data and single-artifact `Hash:` mode.

- [ ] **Step 2: Implement streaming asynchronous download**

Use a long-lived `QNetworkAccessManager`.

On `readyRead`, write chunks to a staged file.

Do not use nested `QEventLoop::exec()`.

- [ ] **Step 3: Add timeout and cancellation**

Abort network reply safely and delete incomplete staged files.

- [ ] **Step 4: Verify checksum before installation**

Wrong or missing checksum must terminate update with no active runtime replacement.

- [ ] **Step 5: Implement atomic rollback**

Keep current good executable until new artifact is verified and staged.

For FFmpeg + ffprobe, install/rollback them as one pair.

- [ ] **Step 6: Update tools page UI**

Show progress and keep window responsive.

Disable conflicting update buttons while an update is active.

- [ ] **Step 7: Run tests and perform manual responsiveness check**

---

### Task 8: Add reviewed FFmpeg integrity lock

**Files:**
- Create: `runtime/toolchain-lock.json` or `resources/toolchain-lock.json`
- Create: `src/runtime/toolchain_lock.hpp`
- Create: `src/runtime/toolchain_lock.cpp`
- Modify: `scripts/build_release_windows.ps1`
- Modify: `scripts/build_release.sh`
- Modify: runtime updater code
- Create/update tests validating lock schema

**Interfaces:**
- Produces: one repository-controlled source of URL + expected SHA256 for artifacts that lack an easily consumed upstream checksum endpoint.

- [ ] **Step 1: Inspect actual current distributor checksum options**

Use an upstream/distributor-provided checksum when available.

- [ ] **Step 2: If pinning is required, download the intended archive through the release preparation process and record its real SHA256**

Never commit a fake or placeholder digest.

- [ ] **Step 3: Make both build scripts and runtime updater verify the same expected artifact identity**

Avoid security divergence between packaged fallback tools and in-app updates.

- [ ] **Step 4: Fail closed on digest mismatch**

Do not install/update the tool.

---

### Task 9: Add persistent queue storage

**Files:**
- Create: `src/storage/queue_store.hpp`
- Create: `src/storage/queue_store.cpp`
- Modify: `src/downloads/download_manager.cpp`
- Modify: application/main window initialization
- Create: `tests_cpp/test_queue_store.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes/produces: serializable `DownloadTaskData` subset.
- Storage path: `AppPaths::dataDir/queue.json`.

- [ ] **Step 1: Add schema-based serialization tests**

Use schema `1`.

- [ ] **Step 2: Implement atomic save with `QSaveFile`**

Persist non-terminal tasks after queue/task changes with sensible debounce if needed.

- [ ] **Step 3: Implement recovery rules**

Previous `Preparing`, `Downloading`, `PostProcessing` restore as queued/recovered.

Terminal tasks do not restore into active queue.

- [ ] **Step 4: Restore queue after services/UI initialize**

Show `Восстановлено задач: N` without a blocking modal.

- [ ] **Step 5: Test corrupt/old queue files**

App must continue to launch with a friendly diagnostic and an empty queue if recovery is impossible.

---

### Task 10: Extract history repository and add useful row actions

**Files:**
- Create: `src/storage/history_repository.hpp`
- Create: `src/storage/history_repository.cpp`
- Modify: `src/main_window.cpp`
- Create: `tests_cpp/test_history_repository.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes existing `history.json` schema.
- Produces repository methods for append/search/delete/list.

- [ ] **Step 1: Write compatibility tests against current history JSON shape**

- [ ] **Step 2: Move raw JSON file handling out of MainWindow**

Use atomic `QSaveFile` writes.

- [ ] **Step 3: Add history actions**

At minimum implement re-download, copy URL and delete record. Preserve double-click/open behaviour where sensible.

- [ ] **Step 4: Keep history bounded**

Retain a documented cap or replace it with a clear retention policy. Do not allow unbounded growth silently.

---

### Task 11: Centralize application version

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `scripts/build_release_windows.ps1`
- Modify: `scripts/build_release.sh`
- Modify: `installer/VideoDownloaderPro.iss`
- Modify: `.github/workflows/build-windows-x64.yml`
- Modify: `.github/workflows/build-macos.yml`
- Modify: `README.md`

**Interfaces:**
- Produces: one canonical version, derived from CMake project version or one generated version file.

- [ ] **Step 1: Add a build/script-accessible canonical version output**

For example generate a small CMake-produced version file or parse the project version in one helper script.

- [ ] **Step 2: Remove hard-coded `4.0.2` from packaging scripts and artifact names**

- [ ] **Step 3: Ensure installer receives version via `/DAppVersion=<version>`**

- [ ] **Step 4: Make README release filename generic or generated so it does not become stale every patch**

- [ ] **Step 5: Run packaging scripts far enough to verify version propagation**

---

### Task 12: CI pull-request validation and release workflow

**Files:**
- Modify: `.github/workflows/build-windows-x64.yml`
- Modify: `.github/workflows/build-macos.yml`
- Create: `.github/workflows/release.yml`

**Interfaces:**
- PR/push workflows validate.
- tag workflow packages/releases.

- [ ] **Step 1: Add `pull_request` trigger for `main` to both existing build workflows**

- [ ] **Step 2: Keep upload-artifact on successful builds**

Use version-aware names where available.

- [ ] **Step 3: Add tag-only release workflow for `v*`**

The workflow must not publish on ordinary pushes.

- [ ] **Step 4: Have release workflow obtain Windows and macOS packages only from successful build jobs**

- [ ] **Step 5: Generate SHA256SUMS for distributables**

- [ ] **Step 6: Upload Windows installer, Windows portable ZIP, macOS ZIP, and checksum file to GitHub Release**

- [ ] **Step 7: Keep signing/notarization optional behind secrets**

Unsigned contributor builds must still compile.

---

### Task 13: Logs and redacted diagnostics

**Files:**
- Create: `src/diagnostics/diagnostics.hpp`
- Create: `src/diagnostics/diagnostics.cpp`
- Modify: tools/about UI
- Create: `tests_cpp/test_diagnostics.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: redacted diagnostic report string and bounded log files.

- [ ] **Step 1: Add redaction tests**

Ensure common forms of cookies/auth/proxy credentials are removed from diagnostic output.

- [ ] **Step 2: Add bounded logging**

Use existing `AppPaths::logsDir` instead of leaving the directory unused.

- [ ] **Step 3: Add `Скопировать диагностику` action**

Include app/OS/Qt/tool versions and last sanitized failure, never secrets.

---

### Task 14: Documentation, third-party notices, final regression pass

**Files:**
- Modify: `README.md`
- Modify: `CHANGELOG.md`
- Create: `THIRD_PARTY_NOTICES.md`
- Create: `docs/production/VIDEO-DOWNLOADER-PRO-5-IMPLEMENTATION-REPORT.md`

**Interfaces:**
- Produces: honest release/readiness documentation matching the implemented software.

- [ ] **Step 1: Update README for 5.x features actually implemented**

Do not advertise P2 items that were not completed.

- [ ] **Step 2: Update CHANGELOG**

Separate Added / Changed / Fixed / Security as appropriate.

- [ ] **Step 3: Add third-party notices structure**

Document redistributed runtime components and applicable license sources. Do not invent a project license if the owner has not selected one.

- [ ] **Step 4: Run full test suite**

```bash
ctest --test-dir build-cpp --output-on-failure
```

- [ ] **Step 5: Run platform packaging/smoke tests available in the current environment**

Use the repository's release scripts.

- [ ] **Step 6: Search for unfinished placeholders**

Check introduced/modified code and docs for `TODO`, `TBD`, fake hashes, dead commented implementations and accidental secrets.

- [ ] **Step 7: Write final implementation report**

Set release readiness to `READY`, `READY_WITH_BLOCKERS`, or `NOT_READY` based on real evidence.

---

## P2 Extension Plan — only after all P0/P1 gates pass

Execute these as separate independently reviewable tasks, not inside the hardening tasks above:

1. playlist inspection and selective batch enqueue;
2. subtitles and language selection;
3. best/original audio + M4A/Opus options;
4. clip/time-range download;
5. opt-in browser-cookie integration with strict redaction;
6. proxy settings;
7. known-transient-error update/retry policy;
8. richer history with thumbnail/date/size/actions;
9. application self-update UX.

Each P2 feature must include its own tests and must not weaken the P0 runtime/security guarantees.
