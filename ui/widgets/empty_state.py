from PySide6.QtCore import Qt
from PySide6.QtWidgets import QFrame, QLabel, QVBoxLayout


class EmptyState(QFrame):
    def __init__(self, title: str, text: str, parent=None):
        super().__init__(parent)
        self.setObjectName("EmptyState")
        layout = QVBoxLayout(self)
        layout.setContentsMargins(24, 28, 24, 28)
        layout.setSpacing(8)

        self.title_label = QLabel(title)
        self.title_label.setObjectName("EmptyStateTitle")
        self.title_label.setAlignment(Qt.AlignCenter)

        self.text_label = QLabel(text)
        self.text_label.setObjectName("EmptyStateText")
        self.text_label.setAlignment(Qt.AlignCenter)
        self.text_label.setWordWrap(True)

        layout.addWidget(self.title_label)
        layout.addWidget(self.text_label)
