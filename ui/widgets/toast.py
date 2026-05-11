from PySide6.QtCore import QTimer
from PySide6.QtWidgets import QFrame, QLabel, QHBoxLayout


class Toast(QFrame):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setObjectName("Toast")
        self.setVisible(False)
        layout = QHBoxLayout(self)
        layout.setContentsMargins(14, 10, 14, 10)
        self.label = QLabel("")
        self.label.setObjectName("ToastText")
        self.label.setWordWrap(True)
        layout.addWidget(self.label)
        self._timer = QTimer(self)
        self._timer.setSingleShot(True)
        self._timer.timeout.connect(self.hide)

    def show_message(self, message: str, timeout_ms: int = 4200) -> None:
        self.label.setText(message)
        self.setVisible(True)
        self.raise_()
        self._timer.start(timeout_ms)
