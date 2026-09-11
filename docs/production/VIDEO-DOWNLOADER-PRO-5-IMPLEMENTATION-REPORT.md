# Video Downloader Pro 5 — Implementation report

Release readiness: **READY_WITH_BLOCKERS**

Проверка: 2026-09-11T10:54:44.8964377+03:00 (MSK, UTC+3). Ветка: `codex/video-downloader-pro-5`. HEAD исходной базы: `2dbaf9938aac142b31f16e00aa27899b6f27854e`. Изменения находятся в рабочем дереве; commit, push и GitHub Release не создавались.

P0/P1 реализованы в коде и проходят доступные локальные проверки Windows. Полная межплатформенная готовность не заявляется: macOS packaging/smoke и выполнение удалённого CI ещё не подтверждены. P2 отложен до прохождения этих gates.

## Исходное состояние и сохранность

Первоначальный `git status --short` был пустым; ветка `main` соответствовала `origin/main`. Пользовательских незакоммиченных изменений не было. Существующие игнорируемые каталоги сборок, runtime, скачанные медиа и пользовательские AppData не удалялись. Новые сборки, фикстуры, smoke и кеш размещены в отдельных подпапках.

Прочитаны CLAUDE.md, все шесть `docs/claude/*.md` и план `docs/superpowers/plans/2026-09-11-video-downloader-pro-5.md` из переданного ZIP. Они сохранены как исходные требования. Указания в приложенных документах не использовались для самостоятельного расширения полномочий, публикации или запуска необязательных навыков.

Baseline: CMake/Ninja Release, MSVC 19.44, Qt 6.8.3 — сборка успешна, исходный CTest **1/1 PASS**. CMake/Qt первоначально отсутствовали в PATH, но найдены в локальных установках Qt и Visual Studio 2022; системная установка SDK не потребовалась.

## Реализация плана и фазовые проверки

| Фаза / задачи плана | Реализация | Проверка |
|---|---|---|
| 0 / 1 — baseline | Сохранён baseline, расширены аргументные и error regression tests | baseline 1/1, затем общий suite |
| 1–2 / 2–4 — lifecycle, parser, manager | TaskState, отмена с намерением до остановки процесса, started/errorOccurred, ровно одно завершение, retry, ограниченные слоты, разделённые stdout/stderr буферы, удаление terminal ownership | 5 focused групп download/storage; реальные управляемые дочерние процессы; интеграция 11/11 |
| 3,5 / 5–6 — metadata/formats | Максимум 3 процесса, кеш 128, поколения подписок, отмена/дедупликация, ограничение ответа, разрешение/кодек/FPS/размер, оригинал аудио и MP3, простые пресеты сохранены | 7 focused групп после review; интеграция 13/13 |
| 4 / 7–8 — runtime | QObject updater, один QNAM, streaming 64 КиБ, HTTPS/redirect/timeout/cancel, SHA256 точного артефакта, worker для проверки/установки, rollback пары и manifest, восстановление повреждённого runtime | 4 focused runtime группы PASS, включая heartbeat и подставной HTTPS-транспорт |
| 6 / 9–10 — storage/history | QSaveFile, schema 1, восстановление non-terminal, сохранение повреждённой очереди, чтение старой истории, cap 500, поиск/удаление/повтор/копирование, исправлена миграция MP3 bitrate | temporary-directory тесты, старый JSON, ошибки commit, GUI restart |
| 7 / 11–13 — release/diagnostics | CMake version, PR/push CI, tag-only выпуск обеих платформ, SHA256SUMS, optional signing, bounded logs, redaction и технические ошибки, notices | 13 Python security cases; actionlint; Inno compile и Windows package |
| 8 / 14 — verification/docs | README, CHANGELOG, notices, отчёт и сохранённые evidence | итоговый CTest **15/15**, production build, Windows package и smoke |

Независимые подсистемы разрабатывались параллельно; после их focused проверок выполнялись общие интеграционные suite. Найденные в review дефекты исправлены до итогового результата: повтор во время updater, ожидание pending metadata, отмена кешированного callback, физическое освобождение metadata-слота и старые MP3-записи.

Дополнительно исправлен исходный дефект реального прогресса yt-dlp: `--print` включает quiet, а первый `download:` является типом шаблона. Теперь используются `--progress`, `download:download:...` и `postprocess:postprocess:...`, а менеджер читает оба потока процесса. Это подтверждено настоящими бинарниками на локальном видео, а не только строковыми тестами. Основание: [официальные options yt-dlp](https://github.com/yt-dlp/yt-dlp/blob/master/yt_dlp/options.py) и `yt-dlp --help` проверенной версии.

## Acceptance gates

| Gate | Результат и доказательство |
|---|---|
| 1 — запуск, страницы, настройки, runtime | PASS локально: Qt Widget tests, рендер всех страниц, сохранение настроек; отдельный запуск настоящего packaged runtime |
| 2 — queue lifecycle | PASS: очередь/лимит/queued и active cancel/start failure/crash/retry/output file/удаление карточек; QPointer карточки обнуляется |
| 3 — отзывчивость | PASS автоматизированно: события Qt продолжаются при fake HTTPS transfer и worker-проверке; progress/cancel/timeout; nested QEventLoop отсутствует. Ручное перемещение окна ОС не выдаётся за выполненный тест |
| 4 — secure updater | PASS: точный checksum, wrong/missing digest, HTTPS downgrade, staged version failure, pair rollback, manifest commit failure, повреждённый бинарник не запускается |
| 5 — persistence | PASS: running -> queued/recovered; terminal не восстанавливаются; shutdown до остановки процессов; corrupt/old schemas и атомарные ошибки |
| 6 — formats | PASS: fixture-driven normalization, simple presets, понятные labels, только реальные estimates, GUI advanced selector |
| 7 — packaging Windows | PASS: Release, тесты, portable ZIP, Inno installer, изолированная установка и запуск установленного приложения |
| 7 — packaging macOS | **NOT_RUN / внешний blocker**: текущий хост Windows. Скрипт, pins, лицензии и CI подготовлены; запуск .app, codesign/notarization и macOS smoke требуют macOS |
| 8 — CI | Конфигурация PASS: PR/push сохранены, tag-only release ждёт обе платформы, version/tag match и SHA256; actionlint без ошибок. Удалённые jobs не запускались, поскольку push не разрешён |

Регрессии интерфейса покрыты: вставка/проверка ссылки, presets, расширенный формат, данные preview, безопасная загрузка thumbnail, папка/parallel/auto-open настройки, история и shortcuts. Реальная доступность конкретных внешних видеоплощадок не является детерминированным acceptance gate; авторизованный живой YouTube URL не использовался.

## Команды и итоговые результаты

Среда текущего хоста: `. ./build-cpp/pro5-env.ps1` импортирует обнаруженный MSVC и добавляет Qt bin в PATH. Этот локальный вспомогательный файл находится в игнорируемом build-каталоге.

```powershell
cmake -S . -B build-cpp/pro5 -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$env:Qt6_DIR" -DBUILD_TESTING=ON
cmake --build build-cpp/pro5 --clean-first --parallel 4
ctest --test-dir build-cpp/pro5 --output-on-failure
# 15/15 групп PASS; последний полный запуск 16.84 с

cmake -S . -B build-cpp/pro5-production -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$env:Qt6_DIR" -DBUILD_TESTING=OFF
cmake --build build-cpp/pro5-production --parallel 4
# PASS, отдельная production-сборка без тестовых целей

python -m unittest discover -s scripts -p test_release_tools.py -v
# 13/13 PASS
python scripts/release_tools.py validate-lock

./scripts/build_release_windows.ps1 -QtDir "$env:Qt6_DIR" -Python C:/Python314/python.exe -InnoSetupCompiler "build_tools/inno-6.7.3/compiler-files/{app}/ISCC.exe" -SkipToolDownloads
# EXIT 0: full tests, real engine, portable, installer, installed smoke
```

Локализованный MSVC имеет некорректно распознанный CMake/Ninja prefix `showIncludes`; поэтому основные итоговые сборки выполнены с очисткой объектов, а Windows release-скрипт использует `--clean-first`. Production-каталог создан заново. Последняя содержательная правка приложения — текст диагностики в `main_window.cpp` — вошла в production и packaged бинарники. После упаковки в `core.cpp` удалена только лишняя пустая строка в конце файла; также обновлены документация и GUI-тесты. Затем полный suite и production build запущены повторно. Изменения заголовков не проверялись на устаревших объектах.

Полные локальные логи: `build-cpp/pro5-evidence/`. Переносимые краткие доказательства: [CTest](evidence/final-ctest.log), [production build](evidence/production-build.log), [release security](evidence/release-security-tests.log), [real engine](evidence/real-engine-smoke.json), [packaging](evidence/windows-packaging-summary.log), [машиночитаемый результат](evidence/validation.json).

## Артефакты Windows

| Артефакт | Байт | SHA256 |
|---|---:|---|
| `dist/VideoDownloaderPro-5.0.0-win-x64.zip` | 196,700,191 | `e1708f24233a77cacec51dd1d1cea3f7370a060a3352cdd3a907c9f5c607dacd` |
| `dist/VideoDownloaderPro-Setup-5.0.0.exe` | 146,599,921 | `5859fd5ba32ae49f60569b528c4aa06e85945d73518c7ca54f2411c6c8b59583` |

Контрольные суммы также находятся в `dist/SHA256SUMS-windows.txt`. В ZIP проверены Qt platform plugin, приложение, app-local MSVC CRT и runtime manifest. Smoke очищает PATH от developer SDK и не использует пользовательские AppData. Результат не выдаётся за тест отдельной чистой VM.

Runtime при подготовке: yt-dlp **2026.08.19**, Deno **2.9.6**, FFmpeg/ffprobe **9.0.1**. Настоящий engine smoke создал собственное 2-секундное H.264/AAC видео, скачал его по loopback HTTP, извлёк MP3 и подтвердил codec/streams/итоговый путь/progress/postprocess через производственные аргументы и ffprobe.

Официальный Inno Setup 6.7.3 не устанавливался в систему: автоматическая проверка запретила запуск его инсталлятора; подписанный дистрибутив проверен и compiler извлечён локально. Это препятствие устранено, итоговый installer compile/install smoke успешен.

## Изменённые файлы

- `src/core.*`, `src/main_window.*`: совместимые аргументы/пути, асинхронная интеграция и UI; MainWindow больше не владеет процессами загрузки или счётчиками очереди.
- `src/downloads/*`: модель, lifecycle, manager и чистый progress parser.
- `src/metadata/*`: очередь запросов, cache, typed metadata и formats.
- `src/runtime/*`, `runtime/toolchain-lock.json`: updater, service, parser, transactional installer, описания целостности.
- `src/storage/*`: queue/history и совместимая сериализация.
- `src/diagnostics/*`: redaction и ограниченные журналы.
- `tests_cpp/*`, `scripts/test_release_tools.py`, `scripts/engine_smoke_arguments.cpp`: тесты и реальная интеграция движка.
- `CMakeLists.txt`, `scripts/build_release*`, `scripts/release_tools.py`, `installer/VideoDownloaderPro.iss`, три CI workflow: версия, сборка, упаковка и выпуск.
- `README.md`, `CHANGELOG.md`, `THIRD_PARTY_NOTICES.md`, `licenses/*`, этот отчёт и evidence. Полный список — в `evidence/validation.json`.

Проверка незавершённых реализаций: нет маркеров незавершённой работы в src/scripts/tests/runtime и поддерживаемой документации выпуска; `git diff --check` проходит. Входные спецификации и сторонние лицензии сохраняются как документы-источники, а не изменяются ради текстового сканера. Тестовые фикстуры, ожидаемые ошибки checksum и текст подсказки поля ввода не являются подменой реализации.

## Оставшиеся внешние подтверждения

1. Выполнить macOS build/package/smoke на macOS и подтвердить CI после отдельно разрешённого push. Проверенные Evermeet FFmpeg pins относятся к Intel; Apple Silicon требует Rosetta для этих бинарников.
2. Публичный выпуск требует решения владельца по лицензии проекта и проверке обязательств corresponding source/notice для точных сторонних сборок, описанных в THIRD_PARTY_NOTICES. Полное юридическое соответствие не заявлялось.
3. Подпись Windows и production Developer ID/notarization macOS опционально поддержаны; секреты не предоставлены. Windows-артефакты этой проверки неподписанные.

**Deferred P2:** плейлисты/выбор batch, субтитры, главы/встраивание metadata, дополнительные конвертации M4A/Opus, диапазоны времени, opt-in browser cookies, proxy UI, автоматическое update-and-retry, расширенная история с thumbnail/размером и самообновление приложения. P1 original audio/MP3 options реализованы по основному feature spec. P2 не расширялся, пока межплатформенные P0/P1 acceptance остаются неподтверждёнными.
