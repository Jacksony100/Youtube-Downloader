# Video Downloader Pro 5 — Feature Specification

## 1. Download lifecycle UX

Each task must visibly occupy one of these states:

| State | Russian UI label |
|---|---|
| Queued | В очереди |
| Preparing | Подготовка... |
| Downloading | Загрузка... |
| PostProcessing | Обработка... |
| Completed | Загрузка завершена |
| Cancelled | Отменено |
| Failed | Ошибка |

### Required actions

Queued/running:

- `Отменить`

Completed:

- `Открыть`
- optional secondary `Показать в папке`
- `Убрать карточку`

Failed:

- `Повторить`
- `Убрать карточку`

Cancelled:

- `Повторить`
- `Убрать карточку`

Retry creates a fresh process lifecycle while retaining URL/format/output directory.

## 2. Simple and advanced quality selection

Keep current presets:

- Лучшее
- 1080p
- 720p
- 480p
- MP3

After metadata is available, provide an advanced selector or grouped menu without making it mandatory.

At minimum normalize useful choices into:

### Video

- best available;
- 2160p if available;
- 1440p if available;
- 1080p;
- 720p;
- 480p;
- prefer combined audio/video output after FFmpeg merge.

Where multiple codecs exist, advanced mode may distinguish:

- H.264 / AVC;
- VP9;
- AV1.

### Audio

- Original / best audio;
- MP3 320 kbps;
- MP3 192 kbps;
- M4A/AAC if source/toolchain supports it cleanly;
- Opus if source/toolchain supports it cleanly.

Do not promise exact bitrate when yt-dlp/FFmpeg cannot guarantee it.

## 3. Estimated size

If yt-dlp metadata exposes `filesize` or `filesize_approx`, show an approximate size for advanced format entries.

Use language such as `~210 МБ`.

Do not fabricate estimates when data is absent.

## 4. Metadata preview

Preserve current preview and improve it with:

- title;
- uploader/channel;
- duration;
- thumbnail;
- source hostname/platform label;
- available top resolution when easily derivable.

Metadata failure must not automatically forbid a download if the URL is syntactically valid and runtime is ready; allow a user to attempt the download with a simple preset.

## 5. Bounded metadata processing

If a user adds 100 URLs, do not spawn 100 simultaneous metadata processes.

Default maximum active metadata processes: `3`.

Requests beyond the limit queue internally.

Repeated request for the same URL during one session should reuse a cached result unless the caller explicitly asks to refresh.

## 6. Persistent queue

Store unfinished queue tasks in:

```text
%LOCALAPPDATA%/VideoDownloaderPro/data/queue.json
```

or platform-equivalent `AppPaths::dataDir`.

On restart, if recovered tasks exist, show a non-blocking message:

`Восстановлено задач: N`

Do not immediately start recovered downloads before the main UI has loaded. Restore them to queued state and let the normal queue manager start them after initialization.

## 7. History improvements

Preserve existing history migration.

Every history row should retain at least:

- task id;
- title;
- URL;
- output path;
- format label/key;
- final state;
- user-facing error if any;
- UTC creation/completion time.

Add context actions where practical:

- Открыть файл;
- Показать в папке;
- Скачать снова;
- Скопировать ссылку;
- Удалить запись.

## 8. Runtime tools UI

Tool page should show for each tool:

- installed/not installed;
- version;
- filesystem path;
- integrity status when known;
- update action state/progress.

During an update:

- buttons for conflicting actions are disabled;
- progress is shown;
- UI stays responsive;
- user can cancel a network download before installation begins;
- after completion status refreshes automatically.

## 9. Friendly error model

Maintain and extend `sanitizeError`, but do not use string matching as the only internal error model.

Separate:

- technical diagnostic;
- user-visible message;
- error category.

Suggested categories:

```cpp
enum class DownloadErrorCategory {
    None,
    RuntimeMissing,
    ProcessStartFailed,
    Network,
    RateLimited,
    Forbidden,
    PrivateOrAuthRequired,
    Unavailable,
    GeoOrAgeRestricted,
    Disk,
    Cancelled,
    Unknown
};
```

Do not claim a VPN will fix every 403. Suggest updating yt-dlp first, then network/proxy troubleshooting where appropriate.

## 10. Playlist/batch — P2

Implement only after P0/P1 acceptance passes.

When a playlist URL is detected:

- do not silently download every item;
- fetch playlist entries first;
- show count/title list;
- allow select all / none / specific items;
- enqueue selected entries as independent tasks;
- keep global parallelism limit.

## 11. Subtitles — P2

Optional task settings:

- download subtitles;
- language selection;
- auto-generated subtitles toggle;
- embed into compatible containers where supported.

Default remains off.

## 12. Clip/time-range — P2

Allow optional start/end times.

Validate:

- start >= 0;
- end > start;
- end <= duration if duration is known.

Use yt-dlp-supported section download mechanisms rather than implementing a second downloader.

## 13. Cookies — P2 and opt-in only

If implemented, present a clearly opt-in setting to use cookies from a locally installed browser.

Never copy cookies into logs or persistent app configuration.

The setting must be off by default.

The app must not claim cookies can bypass paid content, DRM, or account permissions.

## 14. Proxy — P2

Allow optional HTTP/SOCKS proxy configuration compatible with yt-dlp.

Keep proxy credentials out of diagnostics/logs.

## 15. Accessibility / UX details

- buttons must have readable text, not icon-only critical actions;
- keyboard shortcuts already present should continue working;
- avoid modal dialogs for routine success messages;
- reserve modal warnings for actions requiring immediate user acknowledgement;
- all progress/status changes must be understandable without relying solely on colour.
