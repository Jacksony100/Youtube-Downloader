from __future__ import annotations

from pathlib import Path

from PySide6.QtCore import Qt, Signal
from PySide6.QtGui import QPixmap
from PySide6.QtWidgets import (
    QFrame,
    QHBoxLayout,
    QLabel,
    QPushButton,
    QProgressBar,
    QVBoxLayout,
)

from core.downloader import format_duration
from ui.widgets.thumbnail import ThumbnailThread


class DownloadCard(QFrame):
    cancel_requested = Signal(str)
    remove_requested = Signal(str)
    retry_requested = Signal(str)
    open_file_requested = Signal(str)
    open_folder_requested = Signal(str)

    def __init__(self, task_id: str, url: str, format_label: str, parent=None):
        super().__init__(parent)
        self.task_id = task_id
        self.url = url
        self.output_path = ""
        self.thumbnail_url = ""
        self._thumb_thread: ThumbnailThread | None = None
        self.setObjectName("DownloadCard")

        root = QHBoxLayout(self)
        root.setContentsMargins(16, 14, 16, 14)
        root.setSpacing(14)

        self.thumb = QLabel("Видео")
        self.thumb.setObjectName("CardThumb")
        self.thumb.setFixedSize(142, 80)
        self.thumb.setAlignment(Qt.AlignCenter)

        info = QVBoxLayout()
        info.setSpacing(7)

        self.title_label = QLabel("Получение информации...")
        self.title_label.setObjectName("CardTitle")
        self.title_label.setWordWrap(True)

        self.meta_label = QLabel(format_label)
        self.meta_label.setObjectName("CardMeta")

        progress_row = QHBoxLayout()
        progress_row.setSpacing(10)
        self.progress = QProgressBar()
        self.progress.setObjectName("ProgressBar")
        self.progress.setRange(0, 100)
        self.progress.setValue(0)
        self.progress.setTextVisible(False)
        self.percent_label = QLabel("0%")
        self.percent_label.setObjectName("CardPercent")
        self.percent_label.setFixedWidth(46)
        self.percent_label.setAlignment(Qt.AlignRight | Qt.AlignVCenter)
        progress_row.addWidget(self.progress, 1)
        progress_row.addWidget(self.percent_label)

        self.status_label = QLabel("В очереди")
        self.status_label.setObjectName("CardStatus")

        info.addWidget(self.title_label)
        info.addWidget(self.meta_label)
        info.addLayout(progress_row)
        info.addWidget(self.status_label)

        actions = QVBoxLayout()
        actions.setSpacing(7)

        self.cancel_btn = QPushButton("Отменить")
        self.cancel_btn.setObjectName("SecondaryButton")
        self.cancel_btn.clicked.connect(lambda: self.cancel_requested.emit(self.task_id))

        self.open_file_btn = QPushButton("Файл")
        self.open_file_btn.setObjectName("SecondaryButton")
        self.open_file_btn.setVisible(False)
        self.open_file_btn.clicked.connect(lambda: self.open_file_requested.emit(self.output_path))

        self.open_folder_btn = QPushButton("Папка")
        self.open_folder_btn.setObjectName("SecondaryButton")
        self.open_folder_btn.setVisible(False)
        self.open_folder_btn.clicked.connect(lambda: self.open_folder_requested.emit(self.output_path))

        self.retry_btn = QPushButton("Повторить")
        self.retry_btn.setObjectName("SecondaryButton")
        self.retry_btn.setVisible(False)
        self.retry_btn.clicked.connect(lambda: self.retry_requested.emit(self.task_id))

        self.remove_btn = QPushButton("Убрать")
        self.remove_btn.setObjectName("DangerButton")
        self.remove_btn.setVisible(False)
        self.remove_btn.clicked.connect(lambda: self.remove_requested.emit(self.task_id))

        for button in (self.cancel_btn, self.open_file_btn, self.open_folder_btn, self.retry_btn, self.remove_btn):
            button.setMinimumHeight(30)
            actions.addWidget(button)
        actions.addStretch()

        root.addWidget(self.thumb)
        root.addLayout(info, 1)
        root.addLayout(actions)

    def set_queue_position(self, position: int) -> None:
        self.status_label.setText(f"В очереди ({position})")

    def set_running(self) -> None:
        self.status_label.setText("Подготовка загрузки...")
        self.cancel_btn.setVisible(True)
        self.retry_btn.setVisible(False)
        self.remove_btn.setVisible(False)

    def set_cancel_pending(self) -> None:
        self.status_label.setText("Отмена...")
        self.cancel_btn.setEnabled(False)

    def set_info(self, title: str, thumbnail_url: str, duration: int | None, uploader: str) -> None:
        clean_title = title or "Без названия"
        self.title_label.setText(clean_title)
        self.title_label.setToolTip(clean_title)
        parts = [part for part in (uploader, format_duration(duration)) if part]
        if parts:
            current_format = self.meta_label.text().split(" • ")[0]
            self.meta_label.setText(f"{current_format} • {' • '.join(parts)}")
        if thumbnail_url and thumbnail_url != self.thumbnail_url:
            self.thumbnail_url = thumbnail_url
            self._load_thumbnail(thumbnail_url)

    def update_progress(self, percent: float, speed_text: str, eta_text: str, downloaded_text: str) -> None:
        value = max(0, min(100, int(percent)))
        self.progress.setValue(value)
        self.percent_label.setText(f"{value}%")
        parts = []
        if speed_text:
            parts.append(speed_text)
        if eta_text:
            parts.append(f"ETA {eta_text}")
        if downloaded_text:
            parts.append(downloaded_text)
        self.status_label.setText(" • ".join(parts) if parts else "Загрузка...")

    def set_finished(self, success: bool, message: str, output_path: str = "") -> None:
        self.output_path = output_path or ""
        if success:
            self.progress.setValue(100)
            self.percent_label.setText("100%")
        self.status_label.setText(message)
        self.cancel_btn.setVisible(False)
        self.open_file_btn.setVisible(bool(success and self.output_path))
        self.open_folder_btn.setVisible(bool(success and self.output_path))
        self.retry_btn.setVisible(not success)
        self.remove_btn.setVisible(True)

    def _load_thumbnail(self, url: str) -> None:
        thread = ThumbnailThread(url)
        self._thumb_thread = thread
        thread.loaded.connect(self._on_thumbnail_loaded)
        thread.finished.connect(thread.deleteLater)
        thread.start()

    def _on_thumbnail_loaded(self, data: bytes) -> None:
        pixmap = QPixmap()
        if pixmap.loadFromData(data):
            scaled = pixmap.scaled(self.thumb.size(), Qt.KeepAspectRatioByExpanding, Qt.SmoothTransformation)
            self.thumb.setPixmap(scaled)
            self.thumb.setText("")

    def output_folder(self) -> str:
        return str(Path(self.output_path).parent) if self.output_path else ""
