from __future__ import annotations

import urllib.request

from PySide6.QtCore import QThread, Signal


class ThumbnailThread(QThread):
    loaded = Signal(bytes)

    def __init__(self, url: str):
        super().__init__()
        self.url = url

    def run(self) -> None:
        if not self.url:
            return
        try:
            request = urllib.request.Request(self.url, headers={"User-Agent": "VideoDownloaderPro/3.1"})
            with urllib.request.urlopen(request, timeout=8) as response:
                self.loaded.emit(response.read(2_000_000))
        except Exception:
            return
