import json
import os
import shutil
import sys
import urllib.request
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Dict, List, Optional
from urllib.parse import urlparse

import yt_dlp
from yt_dlp.utils import DownloadError

from PySide6.QtCore import QEasingCurve, QPropertyAnimation, QThread, QTimer, Qt, QUrl, Signal
from PySide6.QtGui import QAction, QDesktopServices, QFont, QFontDatabase, QIcon, QPixmap
from PySide6.QtWidgets import (
    QApplication,
    QCheckBox,
    QComboBox,
    QFileDialog,
    QFrame,
    QGridLayout,
    QGraphicsOpacityEffect,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QMainWindow,
    QMessageBox,
    QPushButton,
    QProgressBar,
    QScrollArea,
    QSizePolicy,
    QSpinBox,
    QVBoxLayout,
    QWidget,
)

APP_TITLE = "Video Downloader Pro"
CONFIG_FILE = Path.home() / ".video_downloader_config.json"
PREVIEW_TASK_ID = "__preview__"
ONYSHOP_URL = "https://onyshop.tech"

FORMAT_PRESETS = [
    {
        "label": "🎥 Видео — Лучшее качество",
        "format": "bestvideo+bestaudio/best",
        "extract_audio": False,
    },
    {
        "label": "🎥 Видео — 1080p",
        "format": "bestvideo[height<=1080]+bestaudio/best[height<=1080]/best[height<=1080]",
        "extract_audio": False,
    },
    {
        "label": "🎥 Видео — 720p",
        "format": "bestvideo[height<=720]+bestaudio/best[height<=720]/best[height<=720]",
        "extract_audio": False,
    },
    {
        "label": "🎥 Видео — 480p",
        "format": "bestvideo[height<=480]+bestaudio/best[height<=480]/best[height<=480]",
        "extract_audio": False,
    },
    {
        "label": "🎵 Только аудио (MP3)",
        "format": "bestaudio/best",
        "extract_audio": True,
    },
]


def format_duration(seconds: Optional[int]) -> str:
    if not isinstance(seconds, (int, float)) or seconds <= 0:
        return ""
    total = int(seconds)
    hours, rem = divmod(total, 3600)
    minutes, secs = divmod(rem, 60)
    if hours:
        return f"{hours:d}:{minutes:02d}:{secs:02d}"
    return f"{minutes:02d}:{secs:02d}"


def is_http_url(url: str) -> bool:
    try:
        parsed = urlparse(url)
        return parsed.scheme in {"http", "https"} and bool(parsed.netloc)
    except Exception:
        return False


class ThumbnailThread(QThread):
    thumbnail_loaded = Signal(bytes)

    def __init__(self, thumbnail_url: str):
        super().__init__()
        self.thumbnail_url = thumbnail_url

    def run(self):
        try:
            with urllib.request.urlopen(self.thumbnail_url, timeout=8) as response:
                self.thumbnail_loaded.emit(response.read())
        except Exception:
            # Ошибки миниатюры не критичны
            pass


class InfoThread(QThread):
    info_ready = Signal(str, dict)
    error = Signal(str, str)

    def __init__(self, task_id: str, url: str):
        super().__init__()
        self.task_id = task_id
        self.url = url

    def run(self):
        try:
            opts = {
                "quiet": True,
                "no_warnings": True,
                "skip_download": True,
                "noplaylist": True,
                "extract_flat": False,
            }
            with yt_dlp.YoutubeDL(opts) as ydl:
                info = ydl.extract_info(self.url, download=False)
                self.info_ready.emit(self.task_id, info)
        except Exception as exc:
            self.error.emit(self.task_id, str(exc))


class DownloadThread(QThread):
    progress = Signal(str, float, str, float)
    info_ready = Signal(str, dict)
    finished = Signal(str, str, bool, str)

    def __init__(
        self,
        task_id: str,
        url: str,
        fmt: str,
        out_dir: str,
        extract_audio: bool,
        ffmpeg_location: str,
    ):
        super().__init__()
        self.task_id = task_id
        self.url = url
        self.fmt = fmt
        self.out_dir = out_dir
        self.extract_audio = extract_audio
        self.ffmpeg_location = ffmpeg_location
        self._cancelled = False

    def cancel(self):
        self._cancelled = True

    def run(self):
        try:
            opts = {
                "outtmpl": os.path.join(self.out_dir, "%(title).180B.%(ext)s"),
                "noplaylist": True,
                "quiet": True,
                "no_warnings": True,
                "format": self.fmt,
                "continuedl": True,
                "retries": 10,
                "fragment_retries": 10,
                "concurrent_fragment_downloads": 3,
                "progress_hooks": [self._progress_hook],
            }

            if self.ffmpeg_location:
                opts["ffmpeg_location"] = self.ffmpeg_location

            if self.extract_audio:
                opts.update(
                    {
                        "format": "bestaudio/best",
                        "postprocessors": [
                            {
                                "key": "FFmpegExtractAudio",
                                "preferredcodec": "mp3",
                                "preferredquality": "192",
                            }
                        ],
                    }
                )
            else:
                opts["merge_output_format"] = "mp4"

            with yt_dlp.YoutubeDL(opts) as ydl:
                info = ydl.extract_info(self.url, download=False)
                self.info_ready.emit(self.task_id, info)

                if self._cancelled:
                    raise DownloadError("Cancelled by user")

                result = ydl.extract_info(self.url, download=True)
                normalized = self._normalize_result(result)
                output_path = self._resolve_output_path(ydl, normalized)

            if self._cancelled:
                self.finished.emit(self.task_id, "⛔ Загрузка отменена", False, "")
            else:
                self.finished.emit(self.task_id, "✅ Загрузка завершена", True, output_path)

        except DownloadError as exc:
            msg = str(exc)
            if "Cancelled by user" in msg:
                self.finished.emit(self.task_id, "⛔ Загрузка отменена", False, "")
            else:
                self.finished.emit(self.task_id, f"❌ Ошибка загрузки: {msg}", False, "")
        except Exception as exc:
            self.finished.emit(self.task_id, f"❌ Ошибка: {exc}", False, "")

    def _normalize_result(self, result):
        if isinstance(result, dict) and result.get("_type") in {"playlist", "multi_video"}:
            entries = [entry for entry in (result.get("entries") or []) if entry]
            if entries:
                return entries[0]
        return result

    def _resolve_output_path(self, ydl: yt_dlp.YoutubeDL, info: dict) -> str:
        candidates: List[str] = []

        requested_downloads = info.get("requested_downloads") or []
        for item in requested_downloads:
            filepath = item.get("filepath")
            if filepath:
                candidates.append(filepath)

        if info.get("_filename"):
            candidates.append(info["_filename"])

        prepared = ydl.prepare_filename(info)
        candidates.append(prepared)

        if self.extract_audio:
            base, _ = os.path.splitext(prepared)
            for ext in ("mp3", "m4a", "opus", "aac", "wav"):
                candidates.insert(0, f"{base}.{ext}")

        for path in candidates:
            if path and os.path.exists(path):
                return path

        return candidates[0] if candidates else ""

    def _progress_hook(self, data: dict):
        if self._cancelled:
            raise DownloadError("Cancelled by user")

        status = data.get("status")
        if status == "downloading":
            total = data.get("total_bytes") or data.get("total_bytes_estimate") or 0
            downloaded = data.get("downloaded_bytes", 0)

            if total:
                percent = (downloaded / total) * 100.0
            else:
                value = data.get("_percent_str", "").strip().rstrip("%")
                try:
                    percent = float(value)
                except Exception:
                    percent = 0.0

            speed = data.get("speed") or 0
            speed_mbps = (speed * 8 / 1_000_000) if speed else 0.0

            percent_str = data.get("_percent_str", "").strip()
            speed_str = data.get("_speed_str", "").strip()
            eta_str = data.get("_eta_str", "").strip()

            parts = [part for part in [percent_str, speed_str] if part]
            if eta_str and eta_str != "Unknown ETA":
                parts.append(f"ETA {eta_str}")

            text = "  •  ".join(parts) if parts else "Загрузка..."
            self.progress.emit(self.task_id, percent, text, speed_mbps)

        elif status == "finished":
            self.progress.emit(self.task_id, 100.0, "Обработка файла...", 0.0)


@dataclass
class DownloadTask:
    task_id: str
    url: str
    fmt: str
    extract_audio: bool
    output_dir: str
    format_label: str
    card: "DownloadCard"
    status: str = "queued"
    title: str = "Получение информации..."
    filepath: str = ""
    thread: Optional[DownloadThread] = None
    result_recorded: bool = False


class DownloadCard(QFrame):
    cancel_requested = Signal(str)
    remove_requested = Signal(str)
    open_requested = Signal(str)

    def __init__(self, task_id: str, url: str):
        super().__init__()
        self.task_id = task_id
        self.url = url
        self.file_path = ""
        self.thumbnail_url = ""
        self._thumb_thread: Optional[ThumbnailThread] = None

        self.setObjectName("downloadCard")
        self._setup_ui()
        self._setup_animation()

    def _setup_ui(self):
        layout = QHBoxLayout(self)
        layout.setContentsMargins(16, 16, 16, 16)
        layout.setSpacing(14)

        self.thumb_label = QLabel("📹")
        self.thumb_label.setObjectName("cardThumb")
        self.thumb_label.setFixedSize(128, 72)
        self.thumb_label.setAlignment(Qt.AlignCenter)

        info_layout = QVBoxLayout()
        info_layout.setSpacing(6)

        self.title_label = QLabel("Получение информации...")
        self.title_label.setObjectName("cardTitle")
        self.title_label.setWordWrap(True)

        self.status_label = QLabel("⏳ В очереди")
        self.status_label.setObjectName("cardStatus")

        self.progress_bar = QProgressBar()
        self.progress_bar.setObjectName("cardProgress")
        self.progress_bar.setTextVisible(False)
        self.progress_bar.setRange(0, 100)
        self.progress_bar.setValue(0)
        self.progress_bar.setFixedHeight(8)

        self.speed_label = QLabel("Ожидание")
        self.speed_label.setObjectName("cardSpeed")

        info_layout.addWidget(self.title_label)
        info_layout.addWidget(self.status_label)
        info_layout.addWidget(self.progress_bar)
        info_layout.addWidget(self.speed_label)

        actions_layout = QVBoxLayout()
        actions_layout.setSpacing(8)

        self.cancel_btn = QPushButton("✕")
        self.cancel_btn.setObjectName("cardCancelButton")
        self.cancel_btn.setFixedSize(36, 36)
        self.cancel_btn.setToolTip("Отменить загрузку")
        self.cancel_btn.clicked.connect(lambda: self.cancel_requested.emit(self.task_id))

        self.open_btn = QPushButton("📂")
        self.open_btn.setObjectName("cardOpenButton")
        self.open_btn.setFixedSize(36, 36)
        self.open_btn.setToolTip("Открыть файл")
        self.open_btn.setVisible(False)
        self.open_btn.clicked.connect(self._on_open_clicked)

        self.remove_btn = QPushButton("🗑")
        self.remove_btn.setObjectName("cardRemoveButton")
        self.remove_btn.setFixedSize(36, 36)
        self.remove_btn.setToolTip("Убрать карточку")
        self.remove_btn.setVisible(False)
        self.remove_btn.clicked.connect(lambda: self.remove_requested.emit(self.task_id))

        actions_layout.addWidget(self.cancel_btn)
        actions_layout.addWidget(self.open_btn)
        actions_layout.addWidget(self.remove_btn)
        actions_layout.addStretch()

        layout.addWidget(self.thumb_label)
        layout.addLayout(info_layout, 1)
        layout.addLayout(actions_layout)

    def _setup_animation(self):
        self.opacity_effect = QGraphicsOpacityEffect(self)
        self.setGraphicsEffect(self.opacity_effect)

        fade_in = QPropertyAnimation(self.opacity_effect, b"opacity")
        fade_in.setDuration(350)
        fade_in.setStartValue(0.0)
        fade_in.setEndValue(1.0)
        fade_in.setEasingCurve(QEasingCurve.OutCubic)
        fade_in.start()

        # Удерживаем ссылку, чтобы анимация не была собрана GC
        self._fade_in = fade_in

    def set_queue_position(self, position: int):
        self.status_label.setText(f"⏳ В очереди ({position})")
        self.speed_label.setText("Ожидание")

    def set_running(self):
        self.status_label.setText("⬇️ Подготовка загрузки...")
        self.speed_label.setText("Инициализация")

    def set_cancel_pending(self):
        self.status_label.setText("⛔ Отмена...")
        self.speed_label.setText("Останавливаю")

    def set_info(self, title: str, thumbnail_url: str, duration_seconds: Optional[int], uploader: str):
        clean_title = title or "Без названия"
        self.title_label.setText(clean_title)
        self.title_label.setToolTip(clean_title)

        details: List[str] = []
        duration_text = format_duration(duration_seconds)
        if duration_text:
            details.append(duration_text)
        if uploader:
            details.append(uploader)

        if details:
            self.speed_label.setText(" • ".join(details))

        if thumbnail_url and thumbnail_url != self.thumbnail_url:
            self.thumbnail_url = thumbnail_url
            self._load_thumbnail_async(thumbnail_url)

    def update_progress(self, percent: float, text: str, speed_mbps: float):
        self.progress_bar.setValue(max(0, min(100, int(percent))))
        self.status_label.setText(f"⬇️ {text}" if text else "⬇️ Загрузка...")
        if speed_mbps > 0:
            self.speed_label.setText(f"⚡ {speed_mbps:.1f} Мбит/с")

    def set_finished(self, success: bool, message: str, filepath: str):
        self.progress_bar.setValue(100 if success else self.progress_bar.value())
        self.status_label.setText(message)
        self.cancel_btn.setVisible(False)
        self.remove_btn.setVisible(True)

        self.file_path = filepath or ""
        if success and self.file_path:
            self.open_btn.setVisible(True)
            self.open_btn.setEnabled(True)
            self.speed_label.setText("✓ Готово")
        else:
            self.open_btn.setVisible(False)
            if "отмен" in message.lower():
                self.speed_label.setText("Отменено")
            else:
                self.speed_label.setText("Ошибка")

    def _load_thumbnail_async(self, url: str):
        thumb_thread = ThumbnailThread(url)
        self._thumb_thread = thumb_thread
        thumb_thread.thumbnail_loaded.connect(self._on_thumbnail_loaded)
        thumb_thread.finished.connect(thumb_thread.deleteLater)
        thumb_thread.start()

    def _on_thumbnail_loaded(self, data: bytes):
        pixmap = QPixmap()
        if pixmap.loadFromData(data):
            scaled = pixmap.scaled(128, 72, Qt.KeepAspectRatioByExpanding, Qt.SmoothTransformation)
            self.thumb_label.setPixmap(scaled)
            self.thumb_label.setText("")

    def _on_open_clicked(self):
        if self.file_path:
            self.open_requested.emit(self.file_path)


class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()

        self.settings = self.load_settings()
        self.total_downloads = 0
        self.successful_downloads = 0

        self.tasks: Dict[str, DownloadTask] = {}
        self.download_queue: List[str] = []
        self.running_task_ids: set[str] = set()
        self.info_threads: Dict[str, InfoThread] = {}

        self._task_counter = 0
        self._anim_phase = 0
        self.vpn_tip_shown = False

        self.ffmpeg_path = self.detect_ffmpeg()

        self.setWindowTitle(APP_TITLE)
        self.setMinimumSize(1180, 760)
        self.resize(1260, 860)

        icon_path = self.find_resource_file("icon.icns") or self.find_resource_file("icon.ico")
        if icon_path:
            self.setWindowIcon(QIcon(str(icon_path)))

        self.setup_ui()
        self.setup_menu()
        self.connect_signals()
        self.apply_style()
        self.update_stats()
        self.start_animations()

    def load_settings(self) -> dict:
        defaults = {
            "download_folder": str(Path.home() / "Downloads"),
            "format_index": 0,
            "max_parallel": 2,
            "auto_open": False,
        }
        try:
            if CONFIG_FILE.exists():
                with open(CONFIG_FILE, "r", encoding="utf-8") as file:
                    loaded = json.load(file)
                    if isinstance(loaded, dict):
                        defaults.update(loaded)
        except Exception:
            pass

        defaults["max_parallel"] = int(max(1, min(5, defaults.get("max_parallel", 2))))
        defaults["format_index"] = int(
            max(0, min(len(FORMAT_PRESETS) - 1, defaults.get("format_index", 0)))
        )
        return defaults

    def save_settings(self):
        if not hasattr(self, "folder_edit"):
            return

        payload = {
            "download_folder": self.folder_edit.text().strip(),
            "format_index": self.format_box.currentIndex(),
            "max_parallel": self.parallel_spin.value(),
            "auto_open": self.auto_open_check.isChecked(),
        }
        try:
            with open(CONFIG_FILE, "w", encoding="utf-8") as file:
                json.dump(payload, file, ensure_ascii=False, indent=2)
        except Exception:
            pass

    def runtime_search_dirs(self) -> List[Path]:
        paths: List[Path] = []

        meipass = getattr(sys, "_MEIPASS", None)
        if meipass:
            paths.append(Path(meipass))

        try:
            paths.append(Path(sys.executable).resolve().parent)
        except Exception:
            pass

        try:
            paths.append(Path(__file__).resolve().parent)
        except Exception:
            pass

        expanded: List[Path] = []
        for path in paths:
            expanded.append(path)
            resources = path.parent / "Resources"
            frameworks = path.parent / "Frameworks"
            if resources.exists():
                expanded.append(resources)
            if frameworks.exists():
                expanded.append(frameworks)

        unique: List[Path] = []
        seen: set[str] = set()
        for path in expanded:
            key = str(path)
            if key not in seen:
                seen.add(key)
                unique.append(path)
        return unique

    def find_resource_file(self, filename: str) -> Optional[Path]:
        for base_dir in self.runtime_search_dirs():
            candidate = base_dir / filename
            if candidate.exists():
                return candidate
        return None

    def detect_ffmpeg(self) -> str:
        ffmpeg_binary = shutil.which("ffmpeg")
        if ffmpeg_binary:
            return ffmpeg_binary

        for candidate in ["ffmpeg.exe", "ffmpeg"]:
            resource_path = self.find_resource_file(candidate)
            if resource_path:
                return str(resource_path)

        return ""

    def setup_ui(self):
        central = QWidget()
        self.setCentralWidget(central)

        root = QVBoxLayout(central)
        root.setContentsMargins(18, 18, 18, 18)
        root.setSpacing(14)

        root.addWidget(self.create_header())
        root.addWidget(self.create_control_panel())
        root.addWidget(self.create_stats_panel())
        root.addWidget(self.create_downloads_panel(), 1)
        root.addWidget(self.create_vpn_promo_panel())
        root.addWidget(self.create_footer())

    def create_header(self) -> QFrame:
        frame = QFrame()
        frame.setObjectName("headerFrame")
        frame.setFixedHeight(92)

        layout = QHBoxLayout(frame)
        layout.setContentsMargins(24, 18, 24, 18)

        text_layout = QVBoxLayout()
        text_layout.setSpacing(4)

        title = QLabel("🎬 Video Downloader Pro")
        title.setObjectName("appTitle")

        subtitle = QLabel("YouTube, Instagram, TikTok и другие платформы через yt-dlp")
        subtitle.setObjectName("appSubtitle")

        text_layout.addWidget(title)
        text_layout.addWidget(subtitle)

        self.app_status = QLabel("● Готов к работе")
        self.app_status.setObjectName("appStatus")

        layout.addLayout(text_layout, 1)
        layout.addWidget(self.app_status, 0, Qt.AlignRight | Qt.AlignVCenter)

        return frame

    def create_control_panel(self) -> QFrame:
        frame = QFrame()
        frame.setObjectName("controlPanel")

        layout = QVBoxLayout(frame)
        layout.setContentsMargins(24, 18, 24, 18)
        layout.setSpacing(12)

        url_label = QLabel("Ссылка")
        url_label.setObjectName("sectionLabel")

        url_row = QHBoxLayout()
        url_row.setSpacing(10)

        self.url_edit = QLineEdit()
        self.url_edit.setObjectName("urlEdit")
        self.url_edit.setMinimumHeight(46)
        self.url_edit.setPlaceholderText("https://www.youtube.com/watch?v=...")

        self.paste_btn = QPushButton("📋")
        self.paste_btn.setObjectName("iconButton")
        self.paste_btn.setFixedSize(46, 46)
        self.paste_btn.setToolTip("Вставить из буфера")

        self.info_btn = QPushButton("ℹ️")
        self.info_btn.setObjectName("iconButton")
        self.info_btn.setFixedSize(46, 46)
        self.info_btn.setToolTip("Проверить ссылку")

        url_row.addWidget(self.url_edit)
        url_row.addWidget(self.paste_btn)
        url_row.addWidget(self.info_btn)

        format_label = QLabel("Формат")
        format_label.setObjectName("inputLabel")

        self.format_box = QComboBox()
        self.format_box.setObjectName("comboBox")
        self.format_box.setMinimumHeight(40)
        self.format_box.setMaxVisibleItems(len(FORMAT_PRESETS))
        for preset in FORMAT_PRESETS:
            self.format_box.addItem(preset["label"], preset)
        self.format_box.setCurrentIndex(self.settings["format_index"])
        parallel_label = QLabel("Параллельно")
        parallel_label.setObjectName("inputLabel")

        self.parallel_spin = QSpinBox()
        self.parallel_spin.setObjectName("spinBox")
        self.parallel_spin.setRange(1, 5)
        self.parallel_spin.setValue(self.settings["max_parallel"])
        self.parallel_spin.setMinimumHeight(40)
        self.parallel_spin.setAlignment(Qt.AlignCenter)
        folder_label = QLabel("Папка сохранения")
        folder_label.setObjectName("inputLabel")

        folder_row = QHBoxLayout()
        folder_row.setSpacing(8)

        self.folder_edit = QLineEdit()
        self.folder_edit.setObjectName("folderEdit")
        self.folder_edit.setText(self.settings["download_folder"])
        self.folder_edit.setReadOnly(True)
        self.folder_edit.setMinimumHeight(40)

        self.browse_btn = QPushButton("📁")
        self.browse_btn.setObjectName("iconButton")
        self.browse_btn.setFixedSize(40, 40)
        self.browse_btn.setToolTip("Выбрать папку")

        folder_row.addWidget(self.folder_edit)
        folder_row.addWidget(self.browse_btn)
        settings_grid = QGridLayout()
        settings_grid.setHorizontalSpacing(12)
        settings_grid.setVerticalSpacing(6)

        folder_widget = QWidget()
        folder_widget.setLayout(folder_row)

        settings_grid.addWidget(format_label, 0, 0)
        settings_grid.addWidget(parallel_label, 0, 1)
        settings_grid.addWidget(folder_label, 0, 2)
        settings_grid.addWidget(self.format_box, 1, 0)
        settings_grid.addWidget(self.parallel_spin, 1, 1)
        settings_grid.addWidget(folder_widget, 1, 2)
        settings_grid.setColumnStretch(0, 2)
        settings_grid.setColumnStretch(1, 1)
        settings_grid.setColumnStretch(2, 3)

        actions_row = QHBoxLayout()
        actions_row.setContentsMargins(0, 2, 0, 0)
        actions_row.setSpacing(10)

        self.auto_open_check = QCheckBox("Авто-открывать файл после успешной загрузки")
        self.auto_open_check.setObjectName("checkBox")
        self.auto_open_check.setChecked(bool(self.settings.get("auto_open")))
        self.auto_open_check.setMinimumHeight(30)
        self.auto_open_check.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Fixed)

        self.open_folder_btn = QPushButton("Открыть папку")
        self.open_folder_btn.setObjectName("secondaryButton")
        self.open_folder_btn.setMinimumHeight(40)
        self.open_folder_btn.setMinimumWidth(150)

        self.download_btn = QPushButton("Добавить в очередь")
        self.download_btn.setObjectName("primaryButton")
        self.download_btn.setMinimumHeight(40)
        self.download_btn.setMinimumWidth(220)
        self.download_btn.setMaximumWidth(280)

        actions_row.addWidget(self.auto_open_check)
        actions_row.addStretch()
        actions_row.addWidget(self.open_folder_btn)
        actions_row.addWidget(self.download_btn)

        layout.addWidget(url_label)
        layout.addLayout(url_row)
        layout.addLayout(settings_grid)
        layout.addLayout(actions_row)

        return frame

    def create_stats_panel(self) -> QFrame:
        frame = QFrame()
        frame.setObjectName("statsPanel")

        layout = QHBoxLayout(frame)
        layout.setContentsMargins(20, 12, 20, 12)
        layout.setSpacing(20)

        self.stats_total = QLabel("📊 Всего: 0")
        self.stats_total.setObjectName("statsLabel")

        self.stats_success = QLabel("✅ Успешно: 0")
        self.stats_success.setObjectName("statsLabel")

        self.stats_active = QLabel("⬇️ Активных: 0 • Очередь: 0")
        self.stats_active.setObjectName("statsLabel")

        layout.addWidget(self.stats_total)
        layout.addWidget(self.stats_success)
        layout.addWidget(self.stats_active)
        layout.addStretch()

        return frame

    def create_downloads_panel(self) -> QFrame:
        frame = QFrame()
        frame.setObjectName("downloadsPanel")

        layout = QVBoxLayout(frame)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(8)

        top_row = QHBoxLayout()
        top_row.setContentsMargins(4, 0, 4, 0)

        label = QLabel("📥 Очередь загрузок")
        label.setObjectName("sectionLabel")

        self.clear_finished_btn = QPushButton("Очистить завершенные")
        self.clear_finished_btn.setObjectName("secondaryButton")
        self.clear_finished_btn.setMinimumHeight(34)

        self.cancel_all_btn = QPushButton("Отменить все")
        self.cancel_all_btn.setObjectName("dangerButton")
        self.cancel_all_btn.setMinimumHeight(34)

        top_row.addWidget(label)
        top_row.addStretch()
        top_row.addWidget(self.clear_finished_btn)
        top_row.addWidget(self.cancel_all_btn)

        self.downloads_scroll = QScrollArea()
        self.downloads_scroll.setObjectName("downloadScroll")
        self.downloads_scroll.setWidgetResizable(True)
        self.downloads_scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarAlwaysOff)

        scroll_widget = QWidget()
        self.downloads_layout = QVBoxLayout(scroll_widget)
        self.downloads_layout.setContentsMargins(0, 0, 0, 0)
        self.downloads_layout.setSpacing(10)
        self.downloads_layout.addStretch()

        self.downloads_scroll.setWidget(scroll_widget)

        layout.addLayout(top_row)
        layout.addWidget(self.downloads_scroll)

        return frame

    def create_vpn_promo_panel(self) -> QFrame:
        frame = QFrame()
        frame.setObjectName("vpnPromoPanel")

        layout = QHBoxLayout(frame)
        layout.setContentsMargins(18, 12, 18, 12)
        layout.setSpacing(14)

        text_layout = QVBoxLayout()
        text_layout.setSpacing(4)

        title = QLabel("🌐 Видео не грузится?")
        title.setObjectName("promoTitle")

        text = QLabel(
            "Иногда причина в сетевых ограничениях. Можно попробовать безопасный интернет-маршрут (VPN) от onyshop.tech."
        )
        text.setObjectName("promoText")
        text.setWordWrap(True)

        text_layout.addWidget(title)
        text_layout.addWidget(text)

        buttons_layout = QHBoxLayout()
        buttons_layout.setSpacing(8)

        self.promo_open_btn = QPushButton("Открыть onyshop.tech")
        self.promo_open_btn.setObjectName("promoButton")
        self.promo_open_btn.setMinimumHeight(34)

        self.promo_help_btn = QPushButton("Как это поможет")
        self.promo_help_btn.setObjectName("promoButton")
        self.promo_help_btn.setMinimumHeight(34)

        buttons_layout.addWidget(self.promo_open_btn)
        buttons_layout.addWidget(self.promo_help_btn)

        right_col = QVBoxLayout()
        right_col.addLayout(buttons_layout)
        right_col.addStretch()

        layout.addLayout(text_layout, 1)
        layout.addLayout(right_col)

        return frame

    def create_footer(self) -> QFrame:
        frame = QFrame()
        frame.setObjectName("footerFrame")
        frame.setFixedHeight(48)

        layout = QHBoxLayout(frame)
        layout.setContentsMargins(16, 8, 16, 8)

        coded_label = QLabel("Coded by Jacksony")
        coded_label.setObjectName("footerLabel")

        tg_btn = QPushButton("Telegram")
        tg_btn.setObjectName("footerButton")
        tg_btn.setFixedHeight(30)
        tg_btn.clicked.connect(
            lambda: QDesktopServices.openUrl(QUrl("https://t.me/Smesharik_lair"))
        )

        layout.addWidget(coded_label)
        layout.addStretch()
        layout.addWidget(tg_btn)

        return frame

    def setup_menu(self):
        menu = self.menuBar()
        menu.setNativeMenuBar(sys.platform == "darwin")

        file_menu = menu.addMenu("Файл")
        self._add_menu_action(file_menu, "Фокус на ссылке", self.focus_url_input, "Ctrl+L")
        self._add_menu_action(file_menu, "Открыть папку загрузок", self.open_download_folder, "Ctrl+O")
        file_menu.addSeparator()
        self._add_menu_action(file_menu, "Выход", self.close, "Ctrl+Q")

        downloads_menu = menu.addMenu("Загрузки")
        self._add_menu_action(downloads_menu, "Добавить в очередь", self.start_download, "Ctrl+D")
        self._add_menu_action(downloads_menu, "Проверить ссылку", self.check_url_info, "Ctrl+I")
        downloads_menu.addSeparator()
        self._add_menu_action(downloads_menu, "Отменить все", self.cancel_all_downloads, "Ctrl+Shift+C")
        self._add_menu_action(downloads_menu, "Очистить завершенные", self.clear_finished_cards, "Ctrl+Shift+X")

        tools_menu = menu.addMenu("Инструменты")
        self._add_menu_action(tools_menu, "Открыть onyshop.tech", self.open_onyshop_site)
        self._add_menu_action(tools_menu, "Подсказка при блокировке", self.show_vpn_help_dialog)

        help_menu = menu.addMenu("Справка")
        self._add_menu_action(help_menu, "О приложении", self.show_about_dialog, "F1")

    def _add_menu_action(self, menu, title: str, callback, shortcut: str = ""):
        action = QAction(title, self)
        if shortcut:
            action.setShortcut(shortcut)
        action.triggered.connect(callback)
        menu.addAction(action)
        return action

    def connect_signals(self):
        self.download_btn.clicked.connect(self.start_download)
        self.url_edit.returnPressed.connect(self.start_download)
        self.paste_btn.clicked.connect(self.paste_url)
        self.info_btn.clicked.connect(self.check_url_info)

        self.browse_btn.clicked.connect(self.browse_folder)
        self.open_folder_btn.clicked.connect(self.open_download_folder)

        self.clear_finished_btn.clicked.connect(self.clear_finished_cards)
        self.cancel_all_btn.clicked.connect(self.cancel_all_downloads)
        self.promo_open_btn.clicked.connect(self.open_onyshop_site)
        self.promo_help_btn.clicked.connect(self.show_vpn_help_dialog)

        self.format_box.currentIndexChanged.connect(self.save_settings)
        self.parallel_spin.valueChanged.connect(self._on_parallel_changed)
        self.auto_open_check.toggled.connect(self.save_settings)

    def start_animations(self):
        self.anim_timer = QTimer(self)
        self.anim_timer.timeout.connect(self.update_status_animation)
        self.anim_timer.start(900)

    def update_status_animation(self):
        running_count = len(self.running_task_ids)
        queued_count = len(self.download_queue)

        if running_count > 0:
            self.app_status.setText(f"● Активных загрузок: {running_count}")
            return

        if queued_count > 0:
            self.app_status.setText(f"● В очереди: {queued_count}")
            return

        dots = "." * (self._anim_phase % 4)
        self._anim_phase += 1

        if self.ffmpeg_path:
            self.app_status.setText(f"● Готов к работе{dots}")
        else:
            self.app_status.setText(f"● Готово (ffmpeg не найден){dots}")

    def _on_parallel_changed(self):
        self.save_settings()
        self.pump_queue()

    def paste_url(self):
        text = QApplication.clipboard().text().strip()
        if text:
            self.url_edit.setText(text)
            self.url_edit.setFocus()

    def browse_folder(self):
        folder = QFileDialog.getExistingDirectory(
            self,
            "Выберите папку сохранения",
            self.folder_edit.text().strip() or str(Path.home()),
        )
        if folder:
            self.folder_edit.setText(folder)
            self.save_settings()

    def open_download_folder(self):
        folder = self.folder_edit.text().strip()
        if folder and os.path.isdir(folder):
            QDesktopServices.openUrl(QUrl.fromLocalFile(folder))
        else:
            QMessageBox.warning(self, "Папка", "Папка не найдена")

    def focus_url_input(self):
        self.url_edit.setFocus()
        self.url_edit.selectAll()

    def open_onyshop_site(self):
        QDesktopServices.openUrl(QUrl(ONYSHOP_URL))

    def show_vpn_help_dialog(self):
        msg_box = QMessageBox(self)
        msg_box.setIcon(QMessageBox.Information)
        msg_box.setWindowTitle("Если видео не загружается")
        msg_box.setText("Иногда платформы ограничивают доступ к видео по сети или региону.")
        msg_box.setInformativeText(
            "Можно попробовать безопасный интернет-маршрут (VPN): onyshop.tech"
        )
        open_btn = msg_box.addButton("Открыть onyshop.tech", QMessageBox.AcceptRole)
        msg_box.addButton("Закрыть", QMessageBox.RejectRole)
        msg_box.exec()

        if msg_box.clickedButton() == open_btn:
            self.open_onyshop_site()

    def show_about_dialog(self):
        ffmpeg_status = "найден" if self.ffmpeg_path else "не найден"
        QMessageBox.information(
            self,
            "О приложении",
            "Video Downloader Pro\\n\\n"
            "Скачивание видео/аудио через yt-dlp\\n"
            "Меню, очередь и подсказки при проблемах с сетью.\\n\\n"
            f"FFmpeg: {ffmpeg_status}",
        )

    def check_url_info(self):
        url = self.url_edit.text().strip()
        if not is_http_url(url):
            QMessageBox.warning(self, "Проверка", "Введите корректную ссылку")
            return

        if PREVIEW_TASK_ID in self.info_threads:
            return

        self.info_btn.setEnabled(False)
        self.app_status.setText("● Проверяю ссылку...")

        thread = InfoThread(PREVIEW_TASK_ID, url)
        thread.info_ready.connect(self.on_info_ready)
        thread.error.connect(self.on_info_error)
        thread.finished.connect(lambda: self._cleanup_info_thread(PREVIEW_TASK_ID))
        thread.finished.connect(lambda: self.info_btn.setEnabled(True))
        thread.finished.connect(thread.deleteLater)

        self.info_threads[PREVIEW_TASK_ID] = thread
        thread.start()

    def start_download(self):
        url = self.url_edit.text().strip()
        if not is_http_url(url):
            QMessageBox.warning(self, "Ошибка", "Введите корректную ссылку")
            return

        output_dir = self.folder_edit.text().strip()
        if not output_dir:
            QMessageBox.warning(self, "Ошибка", "Выберите папку сохранения")
            return

        output_path = Path(output_dir).expanduser()
        try:
            output_path.mkdir(parents=True, exist_ok=True)
        except Exception as exc:
            QMessageBox.warning(self, "Ошибка", f"Не удалось создать папку:\n{exc}")
            return

        preset = self.format_box.currentData() or FORMAT_PRESETS[0]
        fmt = preset["format"]
        extract_audio = bool(preset.get("extract_audio"))

        if extract_audio and not self.ffmpeg_path:
            QMessageBox.warning(
                self,
                "FFmpeg не найден",
                "Для конвертации в MP3 нужен ffmpeg.\n"
                "Установите ffmpeg в систему или положите ffmpeg.exe рядом с main.py.",
            )
            return

        self._task_counter += 1
        task_id = f"task-{self._task_counter}-{int(datetime.now().timestamp() * 1000)}"

        card = DownloadCard(task_id, url)
        card.cancel_requested.connect(self.cancel_download)
        card.remove_requested.connect(self.remove_task_card)
        card.open_requested.connect(self.open_downloaded_file)

        self.downloads_layout.insertWidget(0, card)

        task = DownloadTask(
            task_id=task_id,
            url=url,
            fmt=fmt,
            extract_audio=extract_audio,
            output_dir=str(output_path),
            format_label=preset["label"],
            card=card,
        )

        self.tasks[task_id] = task
        self.download_queue.append(task_id)

        self.total_downloads += 1
        self.update_queue_positions()
        self.update_stats()
        self.save_settings()

        self.url_edit.clear()
        self.start_info_lookup(task_id, url)
        self.pump_queue()

    def start_info_lookup(self, task_id: str, url: str):
        if task_id in self.info_threads:
            return

        thread = InfoThread(task_id, url)
        thread.info_ready.connect(self.on_info_ready)
        thread.error.connect(self.on_info_error)
        thread.finished.connect(lambda: self._cleanup_info_thread(task_id))
        thread.finished.connect(thread.deleteLater)

        self.info_threads[task_id] = thread
        thread.start()

    def _cleanup_info_thread(self, task_id: str):
        self.info_threads.pop(task_id, None)

    def on_info_ready(self, task_id: str, info: dict):
        title = info.get("title") or "Без названия"
        duration = info.get("duration")
        uploader = info.get("uploader") or ""
        webpage = info.get("webpage_url") or ""

        if task_id == PREVIEW_TASK_ID:
            duration_text = format_duration(duration)
            meta_parts = [part for part in [duration_text, uploader] if part]
            meta = " • ".join(meta_parts) if meta_parts else ""

            text = title
            if meta:
                text += f"\n{meta}"
            if webpage:
                text += f"\n{webpage}"

            QMessageBox.information(self, "Информация о ссылке", text)
            return

        task = self.tasks.get(task_id)
        if not task:
            return

        task.title = title
        thumbnail = info.get("thumbnail") or ""
        task.card.set_info(title, thumbnail, duration, uploader)

    def on_info_error(self, task_id: str, error_text: str):
        if task_id == PREVIEW_TASK_ID:
            QMessageBox.warning(self, "Проверка ссылки", f"Не удалось получить информацию:\n{error_text}")
            return

        task = self.tasks.get(task_id)
        if task and task.status == "queued":
            task.card.status_label.setText("⏳ В очереди")

    def pump_queue(self):
        max_parallel = self.parallel_spin.value()

        while len(self.running_task_ids) < max_parallel and self.download_queue:
            task_id = self.download_queue.pop(0)
            task = self.tasks.get(task_id)
            if not task or task.status != "queued":
                continue

            thread = DownloadThread(
                task_id=task.task_id,
                url=task.url,
                fmt=task.fmt,
                out_dir=task.output_dir,
                extract_audio=task.extract_audio,
                ffmpeg_location=self.ffmpeg_path,
            )
            thread.progress.connect(self.on_progress)
            thread.info_ready.connect(self.on_info_ready)
            thread.finished.connect(self.on_download_finished)
            thread.finished.connect(thread.deleteLater)

            task.thread = thread
            task.status = "running"
            task.card.set_running()

            self.running_task_ids.add(task_id)
            thread.start()

        self.update_queue_positions()
        self.update_stats()

    def update_queue_positions(self):
        for index, queued_task_id in enumerate(self.download_queue, start=1):
            task = self.tasks.get(queued_task_id)
            if task and task.status == "queued":
                task.card.set_queue_position(index)

    def on_progress(self, task_id: str, percent: float, text: str, speed_mbps: float):
        task = self.tasks.get(task_id)
        if task:
            task.card.update_progress(percent, text, speed_mbps)

    def on_download_finished(self, task_id: str, message: str, success: bool, filepath: str):
        task = self.tasks.get(task_id)
        self.running_task_ids.discard(task_id)

        if not task:
            self.pump_queue()
            return

        if task.status == "cancelling":
            success = False
            message = "⛔ Загрузка отменена"
            filepath = ""

        task.filepath = filepath or ""
        task.thread = None

        if success:
            task.status = "completed"
        elif "отмен" in message.lower() or "cancel" in message.lower():
            task.status = "cancelled"
        else:
            task.status = "failed"

        task.card.set_finished(success, message, task.filepath)

        self.record_task_result(task, success)
        if task.status == "failed":
            self.offer_network_help_if_needed(message)

        if success and task.filepath and self.auto_open_check.isChecked():
            self.open_downloaded_file(task.filepath)

        self.update_stats()
        self.pump_queue()

    def record_task_result(self, task: DownloadTask, success: bool):
        if task.result_recorded:
            return

        task.result_recorded = True
        if success:
            self.successful_downloads += 1

    def offer_network_help_if_needed(self, message: str):
        if self.vpn_tip_shown:
            return

        text = (message or "").lower()
        network_keywords = (
            "403",
            "429",
            "forbidden",
            "geo",
            "blocked",
            "restricted",
            "connection",
            "timeout",
            "timed out",
            "unable to download",
            "http error",
        )
        if not any(keyword in text for keyword in network_keywords):
            return

        self.vpn_tip_shown = True
        self.show_vpn_help_dialog()

    def cancel_download(self, task_id: str, *, pump: bool = True):
        task = self.tasks.get(task_id)
        if not task:
            return

        if task.status == "queued":
            if task_id in self.download_queue:
                self.download_queue.remove(task_id)
            task.status = "cancelled"
            task.card.set_finished(False, "⛔ Отменено", "")
            self.record_task_result(task, False)

        elif task.status == "running":
            task.status = "cancelling"
            task.card.set_cancel_pending()
            if task.thread:
                task.thread.cancel()

        if pump:
            self.update_queue_positions()
            self.update_stats()
            self.pump_queue()

    def cancel_all_downloads(self):
        active_count = sum(
            1 for task in self.tasks.values() if task.status in {"queued", "running", "cancelling"}
        )
        if active_count == 0:
            QMessageBox.information(self, "Отмена", "Нет активных задач")
            return

        reply = QMessageBox.question(
            self,
            "Отменить всё",
            f"Отменить все задачи ({active_count})?",
            QMessageBox.Yes | QMessageBox.No,
            QMessageBox.No,
        )
        if reply != QMessageBox.Yes:
            return

        for task_id in list(self.download_queue):
            self.cancel_download(task_id, pump=False)

        for task in list(self.tasks.values()):
            if task.status == "running":
                self.cancel_download(task.task_id, pump=False)

        self.update_queue_positions()
        self.update_stats()

    def remove_task_card(self, task_id: str):
        task = self.tasks.get(task_id)
        if not task:
            return

        if task.status in {"queued", "running", "cancelling"}:
            QMessageBox.information(
                self,
                "Карточка занята",
                "Нельзя удалить карточку активной задачи. Сначала отмените загрузку.",
            )
            return

        self.downloads_layout.removeWidget(task.card)
        task.card.deleteLater()
        del self.tasks[task_id]

        self.update_stats()

    def clear_finished_cards(self):
        removable_ids = [
            task_id
            for task_id, task in self.tasks.items()
            if task.status in {"completed", "failed", "cancelled"}
        ]

        for task_id in removable_ids:
            task = self.tasks.get(task_id)
            if task:
                self.downloads_layout.removeWidget(task.card)
                task.card.deleteLater()
                del self.tasks[task_id]

    def open_downloaded_file(self, filepath: str):
        if not filepath:
            QMessageBox.warning(self, "Файл", "Путь к файлу отсутствует")
            return

        if not os.path.exists(filepath):
            QMessageBox.warning(self, "Файл", f"Файл не найден:\n{filepath}")
            return

        QDesktopServices.openUrl(QUrl.fromLocalFile(filepath))

    def update_stats(self):
        running_count = len(self.running_task_ids)
        queued_count = len(self.download_queue)

        self.stats_total.setText(f"📊 Всего: {self.total_downloads}")
        self.stats_success.setText(f"✅ Успешно: {self.successful_downloads}")
        self.stats_active.setText(f"⬇️ Активных: {running_count} • Очередь: {queued_count}")

    def closeEvent(self, event):
        active_tasks = [
            task for task in self.tasks.values() if task.status in {"queued", "running", "cancelling"}
        ]

        if active_tasks:
            answer = QMessageBox.question(
                self,
                "Выход",
                f"Есть незавершенные задачи ({len(active_tasks)}). Отменить и выйти?",
                QMessageBox.Yes | QMessageBox.No,
                QMessageBox.No,
            )
            if answer != QMessageBox.Yes:
                event.ignore()
                return

            for task in list(active_tasks):
                if task.status == "queued":
                    self.cancel_download(task.task_id, pump=False)
                elif task.status == "running" and task.thread:
                    task.status = "cancelling"
                    task.thread.cancel()

            self.update_queue_positions()
            self.update_stats()

            for task in list(active_tasks):
                if task.thread and task.thread.isRunning():
                    task.thread.wait(2000)

        self.save_settings()
        event.accept()

    def apply_style(self):
        self.setStyleSheet(
            """
            QMainWindow {
                background-color: #000000;
            }

            QMenuBar {
                background-color: transparent;
                color: #d6d6d6;
                border: none;
                padding: 2px 4px;
            }

            QMenuBar::item {
                background-color: transparent;
                padding: 6px 12px;
                border-radius: 6px;
            }

            QMenuBar::item:selected {
                background-color: #1d1d1d;
                color: #ffffff;
            }

            QMenu {
                background-color: #0f0f0f;
                border: 1px solid #212121;
                color: #d6d6d6;
                padding: 4px;
            }

            QMenu::item {
                padding: 6px 18px;
                border-radius: 5px;
            }

            QMenu::item:selected {
                background-color: #014d2a;
                color: #ffffff;
            }

            QFrame#headerFrame,
            QFrame#controlPanel,
            QFrame#statsPanel,
            QFrame#footerFrame,
            QFrame#downloadCard {
                background-color: #0a0a0a;
                border: 1px solid #1a1a1a;
                border-radius: 14px;
            }

            QFrame#vpnPromoPanel {
                background: qlineargradient(
                    x1:0, y1:0, x2:1, y2:0,
                    stop:0 #082015,
                    stop:1 #103225
                );
                border: 1px solid #1f5139;
                border-radius: 14px;
            }

            QFrame#downloadsPanel {
                background-color: transparent;
                border: none;
            }

            QLabel#appTitle {
                color: #ffffff;
                font-size: 24px;
                font-weight: 700;
            }

            QLabel#appSubtitle {
                color: #8e8e8e;
                font-size: 13px;
            }

            QLabel#appStatus {
                color: #00ff7f;
                font-size: 13px;
                font-weight: 600;
            }

            QLabel#sectionLabel {
                color: #ffffff;
                font-size: 14px;
                font-weight: 600;
            }

            QLabel#inputLabel {
                color: #8e8e8e;
                font-size: 12px;
                font-weight: 500;
            }

            QLabel#statsLabel {
                color: #a8a8a8;
                font-size: 13px;
                font-weight: 600;
            }

            QLabel#promoTitle {
                color: #c9ffd8;
                font-size: 14px;
                font-weight: 700;
            }

            QLabel#promoText {
                color: #b8d8c4;
                font-size: 12px;
            }

            QLabel#cardThumb {
                background-color: #141414;
                border-radius: 8px;
                color: #555555;
                font-size: 32px;
            }

            QLabel#cardTitle {
                color: #ffffff;
                font-size: 14px;
                font-weight: 600;
            }

            QLabel#cardStatus {
                color: #a0a0a0;
                font-size: 12px;
            }

            QLabel#cardSpeed {
                color: #00ff7f;
                font-size: 11px;
                font-weight: 600;
            }

            QLineEdit#urlEdit,
            QLineEdit#folderEdit,
            QSpinBox#spinBox,
            QComboBox#comboBox {
                background-color: #000000;
                color: #ffffff;
                border: 2px solid #1d1d1d;
                border-radius: 10px;
                padding: 8px 12px;
                font-size: 13px;
            }

            QLineEdit#urlEdit:focus,
            QComboBox#comboBox:hover,
            QSpinBox#spinBox:hover {
                border-color: #00ff7f;
            }

            QLineEdit#folderEdit {
                color: #9a9a9a;
            }

            QComboBox#comboBox::drop-down {
                border: none;
                width: 24px;
            }

            QComboBox#comboBox QAbstractItemView {
                background-color: #0f0f0f;
                border: 1px solid #1f1f1f;
                color: #ffffff;
                selection-background-color: #00ff7f;
                selection-color: #000000;
            }

            QComboBox#comboBox QAbstractItemView::item {
                min-height: 26px;
                padding: 2px 8px;
            }

            QPushButton#primaryButton {
                background: qlineargradient(
                    x1:0, y1:0, x2:1, y2:0,
                    stop:0 #00ff7f,
                    stop:1 #00cc66
                );
                color: #000000;
                font-size: 15px;
                font-weight: 700;
                border-radius: 12px;
                border: none;
            }

            QPushButton#primaryButton:hover {
                background: qlineargradient(
                    x1:0, y1:0, x2:1, y2:0,
                    stop:0 #00ff92,
                    stop:1 #00db72
                );
            }

            QPushButton#secondaryButton,
            QPushButton#footerButton,
            QPushButton#promoButton {
                background-color: #1a1a1a;
                color: #c9c9c9;
                border: 1px solid #2a2a2a;
                border-radius: 10px;
                padding: 6px 12px;
                font-size: 12px;
                font-weight: 600;
            }

            QPushButton#secondaryButton:hover,
            QPushButton#footerButton:hover,
            QPushButton#promoButton:hover {
                border-color: #00ff7f;
                color: #ffffff;
            }

            QPushButton#dangerButton {
                background-color: #2a1111;
                color: #ff8080;
                border: 1px solid #4d1f1f;
                border-radius: 10px;
                padding: 6px 12px;
                font-size: 12px;
                font-weight: 600;
            }

            QPushButton#dangerButton:hover {
                background-color: #3a1616;
                border-color: #7a2c2c;
                color: #ff9a9a;
            }

            QPushButton#iconButton,
            QPushButton#cardCancelButton,
            QPushButton#cardOpenButton,
            QPushButton#cardRemoveButton {
                background-color: #1b1b1b;
                border: 1px solid #2b2b2b;
                border-radius: 10px;
                font-size: 16px;
            }

            QPushButton#iconButton:hover,
            QPushButton#cardOpenButton:hover,
            QPushButton#cardRemoveButton:hover {
                border-color: #00ff7f;
                background-color: #242424;
            }

            QPushButton#cardCancelButton {
                background-color: #2b1111;
                border-color: #4a1f1f;
                color: #ff9090;
            }

            QPushButton#cardCancelButton:hover {
                background-color: #3a1616;
                border-color: #763333;
            }

            QCheckBox#checkBox {
                color: #d0d0d0;
                font-size: 12px;
                font-weight: 500;
            }

            QCheckBox#checkBox::indicator {
                width: 16px;
                height: 16px;
            }

            QCheckBox#checkBox::indicator:unchecked {
                border: 1px solid #3a3a3a;
                background: #0d0d0d;
                border-radius: 4px;
            }

            QCheckBox#checkBox::indicator:checked {
                border: 1px solid #00c06a;
                background: #00a95c;
                border-radius: 4px;
            }

            QProgressBar#cardProgress {
                border: 1px solid #1f1f1f;
                border-radius: 4px;
                background-color: #050505;
            }

            QProgressBar#cardProgress::chunk {
                background: qlineargradient(
                    x1:0, y1:0, x2:1, y2:0,
                    stop:0 #00ff7f,
                    stop:1 #00bb5f
                );
                border-radius: 3px;
            }

            QScrollArea#downloadScroll,
            QScrollArea#downloadScroll QWidget {
                background-color: transparent;
                border: none;
            }

            QScrollBar:vertical {
                background-color: #0d0d0d;
                width: 8px;
                margin: 0;
                border-radius: 4px;
            }

            QScrollBar::handle:vertical {
                background-color: #2b2b2b;
                border-radius: 4px;
                min-height: 28px;
            }

            QScrollBar::handle:vertical:hover {
                background-color: #3a3a3a;
            }

            QScrollBar::add-line:vertical,
            QScrollBar::sub-line:vertical {
                height: 0;
            }

            QLabel#footerLabel {
                color: #6f6f6f;
                font-size: 11px;
            }
            """
        )


def main():
    app = QApplication(sys.argv)
    app.setFont(QFontDatabase.systemFont(QFontDatabase.GeneralFont))

    window = MainWindow()
    window.show()

    sys.exit(app.exec())


if __name__ == "__main__":
    main()
