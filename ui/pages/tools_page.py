from __future__ import annotations

from PySide6.QtCore import Signal
from PySide6.QtWidgets import QCheckBox, QFrame, QGridLayout, QHBoxLayout, QLabel, QPushButton, QTextEdit, QVBoxLayout, QWidget

from core.models import ToolchainStatus


class ToolsPage(QWidget):
    check_updates_requested = Signal()
    update_ytdlp_requested = Signal()
    update_ffmpeg_requested = Signal()
    open_runtime_requested = Signal()
    repair_requested = Signal()
    copy_diagnostics_requested = Signal()
    auto_update_changed = Signal(bool)

    def __init__(self, parent=None):
        super().__init__(parent)
        root = QVBoxLayout(self)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(16)

        title = QLabel("Инструменты")
        title.setObjectName("PageTitle")
        subtitle = QLabel("yt-dlp и ffmpeg обновляются отдельно от приложения в runtime-папке.")
        subtitle.setObjectName("PageSubtitle")

        card = QFrame()
        card.setObjectName("Card")
        grid = QGridLayout(card)
        grid.setContentsMargins(18, 18, 18, 18)
        grid.setHorizontalSpacing(12)
        grid.setVerticalSpacing(10)

        self.ytdlp_version = QLabel("Версия: неизвестно")
        self.ytdlp_version.setObjectName("ToolValue")
        self.ytdlp_path = QLabel("Путь: не найден")
        self.ytdlp_path.setObjectName("MutedText")
        self.ytdlp_path.setWordWrap(True)
        self.ffmpeg_version = QLabel("Версия: неизвестно")
        self.ffmpeg_version.setObjectName("ToolValue")
        self.ffmpeg_path = QLabel("Путь: не найден")
        self.ffmpeg_path.setObjectName("MutedText")
        self.ffmpeg_path.setWordWrap(True)
        self.ffprobe_status = QLabel("ffprobe: не найден")
        self.ffprobe_status.setObjectName("MutedText")

        ytdlp_label = QLabel("yt-dlp")
        ytdlp_label.setObjectName("SectionTitle")
        ffmpeg_label = QLabel("ffmpeg")
        ffmpeg_label.setObjectName("SectionTitle")

        self.check_btn = QPushButton("Проверить обновления")
        self.check_btn.setObjectName("SecondaryButton")
        self.update_ytdlp_btn = QPushButton("Обновить yt-dlp")
        self.update_ytdlp_btn.setObjectName("PrimaryButton")
        self.update_ffmpeg_btn = QPushButton("Обновить ffmpeg")
        self.update_ffmpeg_btn.setObjectName("SecondaryButton")

        grid.addWidget(ytdlp_label, 0, 0)
        grid.addWidget(self.ytdlp_version, 1, 0)
        grid.addWidget(self.ytdlp_path, 2, 0)
        grid.addWidget(ffmpeg_label, 0, 1)
        grid.addWidget(self.ffmpeg_version, 1, 1)
        grid.addWidget(self.ffmpeg_path, 2, 1)
        grid.addWidget(self.ffprobe_status, 3, 1)
        grid.addWidget(self.check_btn, 4, 0)
        grid.addWidget(self.update_ytdlp_btn, 5, 0)
        grid.addWidget(self.update_ffmpeg_btn, 5, 1)
        grid.setColumnStretch(0, 1)
        grid.setColumnStretch(1, 1)

        diagnostics = QFrame()
        diagnostics.setObjectName("Card")
        diag_layout = QVBoxLayout(diagnostics)
        diag_layout.setContentsMargins(18, 18, 18, 18)
        diag_layout.setSpacing(12)
        self.auto_update = QCheckBox("Автообновлять инструменты")
        self.auto_update.setObjectName("CheckBox")
        hint = QLabel("Проверять при запуске не чаще раза в 24 часа.")
        hint.setObjectName("MutedText")
        buttons = QHBoxLayout()
        self.open_runtime_btn = QPushButton("Открыть runtime-папку")
        self.open_runtime_btn.setObjectName("SecondaryButton")
        self.repair_btn = QPushButton("Переустановить инструменты")
        self.repair_btn.setObjectName("DangerButton")
        self.copy_diag_btn = QPushButton("Скопировать диагностику")
        self.copy_diag_btn.setObjectName("SecondaryButton")
        buttons.addWidget(self.open_runtime_btn)
        buttons.addWidget(self.repair_btn)
        buttons.addWidget(self.copy_diag_btn)
        buttons.addStretch()
        self.log_panel = QTextEdit()
        self.log_panel.setObjectName("LogPanel")
        self.log_panel.setReadOnly(True)
        self.log_panel.setMinimumHeight(150)
        diag_layout.addWidget(self.auto_update)
        diag_layout.addWidget(hint)
        diag_layout.addLayout(buttons)
        diag_layout.addWidget(self.log_panel)

        root.addWidget(title)
        root.addWidget(subtitle)
        root.addWidget(card)
        root.addWidget(diagnostics, 1)

        self.check_btn.clicked.connect(self.check_updates_requested.emit)
        self.update_ytdlp_btn.clicked.connect(self.update_ytdlp_requested.emit)
        self.update_ffmpeg_btn.clicked.connect(self.update_ffmpeg_requested.emit)
        self.open_runtime_btn.clicked.connect(self.open_runtime_requested.emit)
        self.repair_btn.clicked.connect(self.repair_requested.emit)
        self.copy_diag_btn.clicked.connect(self.copy_diagnostics_requested.emit)
        self.auto_update.toggled.connect(self.auto_update_changed.emit)

    def set_status(self, status: ToolchainStatus) -> None:
        self.ytdlp_version.setText(f"Версия: {status.ytdlp.version or 'неизвестно'}")
        self.ytdlp_path.setText(f"Путь: {status.ytdlp.path or 'не найден'}")
        ffmpeg_verified = "" if status.ffmpeg.verified else " • unverified"
        self.ffmpeg_version.setText(f"Версия: {status.ffmpeg.version or 'неизвестно'}{ffmpeg_verified}")
        self.ffmpeg_path.setText(f"Путь: {status.ffmpeg.path or 'не найден'}")
        self.ffprobe_status.setText("ffprobe: найден" if status.ffprobe.exists else "ffprobe: не найден")
        self.auto_update.blockSignals(True)
        self.auto_update.setChecked(status.auto_update_enabled)
        self.auto_update.blockSignals(False)
        self.append_log(status.warning or "Инструменты проверены.")

    def append_log(self, text: str) -> None:
        if text:
            self.log_panel.append(text)
