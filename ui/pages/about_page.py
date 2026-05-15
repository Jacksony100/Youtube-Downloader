from PySide6.QtCore import QUrl
from PySide6.QtGui import QDesktopServices
from PySide6.QtWidgets import QFrame, QHBoxLayout, QLabel, QPushButton, QTextBrowser, QVBoxLayout, QWidget

from core import APP_VERSION


ABOUT_CHANGELOG = """
# Changelog

## 3.1.0 - главный экран и быстрый формат

### Главное

- Выбор формата перенесён прямо на главный экран, в блок ввода ссылки.
- Теперь формат выбирается до проверки ссылки и до добавления в очередь.
- Добавление в очередь использует текущий выбор формата с главного экрана.
- Последний выбранный формат сразу сохраняется как формат по умолчанию.
- Превью стало чище: в нём остались сведения о видео, папка сохранения, параллельность и авто-открытие.

### Runtime и запуск

- Статус `yt-dlp`, `ffmpeg` и `ffprobe` читает версии из `manifest.json`.
- Обычный запуск больше не вызывает лишние `yt-dlp --version`, `ffmpeg -version` и `ffprobe -version`.
- На Windows служебные процессы запускаются без всплывающих консольных окон.
- Неудачная авто-проверка обновлений тоже запоминает время проверки, чтобы приложение не пыталось долбить сеть при каждом старте.

### Сборка

- Версия приложения поднята до `3.1.0`.
- User-Agent обновлён до `VideoDownloaderPro/3.1`.
- Windows onefile exe пересобран.
- Добавлен GitHub Actions workflow для macOS-сборки.
- Совместимость PySide6 расширена диапазоном `PySide6>=6.8.3,<6.12`, чтобы сборка работала на актуальном Python.

## 3.0.0 - большой редизайн и managed runtime

### UI/UX

- Полный редизайн в стиле premium utility app.
- Тёмный графитовый интерфейс стал основным визуальным стилем.
- Главное действие выделено зелёным цветом.
- Активная навигация выделяется фиолетовым акцентом.
- Добавлены разделы: `Загрузки`, `История`, `Инструменты`, `Настройки`, `О приложении`.
- Добавлена боковая навигация.
- Добавлены карточки превью видео.
- Добавлены карточки активных и завершённых загрузок.
- Добавлены пустые состояния для очереди.
- Добавлен toast для коротких уведомлений.
- Добавлены статусные chips для `yt-dlp` и `ffmpeg`.
- Вынесена отдельная QSS-тема `ui/styles/dark.qss`.
- Блок помощи при сетевых ограничениях сохранён, но стал менее навязчивым.
- Добавлен быстрый переход на `onyshop.tech`.
- Добавлена подсказка, почему VPN может помочь при сетевых ограничениях.

### Core

- Проект разделён на папки `app`, `core`, `ui`, `tests`.
- `main.py` оставлен как compatibility wrapper.
- Загрузка переведена с прямого `yt_dlp.YoutubeDL` на внешний `yt-dlp` через `QProcess`.
- Добавлен `ToolchainManager`.
- Runtime-инструменты хранятся в AppData пользователя.
- `yt-dlp`, `ffmpeg` и `ffprobe` живут отдельно от exe.
- Добавлен `manifest.json` для runtime-состояния.
- Добавлены безопасные staging-обновления.
- Старый runtime не удаляется до успешной проверки нового.
- Добавлена атомарная замена инструментов после успешной установки.
- Добавлены настройки приложения.
- Добавлена SQLite-история загрузок.
- Добавлена диагностика runtime.
- Добавлены отдельные логи: `app.log`, `toolchain.log`, `downloads.log`.
- Добавлена проверка ссылок отдельным процессом.
- Добавлена обработка сетевых ошибок.
- Улучшена очистка потоков при закрытии приложения.

### Build/Release

- Windows-сборка переведена на `onefile`.
- Основной артефакт сборки: `dist\\VideoDownloaderPro.exe`.
- В exe добавляются fallback `yt-dlp.exe`, `ffmpeg.exe`, `ffprobe.exe`.
- При запуске fallback-инструменты копируются в AppData runtime.
- PowerShell build script готовит fallback-инструменты.
- Для `yt-dlp` проверяется SHA256.
- Сохранён опциональный Nuitka-режим через `-UseNuitka`.

### Tests

- Добавлены тесты путей AppData.
- Добавлены тесты парсера прогресса.
- Добавлены тесты форматных пресетов.
- Добавлены тесты SQLite history.
- Добавлены тесты settings.
- Добавлены тесты кэша runtime-версий, чтобы не возвращались лишние командные проверки.

## 2.5.0 - стабильность и релизная упаковка

### UI/UX

- Переработан рабочий экран.
- Верхняя панель стала компактнее.
- Сетка настроек стала стабильнее.
- Отступы выровнены.
- Обновлён скриншот приложения в репозитории.
- Рабочий экран стал чище и быстрее.

### Core

- Стабилизирована очередь загрузок.
- Улучшена обработка завершения задач.
- Улучшено определение ресурсов в собранных билдах.
- Улучшен поиск иконки.
- Улучшен поиск `ffmpeg`.
- Сохранены ключевые улучшения v2: карточки загрузок, асинхронные превью, отмена задач, автопроверка ссылки.

### Build/Release

- Подготовлен Windows x64 one-file executable.
- Подготовлен пакет macOS для распространения.
- Обновлена release-документация.
- Обновлён changelog.
- Добавлен донат-блок в README.

## 2.0.0 - очередь, карточки и полноценный workflow

### Added

- Добавлена полноценная очередь загрузок.
- Добавлено управление параллельностью от 1 до 5 задач.
- Добавлены карточки задач.
- В карточках отображается превью.
- В карточках отображается прогресс.
- В карточках отображается скорость.
- В карточках отображается статус.
- В карточках добавлены действия.
- Добавлена проверка ссылки до скачивания.
- Проверка ссылки работает в отдельном потоке.
- Добавлена асинхронная загрузка миниатюр.
- Миниатюры не блокируют интерфейс.
- Добавлено меню приложения: `Файл`, `Загрузки`, `Инструменты`, `Справка`.
- Добавлены горячие клавиши.
- Добавлен VPN-инфоблок.
- Добавлены подсказки при сетевых ошибках.
- Добавлен переход на `onyshop.tech`.
- Добавлена защита от дублирования учёта результатов задачи.
- Добавлена настройка авто-открытия скачанного файла.

### Changed

- Интерфейс переработан на секции.
- Добавлена более читаемая сетка элементов.
- Обновлены стили.
- Улучшена читаемость.
- Улучшен поиск ресурсов в runtime для сборок.
- Поддержаны `_MEIPASS`, `Resources`, `Frameworks` и путь рядом с exe.
- Улучшено авто-определение `ffmpeg`.
- Поддержаны системные и bundled бинарники.
- Обновлена логика отмены задач.
- Обновлена логика завершения задач.
- Пользовательские настройки сохраняются в конфиг.

### Fixed

- Исправлены наложения элементов.
- Исправлены проблемы верстки в панели управления.
- Исправлена работа шрифта на macOS.
- Вместо жёстко заданного шрифта используется системный.
- Улучшена обработка закрытия приложения при активных загрузках.
- Исправлены ситуации с неправильным определением финального пути файла после скачивания.

### Build/Release

- Добавлен скрипт сборки macOS.
- Добавлен скрипт сборки Windows x64.
- Добавлен CI workflow для Windows x64.
- Поддержана упаковка релизов с иконкой.
- Поддержан bundled `ffmpeg` для офлайн-использования.
"""


class AboutPage(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        root = QVBoxLayout(self)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(16)

        title = QLabel("Video Downloader Pro")
        title.setObjectName("PageTitle")
        subtitle = QLabel(f"Версия {APP_VERSION}")
        subtitle.setObjectName("PageSubtitle")

        card = QFrame()
        card.setObjectName("Card")
        layout = QVBoxLayout(card)
        layout.setContentsMargins(18, 18, 18, 18)
        layout.setSpacing(12)

        coded = QLabel("Coded by Jacksony")
        coded.setObjectName("SectionTitle")
        body = QLabel(
            "Десктопное приложение на Python + PySide6 для загрузки видео и аудио через управляемый runtime yt-dlp/ffmpeg.\n\n"
            "Соблюдайте законы, авторские права и правила платформ. Приложение не обходит возрастные, региональные или платные ограничения."
        )
        body.setObjectName("MutedText")
        body.setWordWrap(True)

        links = QHBoxLayout()
        links.setSpacing(8)
        github = QPushButton("GitHub")
        github.setObjectName("SecondaryButton")
        telegram = QPushButton("Telegram")
        telegram.setObjectName("SecondaryButton")
        github.clicked.connect(lambda: QDesktopServices.openUrl(QUrl("https://github.com/Jacksony100/Youtube-Downloader")))
        telegram.clicked.connect(lambda: QDesktopServices.openUrl(QUrl("https://t.me/Smesharik_lair")))
        links.addWidget(github)
        links.addWidget(telegram)
        links.addStretch()

        changelog_title = QLabel("Большой changelog")
        changelog_title.setObjectName("SectionTitle")
        changelog = QTextBrowser()
        changelog.setObjectName("ChangelogPanel")
        changelog.setOpenExternalLinks(True)
        changelog.setMarkdown(ABOUT_CHANGELOG.strip())

        layout.addWidget(coded)
        layout.addWidget(body)
        layout.addLayout(links)
        layout.addWidget(changelog_title)
        layout.addWidget(changelog, 1)

        root.addWidget(title)
        root.addWidget(subtitle)
        root.addWidget(card, 1)
