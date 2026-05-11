from __future__ import annotations

from PySide6.QtCore import Signal
from PySide6.QtWidgets import QCheckBox, QComboBox, QFrame, QGridLayout, QLabel, QLineEdit, QPushButton, QSpinBox, QVBoxLayout, QWidget

from core.models import FORMAT_PRESETS


class SettingsPage(QWidget):
    browse_requested = Signal()
    reset_requested = Signal()
    settings_changed = Signal(dict)

    def __init__(self, parent=None):
        super().__init__(parent)
        root = QVBoxLayout(self)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(16)

        title = QLabel("Настройки")
        title.setObjectName("PageTitle")
        subtitle = QLabel("Основные параметры очереди, папки сохранения и поведения после загрузки.")
        subtitle.setObjectName("PageSubtitle")

        card = QFrame()
        card.setObjectName("Card")
        grid = QGridLayout(card)
        grid.setContentsMargins(18, 18, 18, 18)
        grid.setHorizontalSpacing(12)
        grid.setVerticalSpacing(10)

        self.output_dir = QLineEdit()
        self.output_dir.setObjectName("Input")
        self.output_dir.setReadOnly(True)
        self.browse_btn = QPushButton("Выбрать папку")
        self.browse_btn.setObjectName("SecondaryButton")

        self.format_box = QComboBox()
        self.format_box.setObjectName("Input")
        for preset in FORMAT_PRESETS:
            self.format_box.addItem(preset.label, preset.key)

        self.parallel_spin = QSpinBox()
        self.parallel_spin.setObjectName("Input")
        self.parallel_spin.setRange(1, 5)

        self.auto_open = QCheckBox("Авто-открытие файла")
        self.auto_open.setObjectName("CheckBox")
        self.auto_update = QCheckBox("Автообновление yt-dlp/ffmpeg")
        self.auto_update.setObjectName("CheckBox")
        theme = QLabel("Тема: тёмная")
        theme.setObjectName("MutedText")
        self.reset_btn = QPushButton("Сбросить настройки")
        self.reset_btn.setObjectName("DangerButton")

        grid.addWidget(QLabel("Папка загрузок"), 0, 0)
        grid.addWidget(self.output_dir, 1, 0)
        grid.addWidget(self.browse_btn, 1, 1)
        grid.addWidget(QLabel("Формат по умолчанию"), 2, 0)
        grid.addWidget(self.format_box, 3, 0)
        grid.addWidget(QLabel("Параллельность"), 2, 1)
        grid.addWidget(self.parallel_spin, 3, 1)
        grid.addWidget(self.auto_open, 4, 0)
        grid.addWidget(self.auto_update, 4, 1)
        grid.addWidget(theme, 5, 0)
        grid.addWidget(self.reset_btn, 6, 0)
        grid.setColumnStretch(0, 3)
        grid.setColumnStretch(1, 1)

        root.addWidget(title)
        root.addWidget(subtitle)
        root.addWidget(card)
        root.addStretch()

        self.browse_btn.clicked.connect(self.browse_requested.emit)
        self.reset_btn.clicked.connect(self.reset_requested.emit)
        self.format_box.currentIndexChanged.connect(self._emit_changed)
        self.parallel_spin.valueChanged.connect(self._emit_changed)
        self.auto_open.toggled.connect(self._emit_changed)
        self.auto_update.toggled.connect(self._emit_changed)

    def apply_settings(self, values: dict) -> None:
        self.output_dir.setText(values.get("output_dir", ""))
        self.parallel_spin.blockSignals(True)
        self.parallel_spin.setValue(int(values.get("parallel_downloads", 2)))
        self.parallel_spin.blockSignals(False)
        self.auto_open.blockSignals(True)
        self.auto_open.setChecked(bool(values.get("auto_open_file", False)))
        self.auto_open.blockSignals(False)
        self.auto_update.blockSignals(True)
        self.auto_update.setChecked(bool(values.get("auto_update_tools", True)))
        self.auto_update.blockSignals(False)
        format_key = values.get("default_format", "best")
        index = self.format_box.findData(format_key)
        self.format_box.blockSignals(True)
        self.format_box.setCurrentIndex(max(0, index))
        self.format_box.blockSignals(False)

    def set_output_dir(self, folder: str) -> None:
        self.output_dir.setText(folder)
        self._emit_changed()

    def _emit_changed(self) -> None:
        self.settings_changed.emit(
            {
                "output_dir": self.output_dir.text().strip(),
                "default_format": self.format_box.currentData() or "best",
                "parallel_downloads": self.parallel_spin.value(),
                "auto_open_file": self.auto_open.isChecked(),
                "auto_update_tools": self.auto_update.isChecked(),
            }
        )
