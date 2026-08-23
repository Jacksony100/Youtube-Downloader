# Video Downloader Pro 4

Нативное desktop-приложение на **C++20 + Qt 6 Widgets** для загрузки видео и аудио через внешний `yt-dlp`.

Python не требуется ни пользователю, ни приложению во время работы. Сборка выполняется CMake/Ninja, загрузки запускаются через `QProcess`.

## Возможности

- Очередь с 1–5 параллельными загрузками.
- Форматы: лучшее качество, 1080p, 720p, 480p и MP3.
- Проверка ссылки и чтение метаданных перед загрузкой.
- Нативные настройки и JSON-история загрузок.
- Управляемый runtime в `%LOCALAPPDATA%\VideoDownloaderPro\runtime`.
- Отдельное обновление `yt-dlp`, Deno и ffmpeg.
- Явная передача Deno через `--js-runtimes` для полной поддержки YouTube.
- Windows x64 и macOS release workflows.

## Runtime

```text
VideoDownloaderPro/
  runtime/
    yt-dlp/yt-dlp.exe
    deno/deno.exe
    ffmpeg/bin/ffmpeg.exe
    ffmpeg/bin/ffprobe.exe
  data/
    settings.ini
    history.json
  runtime/manifest.json
```

Релизный архив уже содержит fallback-копии всех инструментов. При первом запуске приложение копирует их в пользовательский runtime. Обновления устанавливаются отдельно от основного приложения.

## Сборка из исходников

Требования:

- CMake 3.21+
- Ninja
- C++20 compiler: MSVC 2022, AppleClang или GCC
- Qt 6.6+ (`Core`, `Widgets`, `Network`, `Test`)

```powershell
cmake -S . -B build-cpp -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_PREFIX_PATH="$env:Qt6_DIR" `
  -DBUILD_TESTING=ON
cmake --build build-cpp
ctest --test-dir build-cpp --output-on-failure
```

## Windows-релиз

```powershell
./scripts/build_release_windows.ps1 -QtDir "$env:Qt6_DIR"
```

Результат: `dist/VideoDownloaderPro-win-x64.zip`.

Скрипт скачивает и проверяет `yt-dlp` и Deno, подготавливает ffmpeg, собирает C++ приложение, запускает CTest и выполняет `windeployqt`.

## macOS-релиз

```bash
Qt6_DIR=/path/to/Qt/lib/cmake/Qt6 ./scripts/build_release.sh
```

Результат: `dist/VideoDownloaderPro-macOS.zip`.

## Структура

```text
src/
  main.cpp              # точка входа
  main_window.*         # Qt Widgets UI, очередь и процессы
  core.*                # форматы, пути, ошибки и managed toolchain
tests_cpp/
  test_core.cpp         # Qt Test / CTest
scripts/
  build_release_windows.ps1
  build_release.sh
```

## Важно

- Поддержка платформ зависит от актуальности `yt-dlp`.
- Deno 2.3+ требуется для полноценной поддержки YouTube.
- Приложение не обходит DRM, платные, возрастные, аккаунтные или региональные ограничения.

## Disclaimer

This software is provided "as is". You are responsible for compliance with local laws, copyright rules and platform terms.
