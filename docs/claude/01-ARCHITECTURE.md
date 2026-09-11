# Video Downloader Pro 5 — Target Architecture

## Objective

Reduce `MainWindow` to presentation/orchestration glue and move stateful behaviour into focused components that are independently testable.

Do not perform a giant rewrite. Extract responsibility incrementally while keeping existing behaviour working.

## Target source layout

The exact names may be adapted to the current repository, but responsibilities must remain separated.

```text
src/
  app/
    application.cpp
    application.hpp

  downloads/
    download_task.cpp
    download_task.hpp
    download_manager.cpp
    download_manager.hpp
    progress_parser.cpp
    progress_parser.hpp

  metadata/
    metadata_service.cpp
    metadata_service.hpp
    metadata_models.hpp

  runtime/
    toolchain_manager.cpp
    toolchain_manager.hpp
    tool_updater.cpp
    tool_updater.hpp
    checksum_parser.cpp
    checksum_parser.hpp
    toolchain_lock.cpp
    toolchain_lock.hpp

  storage/
    queue_store.cpp
    queue_store.hpp
    history_repository.cpp
    history_repository.hpp

  ui/
    main_window.cpp
    main_window.hpp
```

Do not force this exact directory split in one commit if a safer incremental extraction works better. Final responsibilities are more important than directory aesthetics.

## Core state model

Replace `bool running` / `bool completed` as the primary source of truth with a single explicit state.

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

A task should also include a terminal reason separate from its state.

```cpp
struct TaskFailure {
    QString userMessage;
    QString technicalMessage;
    int processExitCode = 0;
};
```

A cancelled task must never transition to `Failed` just because `QProcess::kill()` produces a non-zero exit code.

## DownloadTask

`DownloadTask` owns pure task state and serializable task data, not UI widgets.

Suggested fields:

```cpp
struct DownloadTaskData {
    QString id;
    QString url;
    QString title;
    QString outputDirectory;
    QString outputPath;
    QString formatKey;
    QString formatSelector;
    QString extension;
    TaskState state = TaskState::Queued;
    double progressPercent = 0.0;
    QString speed;
    QString eta;
    QString errorMessage;
    QDateTime createdAt;
    QDateTime updatedAt;
};
```

UI widget pointers must not live in this model.

## DownloadManager

`DownloadManager` is responsible for:

- pending order;
- maximum parallelism;
- process creation;
- process signal wiring;
- cancellation;
- exactly-once finalization;
- retry entry point;
- queue status signals;
- persistence notifications.

It should expose Qt signals such as:

```cpp
signals:
    void taskAdded(const QString& taskId);
    void taskChanged(const QString& taskId);
    void taskRemoved(const QString& taskId);
    void queueChanged(int running, int queued);
```

The manager must make it impossible to decrement `runningCount` twice for the same task.

Use a finalization guard or state transition check.

## Process lifecycle rules

### Start

A task moves:

`Queued -> Downloading`

only after the process has actually started successfully.

Use `QProcess::started` and `QProcess::errorOccurred` rather than assuming `start()` succeeded.

### Normal completion

`Downloading/PostProcessing -> Completed`

only when the process exits successfully and expected output handling succeeds.

### Cancellation

On user cancellation:

1. set a cancellation intent/state before killing/terminating;
2. remove queued tasks immediately without launching them;
3. for running tasks prefer graceful `terminate()` first;
4. fall back to `kill()` after a short timeout;
5. finalize as `Cancelled`, never `Failed`;
6. preserve already-existing user files unless the application can prove they are only its own temporary partial files.

### Failed start

`Queued -> Failed`

with a friendly message if `yt-dlp` cannot be started.

The parallel slot must be released exactly once.

## ProgressParser

Move stdout line parsing out of `MainWindow`.

The parser must understand at minimum:

```text
download:<percent>|<speed>|<eta>|<downloaded_bytes>|<total_bytes>
vdppath:<final path>
```

Make it a pure component where possible so it can be table-tested without spawning yt-dlp.

Malformed progress lines must not crash and must not reset valid existing progress.

## MetadataService

Responsibilities:

- bounded concurrency, default max 3 metadata processes;
- URL -> metadata cache for the current session;
- deduplicate concurrent requests for the same URL;
- parse `--dump-single-json` output;
- expose thumbnail and dynamic format information;
- return failures without blocking the download manager.

Suggested API:

```cpp
class MetadataService final : public QObject {
    Q_OBJECT
public:
    explicit MetadataService(const ToolchainManager* toolchain, QObject* parent = nullptr);
    void request(const QString& requestId, const QString& url);

signals:
    void ready(const QString& requestId, const VideoMetadata& metadata);
    void failed(const QString& requestId, const QString& message);
};
```

## Format model

Keep simple presets and add advanced options.

Suggested metadata models:

```cpp
struct FormatOption {
    QString id;
    QString label;
    QString selector;
    QString extension;
    QString videoCodec;
    QString audioCodec;
    int height = 0;
    double fps = 0.0;
    qint64 estimatedBytes = -1;
    bool audioOnly = false;
};

struct VideoMetadata {
    QString title;
    QString uploader;
    int durationSeconds = 0;
    QString thumbnailUrl;
    QVector<FormatOption> formats;
};
```

Never expose hundreds of raw yt-dlp rows directly. Normalize/deduplicate into useful choices.

## Runtime architecture

### ToolchainManager

Keep responsibilities for paths/status/installed versions.

### ToolUpdater

Move network/update behaviour into an asynchronous QObject service.

Requirements:

- one long-lived `QNetworkAccessManager`;
- stream response chunks directly to `QSaveFile` or a staged temp file;
- configurable request timeout;
- redirect handling using Qt defaults appropriate for HTTPS;
- progress signal;
- cancellation;
- checksum validation before install;
- version validation after install where possible;
- rollback if post-install validation fails;
- serialize updates for the same tool.

Never call a nested `QEventLoop::exec()` for update/download operations triggered from the main UI.

## ChecksumParser

Implement a pure parser that can select the expected digest for an explicit filename.

Support common forms:

```text
<sha256>  filename
<sha256> *filename
SHA256 (filename) = <sha256>
Hash: <sha256>
```

The generic `Hash:` form is allowed only when the checksum resource is known to refer to exactly one artifact.

The parser API must include the artifact filename when parsing a multi-entry checksum document.

Example:

```cpp
std::optional<QByteArray> findSha256(
    const QString& checksumText,
    const QString& expectedFileName,
    bool singleArtifactDocument);
```

## Queue persistence

Add an atomic queue store in `data/queue.json`.

Persist non-terminal tasks and minimal task metadata.

On startup:

- `Queued` -> `Queued`;
- previous `Preparing`, `Downloading`, `PostProcessing` -> `Queued` with a recovery marker;
- `Completed`, `Cancelled`, `Failed` belong in history and should not be restored as pending queue items.

Use `QSaveFile` for atomic writes.

Schema must include `schema` integer.

## HistoryRepository

For 5.0, JSON is acceptable if moved behind a repository interface and writes remain atomic.

Do not force SQLite solely for architectural purity. SQLite may be introduced later if history querying/features justify Qt Sql dependency.

Repository interface should support:

- append;
- list newest first;
- text search;
- delete one record;
- clear history;
- re-download using stored URL/format.

## UI ownership

`MainWindow`/download page may own widgets, but it should react to model/service signals.

A UI task card should map to a task id and pull display state from `DownloadManager`.

Clearing a finished card must:

- remove the widget from the layout;
- call `deleteLater()`;
- remove the UI mapping;
- allow the underlying terminal task object to be released when no longer needed.

## Threading

Prefer Qt's asynchronous event-driven APIs before adding worker threads.

Use threads only for truly blocking CPU/filesystem work that cannot be made asynchronous.

Do not move QObject instances across threads casually.

## Backwards compatibility

- Existing settings keys must continue working.
- Existing history JSON must still load.
- Existing runtime directory should be reused.
- Existing users must not need to delete AppData to upgrade.
