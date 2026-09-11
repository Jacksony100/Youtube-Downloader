# Video Downloader Pro 5 — Security, Runtime Integrity & Release Engineering

## Threat model

The application downloads and executes third-party binaries. The updater therefore acts as a software supply-chain boundary.

Treat these as security-sensitive:

- URLs used to obtain yt-dlp, Deno, FFmpeg, ffprobe;
- checksum documents;
- archive extraction;
- staged binary replacement;
- tool paths;
- diagnostics and logs;
- release artefacts.

## Runtime update requirements

### 1. HTTPS only

All built-in runtime download URLs must be HTTPS.

Reject accidental downgrade to plain HTTP.

### 2. Stream to staging file

Do not use `reply->readAll()` for large tool archives.

Write `readyRead` chunks into a staged file.

Only commit/install after:

1. request completes successfully;
2. file is non-empty;
3. expected SHA256 matches;
4. archive extraction succeeds if required;
5. expected executable is found;
6. executable/version smoke check succeeds where practical.

### 3. Exact checksum selection

For multi-entry checksum files, match both hash and exact basename.

Never accept the first hash in a checksum document without confirming it belongs to the requested artifact.

Add tests for:

```text
aaaa...  wrong-file.exe
bbbb...  yt-dlp.exe
```

The parser must return `bbbb...` for `yt-dlp.exe`.

### 4. FFmpeg pinning strategy

The current FFmpeg path needs integrity validation.

Use this order of preference:

1. upstream/distributor-provided SHA256 for the exact downloaded archive;
2. if a stable machine-readable checksum is unavailable, store the reviewed archive SHA256 in a repository-controlled `toolchain-lock.json` together with source URL and expected file name;
3. update the pin intentionally when refreshing the packaged runtime.

Never silently download an unverified FFmpeg archive and immediately execute its contents.

Suggested lock schema:

```json
{
  "schema": 1,
  "tools": {
    "ffmpeg-windows-x64": {
      "url": "https://.../ffmpeg-release-essentials.zip",
      "sha256": "<64 lowercase hex>",
      "archive": true,
      "ffmpeg": "ffmpeg.exe",
      "ffprobe": "ffprobe.exe"
    }
  }
}
```

Do not commit placeholder hashes. The implementation must obtain and pin a real digest during the runtime refresh process.

### 5. Atomic install and rollback

Installation algorithm:

1. download to temp;
2. verify temp;
3. extract to temp if needed;
4. validate executable(s);
5. rename current executable to `.bak` only immediately before replacement;
6. install new file;
7. run version/smoke check;
8. if check fails, restore `.bak`;
9. remove backup only after successful validation.

For FFmpeg + ffprobe, treat the pair as one transaction. Do not leave a new ffmpeg with an old ffprobe if the second replacement fails.

### 6. Archive extraction safety

When possible, avoid extracting arbitrary paths outside the intended temp directory.

If the current platform extraction tool does not provide strong path control, extract only into a fresh temporary directory and locate expected executable basenames afterward.

Never extract updates directly into the active runtime directory.

## Diagnostics and privacy

Add logs only if they improve supportability.

Recommended files:

```text
logs/app.log
logs/downloads.log
logs/runtime.log
```

Log rotation/bounds are required. Example:

- max 2–5 MB per file;
- keep a small number of rotated files.

Never log:

- browser cookies;
- `Cookie` headers;
- authorization tokens;
- proxy passwords;
- private environment secrets.

Add `Скопировать диагностику` producing a redacted text report containing:

- app version;
- OS/architecture;
- Qt version;
- tool versions;
- tool paths;
- last sanitized error;
- last technical error with secrets redacted;
- relevant feature flags.

## Dependency / license notices

Add a project license if the repository owner has chosen one. Do not invent licensing terms on the owner's behalf.

Regardless of project license, create a `THIRD_PARTY_NOTICES.md` or bundled notices directory documenting the redistributed tools and linking/copying their applicable license notices as required.

Do not assert license compliance without checking the actual versions being redistributed.

## Version source of truth

Use CMake project version as the canonical app version.

Avoid hard-coded copies in:

- PowerShell release script;
- shell release script;
- installer output name;
- GitHub workflow artifact path;
- user agent strings;
- README installation filename.

Generate/pass the version from CMake or a single version file.

At minimum expose a command/script that prints the canonical version for packaging scripts.

## CI

Current CI should be extended so both platform workflows run on:

```yaml
on:
  push:
    branches: [main]
  pull_request:
    branches: [main]
  workflow_dispatch:
```

Add or retain these gates:

- configure;
- compile;
- unit tests;
- packaged smoke test where platform supports it;
- package artifact existence;
- no placeholder checksum values in lock manifest.

Consider separate lightweight static analysis workflow:

- clang-format check;
- clang-tidy on supported runner/toolchain.

Do not make CI depend on live YouTube availability for every PR. External-site smoke tests are flaky and should be manual/nightly if added.

## Release pipeline

Add a tag-driven workflow for tags matching `v*` only after normal CI is green.

Release workflow should:

1. build Windows package;
2. build macOS package;
3. run tests/smoke tests;
4. generate SHA256 for produced distributables;
5. create/upload release artifacts;
6. attach both Windows and macOS outputs when successful;
7. attach checksum file;
8. fail release publication if one mandatory platform failed.

Do not automatically publish releases from ordinary `main` pushes.

## Signing

### Windows

Support Authenticode signing when signing credentials are configured in GitHub Actions secrets.

The build must still work unsigned for local/open-source contributors.

### macOS

Current ad-hoc signing is acceptable for local CI smoke testing but is not a production trust chain.

Provide optional production path for:

- Developer ID Application signing;
- hardened runtime where compatible;
- notarization;
- stapling.

Keep credentials/secrets outside repository.
