from PySide6.QtCore import Signal
from PySide6.QtWidgets import QFrame, QLabel, QPushButton, QVBoxLayout


class Sidebar(QFrame):
    page_requested = Signal(str)

    PAGES = [
        ("downloads", "Загрузки"),
        ("history", "История"),
        ("tools", "Инструменты"),
        ("settings", "Настройки"),
        ("about", "О приложении"),
    ]

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setObjectName("Sidebar")
        self.buttons: dict[str, QPushButton] = {}

        layout = QVBoxLayout(self)
        layout.setContentsMargins(14, 18, 14, 18)
        layout.setSpacing(8)

        title = QLabel("Video\nDownloader Pro")
        title.setObjectName("SidebarTitle")
        layout.addWidget(title)
        layout.addSpacing(18)

        for key, label in self.PAGES:
            button = QPushButton(label)
            button.setObjectName("SidebarButton")
            button.setMinimumHeight(42)
            button.clicked.connect(lambda checked=False, page=key: self.page_requested.emit(page))
            self.buttons[key] = button
            layout.addWidget(button)

        layout.addStretch()

        footer = QLabel("Coded by Jacksony")
        footer.setObjectName("SidebarFooter")
        layout.addWidget(footer)

    def set_active(self, page_key: str) -> None:
        for key, button in self.buttons.items():
            button.setProperty("active", key == page_key)
            button.setObjectName("SidebarButtonActive" if key == page_key else "SidebarButton")
            button.style().unpolish(button)
            button.style().polish(button)
