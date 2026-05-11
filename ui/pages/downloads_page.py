from __future__ import annotations

from PySide6.QtCore import Qt, Signal
from PySide6.QtWidgets import (
    QFrame,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QPushButton,
    QScrollArea,
    QVBoxLayout,
    QWidget,
)

from core.models import VideoMetadata
from ui.widgets import EmptyState, PreviewCard, StatChip


class DownloadsPage(QWidget):
    paste_requested = Signal()
    check_requested = Signal(str)
    add_requested = Signal(str)
    browse_requested = Signal()
    open_folder_requested = Signal()
    clear_finished_requested = Signal()
    cancel_all_requested = Signal()
    onyshop_requested = Signal()
    onyshop_help_requested = Signal()
    format_changed = Signal(str)
    parallel_changed = Signal(int)
    auto_open_changed = Signal(bool)

    def __init__(self, parent=None):
        super().__init__(parent)
        root = QVBoxLayout(self)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(16)

        header = QHBoxLayout()
        title_col = QVBoxLayout()
        title_col.setSpacing(4)
        title = QLabel("Загрузки")
        title.setObjectName("PageTitle")
        subtitle = QLabel("Вставьте ссылку, выберите формат и добавьте видео в очередь.")
        subtitle.setObjectName("PageSubtitle")
        title_col.addWidget(title)
        title_col.addWidget(subtitle)
        self.ytdlp_chip = StatChip("yt-dlp", "проверка")
        self.ffmpeg_chip = StatChip("ffmpeg", "проверка")
        header.addLayout(title_col, 1)
        header.addWidget(self.ytdlp_chip)
        header.addWidget(self.ffmpeg_chip)

        input_card = QFrame()
        input_card.setObjectName("Card")
        input_layout = QVBoxLayout(input_card)
        input_layout.setContentsMargins(18, 18, 18, 18)
        input_layout.setSpacing(12)

        url_label = QLabel("Ссылка на видео")
        url_label.setObjectName("InputLabel")
        row = QHBoxLayout()
        row.setSpacing(8)
        self.url_edit = QLineEdit()
        self.url_edit.setObjectName("Input")
        self.url_edit.setPlaceholderText("https://www.youtube.com/watch?v=...")
        self.url_edit.setMinimumHeight(44)
        self.paste_btn = QPushButton("Вставить")
        self.paste_btn.setObjectName("SecondaryButton")
        self.check_btn = QPushButton("Проверить")
        self.check_btn.setObjectName("SecondaryButton")
        self.add_btn = QPushButton("Добавить в очередь")
        self.add_btn.setObjectName("PrimaryButton")
        for button in (self.paste_btn, self.check_btn, self.add_btn):
            button.setMinimumHeight(44)
        row.addWidget(self.url_edit, 1)
        row.addWidget(self.paste_btn)
        row.addWidget(self.check_btn)
        row.addWidget(self.add_btn)
        input_layout.addWidget(url_label)
        input_layout.addLayout(row)

        self.preview = PreviewCard()
        self.preview.setVisible(False)

        queue_header = QHBoxLayout()
        queue_title = QLabel("Очередь")
        queue_title.setObjectName("SectionTitle")
        self.queue_count = QLabel("0 активных")
        self.queue_count.setObjectName("MutedText")
        self.clear_btn = QPushButton("Очистить завершённые")
        self.clear_btn.setObjectName("SecondaryButton")
        self.cancel_all_btn = QPushButton("Отменить все")
        self.cancel_all_btn.setObjectName("DangerButton")
        queue_header.addWidget(queue_title)
        queue_header.addWidget(self.queue_count)
        queue_header.addStretch()
        queue_header.addWidget(self.clear_btn)
        queue_header.addWidget(self.cancel_all_btn)

        self.scroll = QScrollArea()
        self.scroll.setObjectName("DownloadsScroll")
        self.scroll.setWidgetResizable(True)
        self.scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarAlwaysOff)
        scroll_widget = QWidget()
        self.cards_layout = QVBoxLayout(scroll_widget)
        self.cards_layout.setContentsMargins(0, 0, 0, 0)
        self.cards_layout.setSpacing(10)
        self.empty_state = EmptyState("Очередь пуста", "Добавленные видео появятся здесь.")
        self.cards_layout.addWidget(self.empty_state)
        self.cards_layout.addStretch()
        self.scroll.setWidget(scroll_widget)

        notice = QFrame()
        notice.setObjectName("NoticeCard")
        notice_layout = QHBoxLayout(notice)
        notice_layout.setContentsMargins(16, 12, 16, 12)
        notice_layout.setSpacing(12)
        notice_text = QVBoxLayout()
        notice_title = QLabel("Видео не грузится?")
        notice_title.setObjectName("NoticeTitle")
        notice_body = QLabel("Иногда причина в сетевых ограничениях. Можно попробовать безопасный интернет-маршрут (VPN) от onyshop.tech.")
        notice_body.setObjectName("NoticeText")
        notice_body.setWordWrap(True)
        notice_text.addWidget(notice_title)
        notice_text.addWidget(notice_body)
        self.onyshop_btn = QPushButton("Открыть onyshop.tech")
        self.onyshop_btn.setObjectName("SecondaryButton")
        self.onyshop_help_btn = QPushButton("Как это поможет?")
        self.onyshop_help_btn.setObjectName("SecondaryButton")
        notice_layout.addLayout(notice_text, 1)
        notice_layout.addWidget(self.onyshop_btn)
        notice_layout.addWidget(self.onyshop_help_btn)

        root.addLayout(header)
        root.addWidget(input_card)
        root.addWidget(self.preview)
        root.addLayout(queue_header)
        root.addWidget(self.scroll, 1)
        root.addWidget(notice)

        self.paste_btn.clicked.connect(self.paste_requested.emit)
        self.check_btn.clicked.connect(lambda: self.check_requested.emit(self.url_edit.text().strip()))
        self.add_btn.clicked.connect(lambda: self.add_requested.emit(self.url_edit.text().strip()))
        self.url_edit.returnPressed.connect(lambda: self.add_requested.emit(self.url_edit.text().strip()))
        self.clear_btn.clicked.connect(self.clear_finished_requested.emit)
        self.cancel_all_btn.clicked.connect(self.cancel_all_requested.emit)
        self.onyshop_btn.clicked.connect(self.onyshop_requested.emit)
        self.onyshop_help_btn.clicked.connect(self.onyshop_help_requested.emit)
        self.preview.browse_requested.connect(self.browse_requested.emit)
        self.preview.format_changed.connect(self.format_changed.emit)
        self.preview.parallel_changed.connect(self.parallel_changed.emit)
        self.preview.auto_open_changed.connect(self.auto_open_changed.emit)

    def set_checking(self, checking: bool) -> None:
        self.check_btn.setEnabled(not checking)
        self.check_btn.setText("Проверяем..." if checking else "Проверить")

    def show_preview(self, metadata: VideoMetadata) -> None:
        self.preview.setVisible(True)
        self.preview.set_metadata(metadata)

    def update_tool_status(self, ytdlp: str, ffmpeg: str) -> None:
        self.ytdlp_chip.set_value(ytdlp)
        self.ffmpeg_chip.set_value(ffmpeg)

    def update_queue_state(self, active: int, queued: int, has_cards: bool) -> None:
        self.queue_count.setText(f"{active} активных • {queued} в очереди")
        self.empty_state.setVisible(not has_cards)

    def add_download_card(self, card: QWidget) -> None:
        self.cards_layout.insertWidget(0, card)
        self.empty_state.setVisible(False)
