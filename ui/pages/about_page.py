from PySide6.QtCore import QUrl
from PySide6.QtGui import QDesktopServices
from PySide6.QtWidgets import QFrame, QLabel, QPushButton, QVBoxLayout, QWidget

from core import APP_VERSION


class AboutPage(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        root = QVBoxLayout(self)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(16)

        title = QLabel("Video Downloader Pro")
        title.setObjectName("PageTitle")
        subtitle = QLabel(f"Версия {APP_VERSION}")
        subtitle.setObjectName("PageSubtitle")

        card = QFrame()
        card.setObjectName("Card")
        layout = QVBoxLayout(card)
        layout.setContentsMargins(18, 18, 18, 18)
        layout.setSpacing(12)
        coded = QLabel("Coded by Jacksony")
        coded.setObjectName("SectionTitle")
        body = QLabel(
            "Десктопное приложение на Python + PySide6 для загрузки видео и аудио через управляемый runtime yt-dlp/ffmpeg.\n\n"
            "Соблюдайте законы, авторские права и правила платформ. Приложение не обходит возрастные, региональные или платные ограничения."
        )
        body.setObjectName("MutedText")
        body.setWordWrap(True)
        github = QPushButton("GitHub")
        github.setObjectName("SecondaryButton")
        telegram = QPushButton("Telegram")
        telegram.setObjectName("SecondaryButton")
        github.clicked.connect(lambda: QDesktopServices.openUrl(QUrl("https://github.com/Jacksony100/Youtube-Downloader")))
        telegram.clicked.connect(lambda: QDesktopServices.openUrl(QUrl("https://t.me/Smesharik_lair")))
        layout.addWidget(coded)
        layout.addWidget(body)
        layout.addWidget(github)
        layout.addWidget(telegram)
        layout.addStretch()

        root.addWidget(title)
        root.addWidget(subtitle)
        root.addWidget(card, 1)
