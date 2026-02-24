# Video Downloader Pro

Desktop-приложение на `PySide6` + `yt-dlp` для загрузки видео и аудио с YouTube и других платформ, которые поддерживает `yt-dlp`.

![App Screenshot](https://github.com/Jacksony100/Youtube-Downloader/blob/main/screenshot.jpg)

## Возможности

- Очередь загрузок с параллельностью от 1 до 5 задач.
- Форматы: лучшее видео, 1080p, 720p, 480p, MP3.
- Карточки загрузок с прогрессом, скоростью, статусом и превью.
- Отмена отдельной загрузки и массовая отмена всех активных задач.
- Проверка ссылки до скачивания (название, длительность, автор).
- Авто-открытие готового файла по опции.
- Сохранение настроек между перезапусками (папка, формат, параллельность, авто-открытие).
- Блок помощи при сетевых ограничениях + переход на [onyshop.tech](https://onyshop.tech).
- Меню приложения и горячие клавиши для ключевых действий.

## Требования

- Python `3.9+`
- `ffmpeg` обязателен для режима `MP3`

Приложение ищет `ffmpeg` так:

- в системном `PATH`
- в папке с приложением (`ffmpeg.exe` или `ffmpeg`)

## Быстрый старт (из исходников)

```bash
git clone https://github.com/Jacksony100/Youtube-Downloader.git
cd Youtube-Downloader
python3 -m venv .venv
source .venv/bin/activate   # Windows: .venv\Scripts\activate
pip install -r requirements.txt
python3 main.py
```

## Сборка релиза

### macOS

```bash
./scripts/build_release.sh
```

Результат:

- `dist/VideoDownloaderPro-macOS.zip`

### Windows x64 (локально на Windows)

```powershell
./scripts/build_release_windows.ps1
```

Результат:

- `dist\VideoDownloaderPro-win-x64.zip`

Дополнительно:

```powershell
./scripts/build_release_windows.ps1 -UseNuitka
```

Это режим с более сильной защитой/обфускацией (дольше сборка).
### Windows x64 через GitHub Actions

В репозитории есть workflow:

- `.github/workflows/build-windows-x64.yml`

Как запускать:

1. Откройте вкладку `Actions`.
2. Выберите `Build Windows x64`.
3. Нажмите `Run workflow`.
4. При необходимости включите `use_nuitka=true`.
5. Скачайте артефакт сборки.

## Горячие клавиши

- `Ctrl+L` фокус на поле ссылки
- `Ctrl+D` добавить в очередь
- `Ctrl+I` проверить ссылку
- `Ctrl+O` открыть папку загрузок
- `Ctrl+Shift+C` отменить все
- `Ctrl+Shift+X` очистить завершенные
- `Ctrl+Q` выход

## Структура проекта

- `main.py` — приложение (UI, очередь, загрузки, настройки)
- `requirements.txt` — зависимости Python
- `scripts/build_release.sh` — сборка macOS
- `scripts/build_release_windows.ps1` — сборка Windows x64
- `.github/workflows/build-windows-x64.yml` — CI сборка Windows
- `CHANGELOG.md` — журнал изменений по релизам

## Донат

Поддержать проект (USDT TRC20):

- Адрес: `TAa2pm6veN9Jd7X93juoqvoT9WE7QxLKGq`
- Сеть: `TRON (TRC20)`

## Важно

- Поддержка сайтов зависит от актуальности `yt-dlp`.
- Если что-то перестало скачиваться, сначала обновите `yt-dlp`:

```bash
pip install -U yt-dlp
```

## Disclaimer

This software is provided "as is", without warranties of any kind.
You are responsible for compliance with local laws, copyright rules, and platform terms.
