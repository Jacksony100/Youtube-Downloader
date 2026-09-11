# Claude Code — Video Downloader Pro 5

## Mission

You are working in the repository `Jacksony100/Youtube-Downloader`.

Current baseline is Video Downloader Pro `4.0.2`, a native C++20 / Qt 6 Widgets desktop application with:

- `yt-dlp` managed runtime;
- Deno runtime;
- FFmpeg / ffprobe;
- download queue with configurable parallelism;
- metadata preview and thumbnails;
- download history;
- Windows Inno Setup packaging;
- macOS packaging;
- CMake + Ninja;
- Qt Test / CTest;
- GitHub Actions for Windows and macOS.

Your job is **not** to redesign the project from scratch and **not** to replace C++/Qt with Electron, Python, Tauri, webview, or another stack.

Your job is to evolve the existing codebase into a reliable **Video Downloader Pro 5.x** while preserving the current product identity and existing working behaviour.

## Required reading order

Before changing code, read these files in this exact order:

1. `docs/claude/00-MISSION.md`
2. `docs/claude/01-ARCHITECTURE.md`
3. `docs/claude/02-FEATURE-SPEC.md`
4. `docs/claude/03-SECURITY-RELEASE.md`
5. `docs/claude/04-TESTING-ACCEPTANCE.md`
6. `docs/claude/05-EXECUTION-CHECKLIST.md`
7. `docs/superpowers/plans/2026-09-11-video-downloader-pro-5.md`

Then inspect the current repository yourself. Treat the repository as source of truth if file names or line numbers have changed since these documents were generated.

## Non-negotiable rules

- Preserve C++20 + Qt 6 Widgets.
- Keep CMake as the build system.
- Keep `yt-dlp` as the download engine.
- Keep Deno explicit via `--js-runtimes` where required.
- Keep FFmpeg / ffprobe as managed runtime tools.
- Do not weaken checksum verification.
- Do not introduce DRM circumvention, paywall bypass, credential theft, or authorization bypass functionality.
- Browser-cookie support, if implemented, may only use cookies the local user already has access to and must be opt-in.
- Never log cookies, authorization headers, tokens, or other secrets.
- Do not silently delete the user's downloaded files.
- Do not delete or rewrite unrelated local changes.
- Do not publish a GitHub Release or push to remote unless explicitly asked by the repository owner.
- Do not stop after writing plans. Implement the code.
- No `TODO`, `TBD`, fake implementations, placeholder methods, or commented-out pseudo-solutions in the finished result.
- Avoid adding dependencies unless they materially improve correctness or maintainability.
- User-visible application copy should remain Russian unless a string is intentionally language-neutral.

## Engineering mode

Work incrementally and test-first where practical:

1. Inspect baseline.
2. Run the existing build/tests before modifications.
3. Make one coherent change at a time.
4. Add or update tests with the change.
5. Run the focused tests.
6. Run the complete test suite after each phase.
7. Keep the application buildable after every completed phase.

If git is available, create a dedicated local branch such as:

```bash
git switch -c claude/video-downloader-pro-5
```

Do not create the branch if doing so would conflict with the user's current workflow or dirty worktree. Never discard uncommitted user work.

## Definition of done

The work is complete only when:

- cancellation is represented as cancellation, not failure;
- process startup/crash failures cannot corrupt queue counters;
- runtime/network updates do not block the UI thread;
- runtime downloads are streamed to disk instead of buffered fully in RAM;
- checksum parsing validates the intended artifact;
- FFmpeg update/install integrity is covered by a defensible pin/checksum strategy;
- task lifecycle and queue orchestration are separated from `MainWindow`;
- metadata fetching is bounded and cached;
- dynamic format information is exposed to users without removing simple presets;
- unfinished queue items can survive an app restart;
- completed task cards can actually be released from memory;
- tests cover the core state machine and security-sensitive parsers;
- CI validates pull requests as well as pushes;
- release version duplication is reduced to one source of truth;
- Windows/macOS release documentation and packaging are internally consistent;
- all acceptance gates in `docs/claude/04-TESTING-ACCEPTANCE.md` pass.

When finished, create a concise implementation report in:

`docs/production/VIDEO-DOWNLOADER-PRO-5-IMPLEMENTATION-REPORT.md`

It must include changed files, completed gates, commands run, test results, remaining external blockers, and any intentionally deferred P2 improvements.
