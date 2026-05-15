from __future__ import annotations

import sys
import time
from pathlib import Path
from typing import Dict, Optional

from PySide6.QtCore import QTimer, QUrl, QThread, Signal
from PySide6.QtGui import QAction, QDesktopServices, QIcon
from PySide6.QtWidgets import (
    QApplication,
    QFileDialog,
    QHBoxLayout,
    QMainWindow,
    QMessageBox,
    QStackedWidget,
    QVBoxLayout,
    QWidget,
)

from core import APP_TITLE
from core.downloader import DownloadProcessThread, MetadataProcessThread
from core.history import HistoryStore
from core.logger import build_diagnostics, setup_logging
from core.models import DownloadTask, ToolchainStatus, UpdateCheckResult, UpdateResult, VideoMetadata, get_format_preset, utc_now_iso
from core.paths import AppPaths, resource_root
from core.settings import AppSettings
from core.toolchain import ToolchainManager
from core.validators import is_http_url
from ui.pages import AboutPage, DownloadsPage, HistoryPage, SettingsPage, ToolsPage
from ui.sidebar import Sidebar
from ui.widgets import DownloadCard, Toast


ONYSHOP_URL = "https://onyshop.tech"
PREVIEW_TASK_ID = "__preview__"


class ToolchainTaskThread(QThread):
    completed = Signal(str, object)
    failed = Signal(str, str)

    def __init__(self, action: str, paths: AppPaths, force: bool = False):
        super().__init__()
        self.action = action
        self.paths = paths
        self.force = force

    def run(self) -> None:
        manager = ToolchainManager(self.paths)
        try:
            if self.action == "ensure":
                result = manager.ensure_runtime()
            elif self.action == "check":
                result = manager.check_updates(force=self.force)
            elif self.action == "update_ytdlp":
                result = manager.update_ytdlp()
            elif self.action == "update_ffmpeg":
                result = manager.update_ffmpeg()
            elif self.action == "repair":
                result = manager.repair_runtime()
            else:
                result = manager.get_status()
            self.completed.emit(self.action, result)
        except Exception as exc:
            self.failed.emit(self.action, str(exc))


class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.paths = AppPaths.default()
        setup_logging(self.paths)
        self.settings = AppSettings(self.paths.settings_path)
        self.history = HistoryStore(self.paths.history_path)
        self.toolchain = ToolchainManager(self.paths)
        self.toolchain_status: Optional[ToolchainStatus] = None

        self.tasks: Dict[str, DownloadTask] = {}
        self.cards: Dict[str, DownloadCard] = {}
        self.download_threads: Dict[str, DownloadProcessThread] = {}
        self.metadata_threads: Dict[str, MetadataProcessThread] = {}
        self.toolchain_threads: list[ToolchainTaskThread] = []
        self.queue: list[str] = []
        self.running: set[str] = set()
        self.task_counter = 0
        self.preview_metadata: Optional[VideoMetadata] = None
        self._startup_update_check = False
        self._closing = False

        self.setObjectName("AppWindow")
        self.setWindowTitle(APP_TITLE)
        self.setMinimumSize(1100, 720)
        self.resize(1280, 820)
        self._apply_icon()
        self._setup_ui()
        self._setup_menu()
        self._connect_signals()
        self._apply_style()
        self._apply_settings_to_pages()
        self.refresh_history()
        self.switch_page(self.settings.get("active_page", "downloads"))
        app = QApplication.instance()
        if app:
            app.aboutToQuit.connect(self.shutdown_background_threads)
        QTimer.singleShot(150, self.ensure_tools_async)

    def _setup_ui(self) -> None:
        central = QWidget()
        self.setCentralWidget(central)
        root = QHBoxLayout(central)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(0)

        self.sidebar = Sidebar()
        self.sidebar.setFixedWidth(220)
        self.stack = QStackedWidget()

        content = QWidget()
        content_layout = QVBoxLayout(content)
        content_layout.setContentsMargins(24, 22, 24, 18)
        content_layout.setSpacing(12)

        self.downloads_page = DownloadsPage()
        self.history_page = HistoryPage()
        self.tools_page = ToolsPage()
        self.settings_page = SettingsPage()
        self.about_page = AboutPage()

        self.pages = {
            "downloads": self.downloads_page,
            "history": self.history_page,
            "tools": self.tools_page,
            "settings": self.settings_page,
            "about": self.about_page,
        }
        for page in self.pages.values():
            self.stack.addWidget(page)

        self.toast = Toast()
        self.toast.setMaximumHeight(72)

        content_layout.addWidget(self.stack, 1)
        content_layout.addWidget(self.toast)

        root.addWidget(self.sidebar)
        root.addWidget(content, 1)

    def _setup_menu(self) -> None:
        menu = self.menuBar()
        menu.setNativeMenuBar(sys.platform == "darwin")

        file_menu = menu.addMenu("Файл")
        self._add_action(file_menu, "Фокус на ссылке", self.focus_url_input, "Ctrl+L")
        self._add_action(file_menu, "Открыть папку загрузок", self.open_download_folder, "Ctrl+O")
        file_menu.addSeparator()
        self._add_action(file_menu, "Выход", self.close, "Ctrl+Q")

        downloads_menu = menu.addMenu("Загрузки")
        self._add_action(downloads_menu, "Добавить в очередь", lambda: self.add_download(self.downloads_page.url_edit.text().strip()), "Ctrl+D")
        self._add_action(downloads_menu, "Проверить ссылку", lambda: self.check_url(self.downloads_page.url_edit.text().strip()), "Ctrl+I")
        downloads_menu.addSeparator()
        self._add_action(downloads_menu, "Отменить все", self.cancel_all_downloads, "Ctrl+Shift+C")
        self._add_action(downloads_menu, "Очистить завершённые", self.clear_finished_cards, "Ctrl+Shift+X")

        tools_menu = menu.addMenu("Инструменты")
        self._add_action(tools_menu, "Проверить обновления", lambda: self.start_toolchain_task("check", force=True))
        self._add_action(tools_menu, "Открыть runtime-папку", self.open_runtime_folder)
        self._add_action(tools_menu, "Открыть onyshop.tech", self.open_onyshop_site)

        help_menu = menu.addMenu("Справка")
        self._add_action(help_menu, "О приложении", lambda: self.switch_page("about"), "F1")

    def _add_action(self, menu, title: str, callback, shortcut: str = "") -> QAction:
        action = QAction(title, self)
        if shortcut:
            action.setShortcut(shortcut)
        action.triggered.connect(callback)
        menu.addAction(action)
        return action

    def _connect_signals(self) -> None:
        self.sidebar.page_requested.connect(self.switch_page)
        self.downloads_page.paste_requested.connect(self.paste_url)
        self.downloads_page.check_requested.connect(self.check_url)
        self.downloads_page.add_requested.connect(self.add_download)
        self.downloads_page.browse_requested.connect(self.choose_output_dir)
        self.downloads_page.open_folder_requested.connect(self.open_download_folder)
        self.downloads_page.clear_finished_requested.connect(self.clear_finished_cards)
        self.downloads_page.cancel_all_requested.connect(self.cancel_all_downloads)
        self.downloads_page.onyshop_requested.connect(self.open_onyshop_site)
        self.downloads_page.onyshop_help_requested.connect(self.show_onyshop_help)
        self.downloads_page.format_changed.connect(lambda key: self.update_setting("default_format", key))
        self.downloads_page.parallel_changed.connect(lambda value: self.update_setting("parallel_downloads", value))
        self.downloads_page.auto_open_changed.connect(lambda value: self.update_setting("auto_open_file", value))

        self.history_page.search_changed.connect(self.refresh_history)
        self.history_page.open_file_requested.connect(self.open_file)
        self.history_page.open_folder_requested.connect(self.open_containing_folder)
        self.history_page.retry_requested.connect(self.retry_history_record)
        self.history_page.delete_requested.connect(self.delete_history_record)

        self.tools_page.check_updates_requested.connect(lambda: self.start_toolchain_task("check", force=True))
        self.tools_page.update_ytdlp_requested.connect(lambda: self.start_toolchain_task("update_ytdlp"))
        self.tools_page.update_ffmpeg_requested.connect(lambda: self.start_toolchain_task("update_ffmpeg"))
        self.tools_page.open_runtime_requested.connect(self.open_runtime_folder)
        self.tools_page.repair_requested.connect(lambda: self.start_toolchain_task("repair"))
        self.tools_page.copy_diagnostics_requested.connect(self.copy_diagnostics)
        self.tools_page.auto_update_changed.connect(self.set_auto_update_tools)

        self.settings_page.browse_requested.connect(self.choose_output_dir)
        self.settings_page.reset_requested.connect(self.reset_settings)
        self.settings_page.settings_changed.connect(self.apply_settings_update)

    def _apply_style(self) -> None:
        qss_path = resource_root() / "ui" / "styles" / "dark.qss"
        if qss_path.exists():
            self.setStyleSheet(qss_path.read_text(encoding="utf-8"))

    def _apply_icon(self) -> None:
        for candidate in (resource_root() / "icon.ico", resource_root() / "icon.icns"):
            if candidate.exists():
                self.setWindowIcon(QIcon(str(candidate)))
                return

    def _apply_settings_to_pages(self) -> None:
        values = self.settings.as_dict()
        self.downloads_page.apply_settings(values)
        self.downloads_page.preview.apply_settings(
            values["output_dir"],
            values["default_format"],
            values["parallel_downloads"],
            values["auto_open_file"],
        )
        self.settings_page.apply_settings(values)

    def switch_page(self, key: str) -> None:
        if key not in self.pages:
            key = "downloads"
        self.stack.setCurrentWidget(self.pages[key])
        self.sidebar.set_active(key)
        self.settings.set("active_page", key)
        self.settings.save()
        if key == "history":
            self.refresh_history()

    def ensure_tools_async(self) -> None:
        self.downloads_page.update_tool_status("подготовка", "подготовка")
        self.tools_page.append_log("Подготавливаем runtime-инструменты...")
        self.start_toolchain_task("ensure")

    def start_toolchain_task(self, action: str, force: bool = False) -> None:
        thread = ToolchainTaskThread(action, self.paths, force=force)
        thread.completed.connect(self.on_toolchain_task_completed)
        thread.failed.connect(self.on_toolchain_task_failed)
        thread.finished.connect(lambda: self._cleanup_toolchain_thread(thread))
        self.toolchain_threads.append(thread)
        thread.start()

    def _cleanup_toolchain_thread(self, thread: ToolchainTaskThread) -> None:
        if thread in self.toolchain_threads:
            self.toolchain_threads.remove(thread)
        thread.deleteLater()

    def on_toolchain_task_completed(self, action: str, result: object) -> None:
        self.toolchain = ToolchainManager(self.paths)
        status = result if isinstance(result, ToolchainStatus) else self.toolchain.get_status()
        self.toolchain_status = status
        self.update_tool_status_ui(status)

        if isinstance(result, UpdateCheckResult):
            self.tools_page.append_log(result.message or "Проверка обновлений завершена.")
            if result.error:
                self.show_toast(result.error)
            else:
                ytdlp_label = "нужно обновить" if result.ytdlp_update_available else "актуален"
                ffmpeg_label = "нужно установить" if result.ffmpeg_update_available else "найден"
                self.downloads_page.update_tool_status(ytdlp_label, ffmpeg_label)
                if self._startup_update_check:
                    self._startup_update_check = False
                    if result.ytdlp_update_available or result.ffmpeg_update_available:
                        self.tools_page.append_log("Есть обновления. Установите их из раздела «Инструменты».")
        elif isinstance(result, UpdateResult):
            self.tools_page.append_log(result.message)
            self.show_toast(result.message if result.ok else f"{result.message} {result.error}")
            self.toolchain_status = self.toolchain.get_status()
            self.update_tool_status_ui(self.toolchain_status)
        elif action == "ensure":
            self.tools_page.append_log("Runtime готов." if status.ready else status.warning)
            if self.settings.get("auto_update_tools") and not self._closing:
                self._startup_update_check = True
                self.start_toolchain_task("check", force=False)

    def on_toolchain_task_failed(self, action: str, error: str) -> None:
        self.tools_page.append_log(f"{action}: {error}")
        self.show_toast(error)

    def update_tool_status_ui(self, status: ToolchainStatus) -> None:
        ytdlp_text = "найден" if status.ytdlp.exists else "не найден"
        ffmpeg_text = "найден" if status.ffmpeg.exists and status.ffprobe.exists else "не найден"
        self.downloads_page.update_tool_status(ytdlp_text, ffmpeg_text)
        self.tools_page.set_status(status)

    def paste_url(self) -> None:
        text = QApplication.clipboard().text().strip()
        if text:
            self.downloads_page.url_edit.setText(text)
            self.downloads_page.url_edit.setFocus()

    def check_url(self, url: str) -> None:
        url = url.strip()
        if not is_http_url(url):
            self.show_toast("Введите корректную ссылку.")
            return
        ytdlp = self.toolchain.get_ytdlp_path()
        if not ytdlp:
            self.show_toast("yt-dlp не найден. Откройте раздел «Инструменты» и переустановите runtime.")
            self.switch_page("tools")
            return
        if PREVIEW_TASK_ID in self.metadata_threads:
            return
        self.downloads_page.set_checking(True)
        self.start_metadata_lookup(PREVIEW_TASK_ID, url)

    def add_download(self, url: str) -> None:
        url = url.strip()
        if not is_http_url(url):
            self.show_toast("Введите корректную ссылку.")
            return

        status = self.toolchain.get_status()
        if not status.ytdlp.exists:
            self.show_toast("yt-dlp не найден. Нажмите «Переустановить инструменты».")
            self.switch_page("tools")
            return
        if not status.ffmpeg.exists or not status.ffprobe.exists:
            self.show_toast("ffmpeg/ffprobe не найден. Нажмите «Переустановить инструменты».")
            self.switch_page("tools")
            return

        output_dir = Path(self.settings.get("output_dir")).expanduser()
        try:
            output_dir.mkdir(parents=True, exist_ok=True)
        except Exception as exc:
            self.show_toast(f"Не удалось создать папку: {exc}")
            return

        format_key = self.downloads_page.selected_format_key()
        self.settings.set("default_format", format_key)
        self.settings.save()
        preset = get_format_preset(format_key)
        self.task_counter += 1
        task_id = f"task-{self.task_counter}-{int(time.time() * 1000)}"
        task = DownloadTask(
            id=task_id,
            url=url,
            preset=preset,
            output_dir=output_dir,
            created_at=utc_now_iso(),
        )
        self.tasks[task_id] = task
        self.queue.append(task_id)

        card = DownloadCard(task_id, url, preset.label)
        card.cancel_requested.connect(self.cancel_download)
        card.remove_requested.connect(self.remove_task_card)
        card.retry_requested.connect(self.retry_task)
        card.open_file_requested.connect(self.open_file)
        card.open_folder_requested.connect(self.open_containing_folder)
        self.cards[task_id] = card
        self.downloads_page.add_download_card(card)

        if self.preview_metadata and self.preview_metadata.url == url:
            self.apply_metadata(task_id, self.preview_metadata)
        else:
            self.start_metadata_lookup(task_id, url)

        self.history.add_or_update(task.to_record())
        self.downloads_page.url_edit.clear()
        self.update_queue_ui()
        self.pump_queue()

    def start_metadata_lookup(self, task_id: str, url: str) -> None:
        ytdlp = self.toolchain.get_ytdlp_path()
        if not ytdlp:
            self.on_metadata_error(task_id, "yt-dlp не найден.")
            return
        thread = MetadataProcessThread(task_id, str(ytdlp), url)
        thread.metadata_ready.connect(self.on_metadata_ready)
        thread.error.connect(self.on_metadata_error)
        thread.finished.connect(lambda: self._cleanup_metadata_thread(task_id))
        thread.finished.connect(thread.deleteLater)
        self.metadata_threads[task_id] = thread
        thread.start()

    def _cleanup_metadata_thread(self, task_id: str) -> None:
        self.metadata_threads.pop(task_id, None)
        if task_id == PREVIEW_TASK_ID:
            self.downloads_page.set_checking(False)

    def on_metadata_ready(self, task_id: str, metadata: VideoMetadata) -> None:
        if task_id == PREVIEW_TASK_ID:
            self.preview_metadata = metadata
            self.downloads_page.show_preview(metadata)
            self.show_toast("Ссылка проверена.")
            return
        self.apply_metadata(task_id, metadata)

    def apply_metadata(self, task_id: str, metadata: VideoMetadata) -> None:
        task = self.tasks.get(task_id)
        card = self.cards.get(task_id)
        if not task or not card:
            return
        task.title = metadata.title
        task.uploader = metadata.uploader
        task.duration = metadata.duration
        task.thumbnail_url = metadata.thumbnail_url
        card.set_info(metadata.title, metadata.thumbnail_url, metadata.duration, metadata.uploader)
        self.history.add_or_update(task.to_record())

    def on_metadata_error(self, task_id: str, error: str) -> None:
        if task_id == PREVIEW_TASK_ID:
            self.show_toast(f"Не удалось проверить ссылку: {error}")
            return
        card = self.cards.get(task_id)
        if card:
            card.status_label.setText("В очереди, превью недоступно")

    def pump_queue(self) -> None:
        max_parallel = int(self.settings.get("parallel_downloads", 2))
        while self.queue and len(self.running) < max_parallel:
            task_id = self.queue.pop(0)
            task = self.tasks.get(task_id)
            if not task or task.status != "queued":
                continue
            self.start_download_process(task)
        self.update_queue_ui()

    def start_download_process(self, task: DownloadTask) -> None:
        ytdlp = self.toolchain.get_ytdlp_path()
        if not ytdlp:
            self.mark_task_error(task.id, "yt-dlp не найден.")
            return

        task.status = "running"
        card = self.cards.get(task.id)
        if card:
            card.set_running()

        thread = DownloadProcessThread(
            task_id=task.id,
            ytdlp_path=str(ytdlp),
            url=task.url,
            preset=task.preset,
            output_dir=task.output_dir,
            ffmpeg_location=self.toolchain.get_ffmpeg_location_arg(),
        )
        thread.progress.connect(self.on_download_progress)
        thread.status_changed.connect(self.on_download_status_changed)
        thread.download_finished.connect(self.on_download_finished)
        thread.error.connect(self.on_download_error)
        thread.finished.connect(lambda: self._cleanup_download_thread(task.id))
        thread.finished.connect(thread.deleteLater)
        self.download_threads[task.id] = thread
        self.running.add(task.id)
        self.history.add_or_update(task.to_record())
        thread.start()

    def _cleanup_download_thread(self, task_id: str) -> None:
        self.download_threads.pop(task_id, None)

    def on_download_progress(self, task_id: str, percent: float, speed_text: str, eta_text: str, downloaded_text: str) -> None:
        card = self.cards.get(task_id)
        if card:
            card.update_progress(percent, speed_text, eta_text, downloaded_text)

    def on_download_status_changed(self, task_id: str, status: str) -> None:
        task = self.tasks.get(task_id)
        if task:
            task.status = status

    def on_download_finished(self, task_id: str, output_path: str) -> None:
        task = self.tasks.get(task_id)
        self.running.discard(task_id)
        if not task:
            self.pump_queue()
            return
        task.status = "completed"
        task.output_path = output_path
        card = self.cards.get(task_id)
        if card:
            card.set_finished(True, "Загрузка завершена", output_path)
        self.history.add_or_update(task.to_record())
        self.refresh_history()
        self.show_toast("Загрузка завершена.")
        if output_path and self.settings.get("auto_open_file"):
            self.open_file(output_path)
        self.pump_queue()

    def on_download_error(self, task_id: str, message: str) -> None:
        task = self.tasks.get(task_id)
        self.running.discard(task_id)
        if not task:
            self.pump_queue()
            return
        task.status = "cancelled" if "отмен" in message.lower() else "failed"
        task.error = message
        card = self.cards.get(task_id)
        if card:
            card.set_finished(False, message)
        self.history.add_or_update(task.to_record())
        self.refresh_history()
        self.show_toast(message)
        if self._looks_like_network_error(message):
            self.tools_page.append_log("Сетевая ошибка: проверьте доступность платформы и сетевой маршрут.")
        self.pump_queue()

    def mark_task_error(self, task_id: str, message: str) -> None:
        self.on_download_error(task_id, message)

    def cancel_download(self, task_id: str) -> None:
        task = self.tasks.get(task_id)
        if not task:
            return
        if task.status == "queued":
            if task_id in self.queue:
                self.queue.remove(task_id)
            task.status = "cancelled"
            task.error = "Отменено пользователем."
            card = self.cards.get(task_id)
            if card:
                card.set_finished(False, "Отменено")
            self.history.add_or_update(task.to_record())
            self.update_queue_ui()
            return
        if task.status == "running":
            task.status = "cancelling"
            card = self.cards.get(task_id)
            if card:
                card.set_cancel_pending()
            thread = self.download_threads.get(task_id)
            if thread:
                thread.cancel()

    def cancel_all_downloads(self) -> None:
        active_ids = [task_id for task_id, task in self.tasks.items() if task.status in {"queued", "running", "cancelling"}]
        if not active_ids:
            self.show_toast("Нет активных задач.")
            return
        for task_id in active_ids:
            self.cancel_download(task_id)
        self.update_queue_ui()

    def clear_finished_cards(self) -> None:
        removable = [task_id for task_id, task in self.tasks.items() if task.status in {"completed", "failed", "cancelled"}]
        for task_id in removable:
            self.remove_task_card(task_id)
        self.update_queue_ui()

    def remove_task_card(self, task_id: str) -> None:
        task = self.tasks.get(task_id)
        if task and task.status in {"queued", "running", "cancelling"}:
            self.show_toast("Сначала отмените активную задачу.")
            return
        card = self.cards.pop(task_id, None)
        if card:
            card.setParent(None)
            card.deleteLater()
        self.tasks.pop(task_id, None)
        self.update_queue_ui()

    def retry_task(self, task_id: str) -> None:
        task = self.tasks.get(task_id)
        if task:
            self.downloads_page.url_edit.setText(task.url)
            self.update_setting("default_format", task.preset.key)
            self.add_download(task.url)

    def retry_history_record(self, record_id: str) -> None:
        records = self.history.list(limit=500)
        record = next((item for item in records if item.id == record_id), None)
        if record:
            self.downloads_page.url_edit.setText(record.url)
            self.switch_page("downloads")
            self.add_download(record.url)

    def update_queue_ui(self) -> None:
        for index, task_id in enumerate(self.queue, start=1):
            card = self.cards.get(task_id)
            if card:
                card.set_queue_position(index)
        has_cards = bool(self.cards)
        self.downloads_page.update_queue_state(len(self.running), len(self.queue), has_cards)

    def refresh_history(self, query: str = "") -> None:
        self.history_page.set_records(self.history.list(limit=100, query=query))

    def delete_history_record(self, record_id: str) -> None:
        self.history.delete(record_id)
        self.refresh_history(self.history_page.search.text())

    def choose_output_dir(self) -> None:
        folder = QFileDialog.getExistingDirectory(self, "Выберите папку сохранения", self.settings.get("output_dir"))
        if not folder:
            return
        self.update_setting("output_dir", folder)

    def open_download_folder(self) -> None:
        self.open_path(self.settings.get("output_dir"))

    def open_runtime_folder(self) -> None:
        self.paths.ensure()
        self.open_path(str(self.paths.runtime_dir))

    def open_file(self, path: str) -> None:
        if not path or not Path(path).exists():
            self.show_toast("Файл не найден.")
            return
        QDesktopServices.openUrl(QUrl.fromLocalFile(path))

    def open_containing_folder(self, path: str) -> None:
        if not path:
            self.show_toast("Путь к файлу отсутствует.")
            return
        folder = Path(path).parent if Path(path).suffix else Path(path)
        self.open_path(str(folder))

    def open_path(self, path: str) -> None:
        if not path or not Path(path).exists():
            self.show_toast("Папка не найдена.")
            return
        QDesktopServices.openUrl(QUrl.fromLocalFile(str(path)))

    def focus_url_input(self) -> None:
        self.switch_page("downloads")
        self.downloads_page.url_edit.setFocus()
        self.downloads_page.url_edit.selectAll()

    def open_onyshop_site(self) -> None:
        QDesktopServices.openUrl(QUrl(ONYSHOP_URL))

    def show_onyshop_help(self) -> None:
        QMessageBox.information(
            self,
            "Если видео не загружается",
            "Иногда платформы ограничивают доступ по сети или региону. VPN-сервис может помочь сменить сетевой маршрут, но приложение не обходит правила платформ, авторизацию, возрастные или платные ограничения.",
        )

    def set_auto_update_tools(self, enabled: bool) -> None:
        self.update_setting("auto_update_tools", enabled)
        self.toolchain.set_auto_update_enabled(enabled)

    def apply_settings_update(self, values: dict) -> None:
        self.settings.update(**values)
        self.settings.save()
        self._apply_settings_to_pages()
        self.pump_queue()

    def update_setting(self, key: str, value) -> None:
        self.settings.set(key, value)
        self.settings.save()
        self._apply_settings_to_pages()
        if key == "parallel_downloads":
            self.pump_queue()

    def reset_settings(self) -> None:
        self.settings.reset()
        self._apply_settings_to_pages()
        self.show_toast("Настройки сброшены.")

    def copy_diagnostics(self) -> None:
        status = self.toolchain.get_status()
        text = build_diagnostics(
            runtime_dir=status.runtime_dir,
            ytdlp_path=status.ytdlp.path,
            ytdlp_version=status.ytdlp.version,
            ffmpeg_path=status.ffmpeg.path,
            ffmpeg_version=status.ffmpeg.version,
            last_update_check=status.last_update_check,
            last_error=status.last_error,
            settings_summary=self.settings.as_dict(),
        )
        QApplication.clipboard().setText(text)
        self.show_toast("Диагностика скопирована.")

    def show_toast(self, message: str) -> None:
        self.toast.show_message(message)

    def _looks_like_network_error(self, message: str) -> bool:
        text = message.lower()
        return any(keyword in text for keyword in ("403", "429", "network", "timeout", "connection", "geo", "blocked", "restricted"))

    def closeEvent(self, event) -> None:
        active = [task_id for task_id, task in self.tasks.items() if task.status in {"queued", "running", "cancelling"}]
        if active:
            answer = QMessageBox.question(
                self,
                "Выход",
                f"Есть незавершённые задачи ({len(active)}). Отменить и выйти?",
                QMessageBox.Yes | QMessageBox.No,
                QMessageBox.No,
            )
            if answer != QMessageBox.Yes:
                event.ignore()
                return
            for task_id in active:
                self.cancel_download(task_id)
            for thread in list(self.download_threads.values()):
                thread.wait(2500)
        self.shutdown_background_threads()
        self.settings.save()
        event.accept()

    def shutdown_background_threads(self) -> None:
        if self._closing:
            return
        self._closing = True
        for thread in list(self.download_threads.values()):
            thread.cancel()
        for thread in list(self.download_threads.values()):
            if thread.isRunning():
                thread.wait(2500)
        for thread in list(self.metadata_threads.values()):
            if thread.isRunning():
                thread.wait(2500)
        for thread in list(self.toolchain_threads):
            if thread.isRunning():
                thread.wait(45000)
