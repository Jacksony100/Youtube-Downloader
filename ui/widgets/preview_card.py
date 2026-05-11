from __future__ import annotations

from pathlib import Path

from PySide6.QtCore import Qt, Signal
from PySide6.QtGui import QPixmap
from PySide6.QtWidgets import (
    QButtonGroup,
    QCheckBox,
    QFrame,
    QGridLayout,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QPushButton,
    QSpinBox,
    QVBoxLayout,
)

from core.downloader import format_duration
from core.models import FORMAT_PRESETS, VideoMetadata, get_format_preset
from ui.widgets.thumbnail import ThumbnailThread


class PreviewCard(QFrame):
    browse_requested = Signal()
    format_changed = Signal(str)
    parallel_changed = Signal(int)
    auto_open_changed = Signal(bool)

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setObjectName("PreviewCard")
        self.metadata: VideoMetadata | None = None
        self._thumb_thread: ThumbnailThread | None = None

        root = QVBoxLayout(self)
        root.setContentsMargins(18, 18, 18, 18)
        root.setSpacing(14)

        top = QHBoxLayout()
        top.setSpacing(16)

        self.thumb = QLabel("Превью")
        self.thumb.setObjectName("PreviewThumb")
        self.thumb.setFixedSize(224, 126)
        self.thumb.setAlignment(Qt.AlignCenter)

        text = QVBoxLayout()
        text.setSpacing(7)
        self.title_label = QLabel("Проверьте ссылку, чтобы увидеть превью")
        self.title_label.setObjectName("PreviewTitle")
        self.title_label.setWordWrap(True)
        self.meta_label = QLabel("")
        self.meta_label.setObjectName("PreviewMeta")
        self.summary_label = QLabel("")
        self.summary_label.setObjectName("PreviewSummary")
        self.summary_label.setWordWrap(True)
        text.addWidget(self.title_label)
        text.addWidget(self.meta_label)
        text.addWidget(self.summary_label)
        text.addStretch()

        top.addWidget(self.thumb)
        top.addLayout(text, 1)

        chips = QHBoxLayout()
        chips.setSpacing(8)
        self.format_group = QButtonGroup(self)
        self.format_group.setExclusive(True)
        for index, preset in enumerate(FORMAT_PRESETS):
            button = QPushButton(preset.label)
            button.setObjectName("FormatChip")
            button.setCheckable(True)
            button.setMinimumHeight(34)
            self.format_group.addButton(button, index)
            chips.addWidget(button)
        chips.addStretch()
        self.format_group.idClicked.connect(self._on_format_clicked)

        settings_grid = QGridLayout()
        settings_grid.setHorizontalSpacing(12)
        settings_grid.setVerticalSpacing(8)

        folder_label = QLabel("Папка сохранения")
        folder_label.setObjectName("InputLabel")
        self.folder_edit = QLineEdit()
        self.folder_edit.setObjectName("Input")
        self.folder_edit.setReadOnly(True)
        self.browse_btn = QPushButton("Выбрать")
        self.browse_btn.setObjectName("SecondaryButton")
        self.browse_btn.clicked.connect(self.browse_requested.emit)

        folder_row = QHBoxLayout()
        folder_row.setSpacing(8)
        folder_row.addWidget(self.folder_edit, 1)
        folder_row.addWidget(self.browse_btn)

        parallel_label = QLabel("Параллельно")
        parallel_label.setObjectName("InputLabel")
        self.parallel_spin = QSpinBox()
        self.parallel_spin.setObjectName("Input")
        self.parallel_spin.setRange(1, 5)
        self.parallel_spin.valueChanged.connect(self.parallel_changed.emit)

        self.auto_open = QCheckBox("Авто-открывать файл после загрузки")
        self.auto_open.setObjectName("CheckBox")
        self.auto_open.toggled.connect(self.auto_open_changed.emit)

        settings_grid.addWidget(folder_label, 0, 0)
        settings_grid.addWidget(parallel_label, 0, 1)
        settings_grid.addLayout(folder_row, 1, 0)
        settings_grid.addWidget(self.parallel_spin, 1, 1)
        settings_grid.addWidget(self.auto_open, 2, 0, 1, 2)
        settings_grid.setColumnStretch(0, 4)
        settings_grid.setColumnStretch(1, 1)

        root.addLayout(top)
        root.addLayout(chips)
        root.addLayout(settings_grid)

    def apply_settings(self, output_dir: str, format_key: str, parallel: int, auto_open: bool) -> None:
        self.folder_edit.setText(output_dir)
        self.parallel_spin.blockSignals(True)
        self.parallel_spin.setValue(parallel)
        self.parallel_spin.blockSignals(False)
        self.auto_open.blockSignals(True)
        self.auto_open.setChecked(auto_open)
        self.auto_open.blockSignals(False)
        self.set_format(format_key)

    def set_format(self, format_key: str) -> None:
        preset = get_format_preset(format_key)
        for button in self.format_group.buttons():
            button.setChecked(self.format_group.id(button) == FORMAT_PRESETS.index(preset))

    def selected_format_key(self) -> str:
        checked = self.format_group.checkedButton()
        if not checked:
            return "best"
        index = self.format_group.id(checked)
        return FORMAT_PRESETS[index].key if 0 <= index < len(FORMAT_PRESETS) else "best"

    def output_dir(self) -> str:
        return self.folder_edit.text().strip()

    def set_output_dir(self, folder: str) -> None:
        self.folder_edit.setText(str(Path(folder)))

    def set_metadata(self, metadata: VideoMetadata) -> None:
        self.metadata = metadata
        self.title_label.setText(metadata.title)
        parts = [part for part in (metadata.uploader, format_duration(metadata.duration), metadata.platform) if part]
        self.meta_label.setText(" • ".join(parts))
        self.summary_label.setText(metadata.format_summary)
        if metadata.thumbnail_url:
            self._load_thumbnail(metadata.thumbnail_url)

    def clear_metadata(self) -> None:
        self.metadata = None
        self.title_label.setText("Проверьте ссылку, чтобы увидеть превью")
        self.meta_label.setText("")
        self.summary_label.setText("")
        self.thumb.clear()
        self.thumb.setText("Превью")

    def _on_format_clicked(self, index: int) -> None:
        if 0 <= index < len(FORMAT_PRESETS):
            self.format_changed.emit(FORMAT_PRESETS[index].key)

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
