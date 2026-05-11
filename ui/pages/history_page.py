from __future__ import annotations

from PySide6.QtCore import Signal
from PySide6.QtWidgets import QFrame, QHBoxLayout, QLabel, QLineEdit, QPushButton, QScrollArea, QVBoxLayout, QWidget

from core.models import DownloadRecord
from ui.widgets import EmptyState


class HistoryRow(QFrame):
    open_file_requested = Signal(str)
    open_folder_requested = Signal(str)
    retry_requested = Signal(str)
    delete_requested = Signal(str)

    def __init__(self, record: DownloadRecord, parent=None):
        super().__init__(parent)
        self.record = record
        self.setObjectName("Card")
        root = QHBoxLayout(self)
        root.setContentsMargins(14, 12, 14, 12)
        root.setSpacing(12)

        info = QVBoxLayout()
        title = QLabel(record.title or "Без названия")
        title.setObjectName("CardTitle")
        title.setWordWrap(True)
        meta = QLabel(f"{record.format_label} • {record.status} • {record.finished_at or record.created_at}")
        meta.setObjectName("CardMeta")
        path = QLabel(record.output_path or record.url)
        path.setObjectName("MutedText")
        path.setWordWrap(True)
        info.addWidget(title)
        info.addWidget(meta)
        info.addWidget(path)

        open_file = QPushButton("Файл")
        open_file.setObjectName("SecondaryButton")
        open_folder = QPushButton("Папка")
        open_folder.setObjectName("SecondaryButton")
        retry = QPushButton("Скачать заново")
        retry.setObjectName("SecondaryButton")
        delete = QPushButton("Удалить")
        delete.setObjectName("DangerButton")

        open_file.clicked.connect(lambda: self.open_file_requested.emit(record.output_path))
        open_folder.clicked.connect(lambda: self.open_folder_requested.emit(record.output_path))
        retry.clicked.connect(lambda: self.retry_requested.emit(record.id))
        delete.clicked.connect(lambda: self.delete_requested.emit(record.id))

        buttons = QVBoxLayout()
        for button in (open_file, open_folder, retry, delete):
            button.setMinimumHeight(30)
            buttons.addWidget(button)
        buttons.addStretch()

        root.addLayout(info, 1)
        root.addLayout(buttons)


class HistoryPage(QWidget):
    search_changed = Signal(str)
    open_file_requested = Signal(str)
    open_folder_requested = Signal(str)
    retry_requested = Signal(str)
    delete_requested = Signal(str)

    def __init__(self, parent=None):
        super().__init__(parent)
        root = QVBoxLayout(self)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(16)

        title = QLabel("История")
        title.setObjectName("PageTitle")
        subtitle = QLabel("Завершённые, отменённые и неудачные загрузки.")
        subtitle.setObjectName("PageSubtitle")
        self.search = QLineEdit()
        self.search.setObjectName("Input")
        self.search.setPlaceholderText("Поиск по названию, автору или ссылке")
        self.search.setMinimumHeight(42)

        self.scroll = QScrollArea()
        self.scroll.setObjectName("DownloadsScroll")
        self.scroll.setWidgetResizable(True)
        scroll_widget = QWidget()
        self.list_layout = QVBoxLayout(scroll_widget)
        self.list_layout.setContentsMargins(0, 0, 0, 0)
        self.list_layout.setSpacing(10)
        self.empty_state = EmptyState("История пуста", "После загрузки файлы появятся в этом списке.")
        self.list_layout.addWidget(self.empty_state)
        self.list_layout.addStretch()
        self.scroll.setWidget(scroll_widget)

        root.addWidget(title)
        root.addWidget(subtitle)
        root.addWidget(self.search)
        root.addWidget(self.scroll, 1)

        self.search.textChanged.connect(self.search_changed.emit)

    def set_records(self, records: list[DownloadRecord]) -> None:
        while self.list_layout.count() > 0:
            item = self.list_layout.takeAt(0)
            widget = item.widget()
            if widget:
                widget.deleteLater()

        if not records:
            self.empty_state = EmptyState("История пуста", "Здесь появятся завершённые загрузки.")
            self.list_layout.addWidget(self.empty_state)
        else:
            for record in records:
                row = HistoryRow(record)
                row.open_file_requested.connect(self.open_file_requested.emit)
                row.open_folder_requested.connect(self.open_folder_requested.emit)
                row.retry_requested.connect(self.retry_requested.emit)
                row.delete_requested.connect(self.delete_requested.emit)
                self.list_layout.addWidget(row)
        self.list_layout.addStretch()
