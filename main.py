import sys
import os
import urllib.request
from pathlib import Path
from datetime import datetime
import json
import base64

import yt_dlp
from yt_dlp.utils import DownloadError

from PySide6.QtCore import (Qt, QThread, Signal, QTimer, QPropertyAnimation, 
                           QEasingCurve, Property, QByteArray, QParallelAnimationGroup)
from PySide6.QtGui import QFont, QPixmap, QIcon, QColor, QPalette, QFontDatabase
from PySide6.QtWidgets import (
    QApplication, QMainWindow, QWidget,
    QVBoxLayout, QHBoxLayout,
    QLabel, QLineEdit, QPushButton, QComboBox,
    QFrame, QMessageBox, QListWidget, QListWidgetItem,
    QProgressBar, QSizePolicy, QFileDialog, QScrollArea,
    QGraphicsDropShadowEffect, QGraphicsOpacityEffect
)

# Встроенный шрифт Montserrat (Base64)
MONTSERRAT_FONT = """
# Здесь будет base64 шрифта, но для упрощения используем системный
"""

# ------------------- Поток загрузки -------------------

class DownloadThread(QThread):
    progress = Signal(float, str, float)  # percent, text, speed_mbps
    finished = Signal(str, bool, str)     # message, success_flag, filepath
    info_ready = Signal(dict)              # video info

    def __init__(self, url: str, fmt: str, out_dir: str, extract_audio: bool = False):
        super().__init__()
        self.url = url
        self.fmt = fmt
        self.out_dir = out_dir
        self.extract_audio = extract_audio
        self._cancelled = False
        self.output_filepath = ""

    def cancel(self):
        self._cancelled = True

    def run(self):
        try:
            opts = {
                "outtmpl": os.path.join(self.out_dir, "%(title)s.%(ext)s"),
                "noplaylist": True,
                "quiet": True,
                "no_warnings": True,
                "format": self.fmt,
                "progress_hooks": [self._hook],
            }

            if self.extract_audio:
                opts.update({
                    "postprocessors": [{
                        "key": "FFmpegExtractAudio",
                        "preferredcodec": "mp3",
                        "preferredquality": "192",
                    }],
                    "format": "bestaudio/best",
                })
            else:
                opts["merge_output_format"] = "mp4"

            with yt_dlp.YoutubeDL(opts) as ydl:
                info = ydl.extract_info(self.url, download=False)
                self.info_ready.emit(info)
                
                result = ydl.extract_info(self.url, download=True)
                
                if result:
                    filename = ydl.prepare_filename(result)
                    if self.extract_audio:
                        filename = os.path.splitext(filename)[0] + ".mp3"
                    self.output_filepath = filename

            if self._cancelled:
                self.finished.emit("⛔ Загрузка отменена", False, "")
            else:
                self.finished.emit("✅ Загрузка завершена", True, self.output_filepath)
                
        except DownloadError as e:
            if "Cancelled by user" in str(e):
                self.finished.emit("⛔ Загрузка отменена", False, "")
            else:
                self.finished.emit(f"❌ Ошибка: {e}", False, "")
        except Exception as e:
            self.finished.emit(f"❌ Ошибка: {e}", False, "")

    def _hook(self, d: dict):
        if self._cancelled:
            raise DownloadError("Cancelled by user")

        if d.get("status") == "downloading":
            total = d.get("total_bytes") or d.get("total_bytes_estimate") or 0
            downloaded = d.get("downloaded_bytes", 0)
            
            if total:
                percent = downloaded / total * 100.0
            else:
                pstr = d.get("_percent_str", "").strip().rstrip("%")
                try:
                    percent = float(pstr)
                except Exception:
                    percent = 0.0

            speed = d.get("speed", 0)
            speed_mbps = (speed * 8 / 1_000_000) if speed else 0

            percent_str = d.get("_percent_str", "").strip()
            speed_str = d.get("_speed_str", "").strip()
            eta_str = d.get("_eta_str", "").strip()
            
            parts = [percent_str]
            if speed_str:
                parts.append(speed_str)
            if eta_str and eta_str != "Unknown ETA":
                parts.append(f"ETA {eta_str}")
                
            text = "  •  ".join(parts)
            self.progress.emit(percent, text, speed_mbps)


# ------------------- Поток получения информации -------------------

class InfoThread(QThread):
    info_ready = Signal(dict)
    error = Signal(str)

    def __init__(self, url: str):
        super().__init__()
        self.url = url

    def run(self):
        try:
            opts = {
                "quiet": True,
                "no_warnings": True,
                "skip_download": True,
                "noplaylist": True,
            }
            with yt_dlp.YoutubeDL(opts) as ydl:
                info = ydl.extract_info(self.url, download=False)
                self.info_ready.emit(info)
        except Exception as e:
            self.error.emit(str(e))


# ------------------- Карточка загрузки -------------------

class DownloadCard(QFrame):
    cancel_requested = Signal(object)  # Сигнал для отмены загрузки
    
    def __init__(self, title: str, url: str, thumbnail_url: str = None):
        super().__init__()
        self.title = title
        self.url = url
        self.thumbnail_url = thumbnail_url
        self.download_thread = None
        
        self.setObjectName("downloadCard")
        self.setup_ui()
        self.setup_animations()
        self.load_thumbnail()
        
    def setup_ui(self):
        layout = QHBoxLayout(self)
        layout.setContentsMargins(16, 16, 16, 16)
        layout.setSpacing(16)
        
        # Миниатюра
        self.thumb_label = QLabel()
        self.thumb_label.setObjectName("cardThumb")
        self.thumb_label.setFixedSize(120, 68)
        self.thumb_label.setAlignment(Qt.AlignCenter)
        self.thumb_label.setText("📹")
        self.thumb_label.setStyleSheet("""
            QLabel#cardThumb {
                background-color: #1a1a1a;
                border-radius: 8px;
                color: #666;
                font-size: 32px;
            }
        """)
        
        # Информация и прогресс
        info_layout = QVBoxLayout()
        info_layout.setSpacing(8)
        
        self.title_label = QLabel(self.title)
        self.title_label.setObjectName("cardTitle")
        self.title_label.setWordWrap(True)
        self.title_label.setMaximumWidth(500)
        
        self.status_label = QLabel("⏳ В очереди...")
        self.status_label.setObjectName("cardStatus")
        
        self.progress_bar = QProgressBar()
        self.progress_bar.setObjectName("cardProgress")
        self.progress_bar.setRange(0, 100)
        self.progress_bar.setValue(0)
        self.progress_bar.setFixedHeight(8)
        self.progress_bar.setTextVisible(False)
        
        self.speed_label = QLabel("⚡ 0 Мбит/с")
        self.speed_label.setObjectName("cardSpeed")
        
        info_layout.addWidget(self.title_label)
        info_layout.addWidget(self.status_label)
        info_layout.addWidget(self.progress_bar)
        info_layout.addWidget(self.speed_label)
        
        # Кнопка отмены
        self.cancel_btn = QPushButton("✕")
        self.cancel_btn.setObjectName("cardCancelButton")
        self.cancel_btn.setFixedSize(36, 36)
        self.cancel_btn.setToolTip("Отменить загрузку")
        self.cancel_btn.clicked.connect(self.on_cancel_clicked)
        
        layout.addWidget(self.thumb_label)
        layout.addLayout(info_layout, 1)
        layout.addWidget(self.cancel_btn, 0, Qt.AlignTop)
        
    def setup_animations(self):
        """Настройка анимации появления"""
        self.opacity_effect = QGraphicsOpacityEffect(self)
        self.setGraphicsEffect(self.opacity_effect)
        
        self.fade_in = QPropertyAnimation(self.opacity_effect, b"opacity")
        self.fade_in.setDuration(400)
        self.fade_in.setStartValue(0.0)
        self.fade_in.setEndValue(1.0)
        self.fade_in.setEasingCurve(QEasingCurve.OutCubic)
        self.fade_in.start()
        
    def load_thumbnail(self):
        """Загрузка миниатюры"""
        if self.thumbnail_url:
            try:
                data = urllib.request.urlopen(self.thumbnail_url, timeout=5).read()
                pixmap = QPixmap()
                if pixmap.loadFromData(data):
                    scaled = pixmap.scaled(120, 68, Qt.KeepAspectRatio, Qt.SmoothTransformation)
                    self.thumb_label.setPixmap(scaled)
                    self.thumb_label.setText("")
            except Exception:
                pass
    
    def update_progress(self, percent: float, text: str, speed_mbps: float):
        """Обновление прогресса"""
        self.progress_bar.setValue(int(percent))
        self.status_label.setText("⬇️ Загружаю...")
        if speed_mbps > 0:
            self.speed_label.setText(f"⚡ {speed_mbps:.1f} Мбит/с")
    
    def set_finished(self, success: bool, message: str):
        """Установка завершения"""
        self.progress_bar.setValue(100 if success else 0)
        self.status_label.setText(message)
        self.speed_label.setText("✓ Завершено" if success else "✕ Ошибка")
        self.cancel_btn.setEnabled(False)
        self.cancel_btn.setVisible(False)
        
    def on_cancel_clicked(self):
        """Обработка нажатия кнопки отмены"""
        self.cancel_requested.emit(self)


# ------------------- Главное окно -------------------

class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()

        self.setWindowTitle("Video Downloader Pro")
        self.setMinimumSize(1100, 720)
        self.resize(1200, 800)

        # Загрузка шрифта Montserrat
        self.load_custom_font()

        # Переменные состояния
        self.active_downloads = {}  # URL -> (DownloadCard, DownloadThread)
        self.default_folder = str(Path.home() / "Downloads")
        self.history_file = Path.home() / ".video_downloader_history.json"
        self.download_history = self.load_history()
        
        self.total_downloads = 0
        self.successful_downloads = 0
        
        self.setup_ui()
        self._connect_signals()
        self._apply_style()
        self.start_animations()
        
    def load_custom_font(self):
        """Загрузка пользовательского шрифта"""
        # Пытаемся загрузить Montserrat, если недоступен - используем Segoe UI
        font_id = QFontDatabase.addApplicationFont(":/fonts/Montserrat-Regular.ttf")
        if font_id == -1:
            # Шрифт не найден, используем системный
            self.app_font = QFont("Segoe UI", 9)
        else:
            families = QFontDatabase.applicationFontFamilies(font_id)
            if families:
                self.app_font = QFont(families[0], 9)
            else:
                self.app_font = QFont("Segoe UI", 9)

    def setup_ui(self):
        """Настройка интерфейса"""
        central = QWidget()
        self.setCentralWidget(central)

        root = QVBoxLayout(central)
        root.setContentsMargins(20, 20, 20, 20)
        root.setSpacing(16)

        # ЗАГОЛОВОК
        header = self.create_header()
        root.addWidget(header)

        # ПАНЕЛЬ УПРАВЛЕНИЯ
        control_panel = self.create_control_panel()
        root.addWidget(control_panel)

        # СТАТИСТИКА
        stats_panel = self.create_stats_panel()
        root.addWidget(stats_panel)

        # АКТИВНЫЕ ЗАГРУЗКИ
        downloads_label = QLabel("📥 Активные загрузки")
        downloads_label.setObjectName("sectionLabel")
        root.addWidget(downloads_label)
        
        # Скролл-область для карточек загрузок
        scroll = QScrollArea()
        scroll.setObjectName("downloadScroll")
        scroll.setWidgetResizable(True)
        scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarAlwaysOff)
        
        scroll_content = QWidget()
        self.downloads_layout = QVBoxLayout(scroll_content)
        self.downloads_layout.setSpacing(12)
        self.downloads_layout.setContentsMargins(0, 0, 0, 0)
        self.downloads_layout.addStretch()
        
        scroll.setWidget(scroll_content)
        root.addWidget(scroll, 1)

        # FOOTER
        footer = self.create_footer()
        root.addWidget(footer)

    def create_header(self):
        """Создание заголовка"""
        header = QFrame()
        header.setObjectName("headerFrame")
        header.setFixedHeight(90)
        
        layout = QHBoxLayout(header)
        layout.setContentsMargins(28, 20, 28, 20)

        left_layout = QVBoxLayout()
        left_layout.setSpacing(6)
        
        title = QLabel("🎬 Video Downloader Pro")
        title.setObjectName("appTitle")
        
        subtitle = QLabel("Скачивайте видео с YouTube, Instagram, TikTok и других платформ")
        subtitle.setObjectName("appSubtitle")
        
        left_layout.addWidget(title)
        left_layout.addWidget(subtitle)

        self.app_status = QLabel("● Готов к работе")
        self.app_status.setObjectName("appStatus")

        layout.addLayout(left_layout, 1)
        layout.addWidget(self.app_status, 0, Qt.AlignRight | Qt.AlignVCenter)

        return header

    def create_control_panel(self):
        """Создание панели управления"""
        panel = QFrame()
        panel.setObjectName("controlPanel")
        
        layout = QVBoxLayout(panel)
        layout.setContentsMargins(28, 24, 28, 24)
        layout.setSpacing(18)

        # URL ввод
        url_label = QLabel("Ссылка на видео")
        url_label.setObjectName("sectionLabel")
        
        url_row = QHBoxLayout()
        url_row.setSpacing(12)

        self.url_edit = QLineEdit()
        self.url_edit.setObjectName("urlEdit")
        self.url_edit.setPlaceholderText("https://www.youtube.com/watch?v=...")
        self.url_edit.setMinimumHeight(48)

        self.paste_btn = QPushButton("📋")
        self.paste_btn.setObjectName("iconButton")
        self.paste_btn.setFixedSize(48, 48)
        self.paste_btn.setToolTip("Вставить из буфера")

        url_row.addWidget(self.url_edit)
        url_row.addWidget(self.paste_btn)

        # Настройки
        settings_row = QHBoxLayout()
        settings_row.setSpacing(16)

        format_layout = QVBoxLayout()
        format_layout.setSpacing(8)
        
        format_label = QLabel("Качество")
        format_label.setObjectName("inputLabel")
        
        self.format_box = QComboBox()
        self.format_box.setObjectName("comboBox")
        self.format_box.setMinimumHeight(42)
        self.format_box.addItem("🎥 Видео — Лучшее качество", "bestvideo+bestaudio/best")
        self.format_box.addItem("🎥 Видео — 1080p", "bestvideo[height<=1080]+bestaudio/best")
        self.format_box.addItem("🎥 Видео — 720p", "bestvideo[height<=720]+bestaudio/best")
        self.format_box.addItem("🎥 Видео — 480p", "bestvideo[height<=480]+bestaudio/best")
        self.format_box.addItem("🎵 Только аудио (MP3)", "bestaudio/best")
        
        format_layout.addWidget(format_label)
        format_layout.addWidget(self.format_box)

        folder_layout = QVBoxLayout()
        folder_layout.setSpacing(8)
        
        folder_label = QLabel("Папка сохранения")
        folder_label.setObjectName("inputLabel")
        
        folder_row = QHBoxLayout()
        folder_row.setSpacing(10)
        
        self.folder_edit = QLineEdit()
        self.folder_edit.setObjectName("folderEdit")
        self.folder_edit.setMinimumHeight(42)
        self.folder_edit.setText(self.default_folder)
        self.folder_edit.setReadOnly(True)
        
        self.browse_btn = QPushButton("📁")
        self.browse_btn.setObjectName("iconButton")
        self.browse_btn.setFixedSize(42, 42)
        self.browse_btn.setToolTip("Выбрать папку")
        
        folder_row.addWidget(self.folder_edit)
        folder_row.addWidget(self.browse_btn)
        
        folder_layout.addWidget(folder_label)
        folder_layout.addLayout(folder_row)

        settings_row.addLayout(format_layout, 1)
        settings_row.addLayout(folder_layout, 2)

        # Кнопка загрузки
        self.download_btn = QPushButton("Скачать")
        self.download_btn.setObjectName("primaryButton")
        self.download_btn.setMinimumHeight(52)

        layout.addWidget(url_label)
        layout.addLayout(url_row)
        layout.addLayout(settings_row)
        layout.addWidget(self.download_btn)

        return panel

    def create_stats_panel(self):
        """Создание панели статистики"""
        panel = QFrame()
        panel.setObjectName("statsPanel")
        
        layout = QHBoxLayout(panel)
        layout.setContentsMargins(24, 14, 24, 14)
        layout.setSpacing(24)

        self.stats_total = QLabel("📊 Всего: 0")
        self.stats_total.setObjectName("statsLabel")

        self.stats_success = QLabel("✅ Успешно: 0")
        self.stats_success.setObjectName("statsLabel")

        self.stats_active = QLabel("⬇️ Активных: 0")
        self.stats_active.setObjectName("statsLabel")

        layout.addWidget(self.stats_total)
        layout.addWidget(self.stats_success)
        layout.addWidget(self.stats_active)
        layout.addStretch()

        return panel

    def create_footer(self):
        """Создание футера"""
        footer = QFrame()
        footer.setObjectName("footerFrame")
        footer.setFixedHeight(50)
        
        layout = QHBoxLayout(footer)
        layout.setContentsMargins(20, 10, 20, 10)
        
        coded_label = QLabel("Coded by Jacksony")
        coded_label.setObjectName("footerLabel")
        
        tg_btn = QPushButton("📱 Telegram")
        tg_btn.setObjectName("footerButton")
        tg_btn.setFixedHeight(32)
        tg_btn.setCursor(Qt.PointingHandCursor)
        tg_btn.clicked.connect(lambda: self.open_url("https://t.me/Smesharik_lair"))
        
        layout.addWidget(coded_label)
        layout.addStretch()
        layout.addWidget(tg_btn)
        
        return footer

    def _connect_signals(self):
        """Подключение сигналов"""
        self.download_btn.clicked.connect(self.start_download)
        self.browse_btn.clicked.connect(self.browse_folder)
        self.paste_btn.clicked.connect(self.paste_url)
        self.url_edit.returnPressed.connect(self.start_download)

    # --------- Анимации ---------

    def start_animations(self):
        """Запуск фоновых анимаций"""
        self.anim_timer = QTimer()
        self.anim_timer.timeout.connect(self.update_status_animation)
        self.anim_timer.start(1000)
        self.anim_phase = 0

    def update_status_animation(self):
        """Анимация статуса"""
        if len(self.active_downloads) == 0:
            dots = "." * (self.anim_phase % 4)
            self.app_status.setText(f"● Готов к работе{dots}")
            self.anim_phase += 1

    # --------- Работа с папками ---------

    def browse_folder(self):
        """Выбор папки для сохранения"""
        folder = QFileDialog.getExistingDirectory(
            self,
            "Выберите папку для сохранения",
            self.folder_edit.text()
        )
        if folder:
            self.folder_edit.setText(folder)

    def paste_url(self):
        """Вставить URL из буфера обмена"""
        clipboard = QApplication.clipboard()
        text = clipboard.text().strip()
        if text:
            self.url_edit.setText(text)
            self.url_edit.setFocus()

    def open_url(self, url: str):
        """Открыть URL в браузере"""
        import webbrowser
        webbrowser.open(url)

    # --------- Загрузка ---------

    def start_download(self):
        """Начать загрузку"""
        url = self.url_edit.text().strip()
        if not url:
            QMessageBox.warning(self, "Ошибка", "Введите ссылку на видео")
            return

        folder = self.folder_edit.text()
        if not os.path.isdir(folder):
            QMessageBox.warning(self, "Ошибка", f"Папка не существует:\n{folder}")
            return

        if url in self.active_downloads:
            QMessageBox.information(self, "Загрузка", "Это видео уже загружается")
            return

        fmt = self.format_box.currentData()
        extract_audio = "bestaudio" in fmt and "mp3" in self.format_box.currentText().lower()

        # Создаем карточку загрузки
        card = DownloadCard("Получение информации...", url)
        card.cancel_requested.connect(self.cancel_download)
        
        # Вставляем карточку в начало списка (перед stretch)
        self.downloads_layout.insertWidget(0, card)

        # Создаем поток загрузки
        thread = DownloadThread(url, fmt, folder, extract_audio)
        thread.progress.connect(lambda p, t, s: self.on_progress(url, p, t, s))
        thread.finished.connect(lambda m, suc, f: self.on_finished(url, m, suc, f))
        thread.info_ready.connect(lambda info: self.on_info_ready(url, info))
        thread.start()

        self.active_downloads[url] = (card, thread)
        
        self.total_downloads += 1
        self.update_stats()
        
        # Очищаем поле ввода
        self.url_edit.clear()

    def cancel_download(self, card: DownloadCard):
        """Отменить загрузку"""
        url = card.url
        if url in self.active_downloads:
            _, thread = self.active_downloads[url]
            thread.cancel()
            card.set_finished(False, "⛔ Отменено")

    def on_info_ready(self, url: str, info: dict):
        """Обработка информации о видео"""
        if url in self.active_downloads:
            card, _ = self.active_downloads[url]
            title = info.get("title", "Без названия")
            thumbnail_url = info.get("thumbnail")
            
            card.title = title
            card.title_label.setText(title)
            card.thumbnail_url = thumbnail_url
            card.load_thumbnail()
            card.status_label.setText("⬇️ Начинаю загрузку...")

    def on_progress(self, url: str, percent: float, text: str, speed_mbps: float):
        """Обновление прогресса"""
        if url in self.active_downloads:
            card, _ = self.active_downloads[url]
            card.update_progress(percent, text, speed_mbps)
            
        self.app_status.setText(f"● Загрузка ({len(self.active_downloads)} активных)")

    def on_finished(self, url: str, message: str, success: bool, filepath: str):
        """Завершение загрузки"""
        if url in self.active_downloads:
            card, thread = self.active_downloads[url]
            card.set_finished(success, message)
            
            if success:
                self.successful_downloads += 1
                
            # Сохраняем в историю
            self.save_to_history({
                "title": card.title,
                "url": url,
                "success": success,
                "timestamp": datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
                "filepath": filepath
            })
            
            # Удаляем из активных через 3 секунды
            QTimer.singleShot(3000, lambda: self.remove_download_card(url))
            
            if success and filepath:
                card.status_label.setText(f"✅ Сохранено: {os.path.basename(filepath)}")
        
        self.update_stats()

    def remove_download_card(self, url: str):
        """Удалить карточку загрузки"""
        if url in self.active_downloads:
            card, _ = self.active_downloads[url]
            
            # Анимация исчезновения
            fade_out = QPropertyAnimation(card.opacity_effect, b"opacity")
            fade_out.setDuration(300)
            fade_out.setStartValue(1.0)
            fade_out.setEndValue(0.0)
            fade_out.finished.connect(lambda: self.finalize_card_removal(url, card))
            fade_out.start()

    def finalize_card_removal(self, url: str, card: DownloadCard):
        """Окончательное удаление карточки"""
        self.downloads_layout.removeWidget(card)
        card.deleteLater()
        del self.active_downloads[url]
        self.update_stats()

    # --------- История ---------

    def load_history(self):
        """Загрузить историю из файла"""
        if self.history_file.exists():
            try:
                with open(self.history_file, 'r', encoding='utf-8') as f:
                    return json.load(f)
            except Exception:
                return []
        return []

    def save_to_history(self, entry: dict):
        """Сохранить запись в историю"""
        self.download_history.insert(0, entry)
        if len(self.download_history) > 100:
            self.download_history = self.download_history[:100]
        
        try:
            with open(self.history_file, 'w', encoding='utf-8') as f:
                json.dump(self.download_history, f, ensure_ascii=False, indent=2)
        except Exception:
            pass

    # --------- Статистика ---------

    def update_stats(self):
        """Обновить статистику"""
        self.stats_total.setText(f"📊 Всего: {self.total_downloads}")
        self.stats_success.setText(f"✅ Успешно: {self.successful_downloads}")
        self.stats_active.setText(f"⬇️ Активных: {len(self.active_downloads)}")

    # --------- Стили ---------

    def _apply_style(self):
        """Применить стили"""
        self.setStyleSheet("""
            /* Основное окно */
            QMainWindow {
                background-color: #000000;
            }

            /* Заголовок */
            QFrame#headerFrame {
                background: qlineargradient(
                    x1:0, y1:0, x2:1, y2:0,
                    stop:0 #0a0a0a,
                    stop:1 #1a1a1a
                );
                border-radius: 16px;
                border: 1px solid #1a1a1a;
            }

            QLabel#appTitle {
                color: #FFFFFF;
                font-size: 24px;
                font-weight: 700;
                font-family: 'Montserrat', 'Segoe UI', sans-serif;
            }

            QLabel#appSubtitle {
                color: #888888;
                font-size: 13px;
                font-family: 'Montserrat', 'Segoe UI', sans-serif;
            }

            QLabel#appStatus {
                color: #00FF7F;
                font-size: 14px;
                font-weight: 600;
                font-family: 'Montserrat', 'Segoe UI', sans-serif;
            }

            /* Панель управления */
            QFrame#controlPanel {
                background-color: #0a0a0a;
                border-radius: 16px;
                border: 1px solid #1a1a1a;
            }

            QLabel#sectionLabel {
                color: #FFFFFF;
                font-size: 15px;
                font-weight: 600;
                font-family: 'Montserrat', 'Segoe UI', sans-serif;
            }

            QLabel#inputLabel {
                color: #888888;
                font-size: 12px;
                font-weight: 500;
                font-family: 'Montserrat', 'Segoe UI', sans-serif;
            }

            /* Поля ввода */
            QLineEdit#urlEdit, QLineEdit#folderEdit {
                background-color: #000000;
                border-radius: 12px;
                border: 2px solid #1a1a1a;
                padding: 12px 18px;
                color: #FFFFFF;
                font-size: 13px;
                font-family: 'Montserrat', 'Segoe UI', sans-serif;
            }

            QLineEdit#urlEdit:focus {
                border: 2px solid #00FF7F;
                background-color: #0a0a0a;
            }

            QLineEdit#folderEdit {
                color: #888888;
            }

            /* Комбобокс */
            QComboBox#comboBox {
                background-color: #000000;
                border-radius: 12px;
                border: 2px solid #1a1a1a;
                padding: 10px 14px;
                color: #FFFFFF;
                font-size: 13px;
                font-family: 'Montserrat', 'Segoe UI', sans-serif;
            }

            QComboBox#comboBox:hover {
                border-color: #00FF7F;
            }

            QComboBox#comboBox::drop-down {
                border: none;
                width: 30px;
            }

            QComboBox#comboBox QAbstractItemView {
                background-color: #0a0a0a;
                border: 1px solid #1a1a1a;
                selection-background-color: #00FF7F;
                selection-color: #000000;
                color: #FFFFFF;
                padding: 6px;
            }

            /* Кнопки */
            QPushButton#primaryButton {
                background: qlineargradient(
                    x1:0, y1:0, x2:1, y2:0,
                    stop:0 #00FF7F,
                    stop:1 #00CC66
                );
                color: #000000;
                border-radius: 14px;
                border: none;
                font-weight: 700;
                font-size: 16px;
                font-family: 'Montserrat', 'Segoe UI', sans-serif;
            }

            QPushButton#primaryButton:hover {
                background: qlineargradient(
                    x1:0, y1:0, x2:1, y2:0,
                    stop:0 #00FF99,
                    stop:1 #00FF7F
                );
            }

            QPushButton#primaryButton:pressed {
                background: #00AA55;
            }

            QPushButton#iconButton {
                background-color: #1a1a1a;
                border-radius: 12px;
                border: 1px solid #2a2a2a;
                font-size: 20px;
            }

            QPushButton#iconButton:hover {
                background-color: #2a2a2a;
                border-color: #00FF7F;
            }

            /* Статистика */
            QFrame#statsPanel {
                background-color: #0a0a0a;
                border-radius: 14px;
                border: 1px solid #1a1a1a;
            }

            QLabel#statsLabel {
                color: #AAAAAA;
                font-size: 13px;
                font-weight: 600;
                font-family: 'Montserrat', 'Segoe UI', sans-serif;
            }

            /* Скролл область */
            QScrollArea#downloadScroll {
                background-color: transparent;
                border: none;
            }

            QScrollArea#downloadScroll QWidget {
                background-color: transparent;
            }

            QScrollBar:vertical {
                background-color: #0a0a0a;
                width: 8px;
                border-radius: 4px;
            }

            QScrollBar::handle:vertical {
                background-color: #2a2a2a;
                border-radius: 4px;
                min-height: 30px;
            }

            QScrollBar::handle:vertical:hover {
                background-color: #3a3a3a;
            }

            /* Карточка загрузки */
            QFrame#downloadCard {
                background-color: #0a0a0a;
                border-radius: 14px;
                border: 1px solid #1a1a1a;
            }

            QLabel#cardTitle {
                color: #FFFFFF;
                font-size: 14px;
                font-weight: 600;
                font-family: 'Montserrat', 'Segoe UI', sans-serif;
            }

            QLabel#cardStatus {
                color: #888888;
                font-size: 12px;
                font-family: 'Montserrat', 'Segoe UI', sans-serif;
            }

            QLabel#cardSpeed {
                color: #00FF7F;
                font-size: 11px;
                font-weight: 600;
                font-family: 'Montserrat', 'Segoe UI', sans-serif;
            }

            QProgressBar#cardProgress {
                background-color: #000000;
                border-radius: 4px;
                border: 1px solid #1a1a1a;
            }

            QProgressBar#cardProgress::chunk {
                border-radius: 3px;
                background: qlineargradient(
                    x1:0, y1:0, x2:1, y2:0,
                    stop:0 #00FF7F,
                    stop:1 #00CC66
                );
            }

            QPushButton#cardCancelButton {
                background-color: #1a0000;
                border-radius: 18px;
                border: 2px solid #FF3333;
                color: #FF6666;
                font-size: 18px;
                font-weight: bold;
            }

            QPushButton#cardCancelButton:hover {
                background-color: #330000;
                border-color: #FF4444;
                color: #FF8888;
            }

            /* Footer */
            QFrame#footerFrame {
                background-color: #0a0a0a;
                border-radius: 12px;
                border: 1px solid #1a1a1a;
            }

            QLabel#footerLabel {
                color: #666666;
                font-size: 12px;
                font-family: 'Montserrat', 'Segoe UI', sans-serif;
            }

            QPushButton#footerButton {
                background-color: #1a1a1a;
                color: #00AAFF;
                border-radius: 8px;
                border: 1px solid #2a2a2a;
                padding: 6px 16px;
                font-size: 11px;
                font-weight: 600;
                font-family: 'Montserrat', 'Segoe UI', sans-serif;
            }

            QPushButton#footerButton:hover {
                background-color: #2a2a2a;
                color: #00CCFF;
                border-color: #00AAFF;
            }
        """)


# ------------------- Запуск -------------------

def main():
    app = QApplication(sys.argv)
    
    # Установка шрифта
    app.setFont(QFont("Montserrat", 9))
    
    # Создание окна
    window = MainWindow()
    window.show()
    
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
