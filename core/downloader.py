from __future__ import annotations

import json
import os
import re
from pathlib import Path
from typing import List, Optional

try:
    from PySide6.QtCore import QProcess, QThread, Signal
except ModuleNotFoundError:  # Allows non-Qt unit tests to import pure helpers.
    QProcess = None  # type: ignore[assignment]

    class QThread:  # type: ignore[no-redef]
        pass

    class Signal:  # type: ignore[no-redef]
        def __init__(self, *args, **kwargs):
            pass

        def emit(self, *args, **kwargs):
            pass

from core.logger import get_logger
from core.models import DownloadProgress, FormatPreset, VideoMetadata
from core.validators import sanitize_error_message


log = get_logger("downloads")


def parse_progress_line(line: str) -> Optional[DownloadProgress]:
    if not line or not line.startswith("download:"):
        return None

    raw_parts = line[len("download:") :].strip().split("|")
    while len(raw_parts) < 5:
        raw_parts.append("")

    percent_text, speed_text, eta_text, downloaded_text, total_text = [part.strip() for part in raw_parts[:5]]
    percent = _parse_percent(percent_text)

    return DownloadProgress(
        percent=percent,
        speed_text="" if speed_text.upper() in {"NA", "N/A"} else speed_text,
        eta_text="" if eta_text.upper() in {"NA", "N/A", "UNKNOWN ETA"} else eta_text,
        downloaded_bytes=_parse_int(downloaded_text),
        total_bytes=_parse_int(total_text),
    )


def format_bytes(value: Optional[int]) -> str:
    if not value:
        return ""
    size = float(value)
    for unit in ("B", "KiB", "MiB", "GiB"):
        if size < 1024 or unit == "GiB":
            return f"{size:.1f} {unit}" if unit != "B" else f"{int(size)} B"
        size /= 1024
    return ""


def format_duration(seconds: Optional[int]) -> str:
    if not isinstance(seconds, int) or seconds <= 0:
        return ""
    hours, remainder = divmod(seconds, 3600)
    minutes, secs = divmod(remainder, 60)
    if hours:
        return f"{hours}:{minutes:02d}:{secs:02d}"
    return f"{minutes:02d}:{secs:02d}"


def build_metadata_args(url: str) -> List[str]:
    return ["--dump-single-json", "--skip-download", "--no-playlist", url]


def build_download_args(
    *,
    url: str,
    preset: FormatPreset,
    output_dir: Path,
    ffmpeg_location: str,
) -> List[str]:
    args = [
        "--newline",
        "--ignore-config",
        "--no-playlist",
        "--progress-template",
        "download:%(progress._percent_str)s|%(progress._speed_str)s|%(progress._eta_str)s|%(progress.downloaded_bytes)s|%(progress.total_bytes)s",
        "--print",
        "after_move:vdppath:%(filepath)s",
    ]
    if ffmpeg_location:
        args.extend(["--ffmpeg-location", ffmpeg_location])

    args.extend(["-f", preset.selector])
    if preset.extract_audio:
        args.extend(["-x", "--audio-format", "mp3", "--audio-quality", "192K"])
    else:
        args.extend(["--merge-output-format", "mp4"])

    args.extend(["-o", str(output_dir / "%(title).180B.%(ext)s"), url])
    return args


class MetadataProcessThread(QThread):
    metadata_ready = Signal(str, object)
    error = Signal(str, str)

    def __init__(self, task_id: str, ytdlp_path: str, url: str):
        super().__init__()
        self.task_id = task_id
        self.ytdlp_path = ytdlp_path
        self.url = url

    def run(self) -> None:
        if QProcess is None:
            self.error.emit(self.task_id, "PySide6 не установлен.")
            return

        process = QProcess()
        process.setProgram(self.ytdlp_path)
        process.setArguments(build_metadata_args(self.url))
        process.setProcessChannelMode(QProcess.SeparateChannels)
        log.info("metadata command: %s %s", self.ytdlp_path, " ".join(build_metadata_args("<url>")))
        process.start()
        if not process.waitForStarted(8000):
            self.error.emit(self.task_id, "Не удалось запустить yt-dlp.")
            return
        process.waitForFinished(120000)

        stdout = bytes(process.readAllStandardOutput()).decode("utf-8", errors="replace")
        stderr = bytes(process.readAllStandardError()).decode("utf-8", errors="replace")
        if process.state() != QProcess.NotRunning:
            process.kill()
            process.waitForFinished(3000)
            self.error.emit(self.task_id, "Проверка ссылки заняла слишком много времени.")
            return

        if process.exitCode() != 0:
            self.error.emit(self.task_id, sanitize_error_message(stderr or stdout))
            return

        try:
            payload = json.loads(stdout)
        except Exception as exc:
            self.error.emit(self.task_id, f"Не удалось прочитать ответ yt-dlp: {exc}")
            return

        self.metadata_ready.emit(self.task_id, VideoMetadata.from_ytdlp_json(payload, self.url))


class DownloadProcessThread(QThread):
    metadata_ready = Signal(str, object)
    progress = Signal(str, float, str, str, str)
    status_changed = Signal(str, str)
    download_finished = Signal(str, str)
    error = Signal(str, str)

    def __init__(
        self,
        *,
        task_id: str,
        ytdlp_path: str,
        url: str,
        preset: FormatPreset,
        output_dir: Path,
        ffmpeg_location: str,
    ):
        super().__init__()
        self.task_id = task_id
        self.ytdlp_path = ytdlp_path
        self.url = url
        self.preset = preset
        self.output_dir = Path(output_dir)
        self.ffmpeg_location = ffmpeg_location
        self._cancel_requested = False
        self._output_path = ""

    def cancel(self) -> None:
        self._cancel_requested = True

    def run(self) -> None:
        if QProcess is None:
            self.error.emit(self.task_id, "PySide6 не установлен.")
            return

        self.output_dir.mkdir(parents=True, exist_ok=True)
        started_at = self._latest_mtime()
        args = build_download_args(
            url=self.url,
            preset=self.preset,
            output_dir=self.output_dir,
            ffmpeg_location=self.ffmpeg_location,
        )
        log.info("download command: %s %s", self.ytdlp_path, " ".join(arg if arg != self.url else "<url>" for arg in args))

        process = QProcess()
        process.setProgram(self.ytdlp_path)
        process.setArguments(args)
        process.setProcessChannelMode(QProcess.SeparateChannels)
        process.start()

        if not process.waitForStarted(8000):
            self.error.emit(self.task_id, "Не удалось запустить yt-dlp.")
            return

        self.status_changed.emit(self.task_id, "running")
        stdout_buffer = ""
        stderr_buffer = ""

        while process.state() != QProcess.NotRunning:
            if self._cancel_requested:
                process.terminate()
                if not process.waitForFinished(2500):
                    process.kill()
                    process.waitForFinished(2500)
                self.status_changed.emit(self.task_id, "cancelled")
                self.error.emit(self.task_id, "Загрузка отменена.")
                return

            process.waitForReadyRead(100)
            stdout_buffer = self._consume_stdout(process, stdout_buffer)
            stderr = bytes(process.readAllStandardError()).decode("utf-8", errors="replace")
            if stderr:
                stderr_buffer += stderr

        stdout_buffer = self._consume_stdout(process, stdout_buffer, flush=True)
        remaining_stderr = bytes(process.readAllStandardError()).decode("utf-8", errors="replace")
        if remaining_stderr:
            stderr_buffer += remaining_stderr

        if self._cancel_requested:
            self.error.emit(self.task_id, "Загрузка отменена.")
            return

        if process.exitCode() != 0:
            message = sanitize_error_message(stderr_buffer or stdout_buffer)
            log.error("download failed: %s", message)
            self.error.emit(self.task_id, message)
            return

        output_path = self._output_path or self._guess_output_path(started_at)
        self.progress.emit(self.task_id, 100.0, "", "", "")
        self.status_changed.emit(self.task_id, "completed")
        self.download_finished.emit(self.task_id, output_path)

    def _consume_stdout(self, process: QProcess, buffer: str, flush: bool = False) -> str:
        chunk = bytes(process.readAllStandardOutput()).decode("utf-8", errors="replace")
        if chunk:
            buffer += chunk
        if not buffer:
            return ""

        lines = buffer.splitlines(keepends=True)
        if not flush and lines and not lines[-1].endswith(("\n", "\r")):
            buffer = lines.pop()
        else:
            buffer = ""

        for raw_line in lines:
            self._handle_output_line(raw_line.strip())
        return buffer

    def _handle_output_line(self, line: str) -> None:
        if not line:
            return
        if line.startswith("vdppath:"):
            self._output_path = line[len("vdppath:") :].strip()
            return

        progress = parse_progress_line(line)
        if progress:
            downloaded = format_bytes(progress.downloaded_bytes)
            total = format_bytes(progress.total_bytes)
            downloaded_text = f"{downloaded} / {total}" if downloaded and total else downloaded
            self.progress.emit(
                self.task_id,
                progress.percent,
                progress.speed_text,
                progress.eta_text,
                downloaded_text,
            )

    def _latest_mtime(self) -> float:
        try:
            return max((path.stat().st_mtime for path in self.output_dir.iterdir() if path.is_file()), default=0.0)
        except Exception:
            return 0.0

    def _guess_output_path(self, started_at: float) -> str:
        candidates = []
        try:
            for path in self.output_dir.iterdir():
                if not path.is_file() or path.suffix.lower() in {".part", ".ytdl"}:
                    continue
                if path.stat().st_mtime >= started_at:
                    candidates.append(path)
        except Exception:
            return ""

        if not candidates:
            return ""
        return str(max(candidates, key=lambda item: item.stat().st_mtime))


def _parse_percent(value: str) -> float:
    cleaned = value.strip().replace("%", "")
    cleaned = re.sub(r"\x1b\[[0-9;]*m", "", cleaned)
    try:
        return max(0.0, min(100.0, float(cleaned)))
    except Exception:
        return 0.0


def _parse_int(value: str) -> Optional[int]:
    value = value.strip()
    if not value or value.upper() in {"NA", "N/A", "NONE"}:
        return None
    try:
        return int(value)
    except Exception:
        return None
