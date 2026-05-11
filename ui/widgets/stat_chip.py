from PySide6.QtCore import Qt
from PySide6.QtWidgets import QFrame, QHBoxLayout, QLabel


class StatChip(QFrame):
    def __init__(self, title: str, value: str = "", parent=None):
        super().__init__(parent)
        self.setObjectName("StatChip")
        layout = QHBoxLayout(self)
        layout.setContentsMargins(12, 6, 12, 6)
        layout.setSpacing(8)

        self.title_label = QLabel(title)
        self.title_label.setObjectName("StatChipTitle")
        self.value_label = QLabel(value)
        self.value_label.setObjectName("StatChipValue")
        self.value_label.setAlignment(Qt.AlignRight | Qt.AlignVCenter)

        layout.addWidget(self.title_label)
        layout.addWidget(self.value_label)

    def set_value(self, value: str) -> None:
        self.value_label.setText(value)
