from __future__ import annotations

import logging
import platform
import sys
from pathlib import Path
from typing import Any, Mapping

from core import APP_VERSION
from core.paths import AppPaths


def setup_logging(paths: AppPaths | None = None) -> None:
    paths = paths or AppPaths.default()
    paths.ensure()

    formatter = logging.Formatter(
        "%(asctime)s %(levelname)s [%(name)s] %(message)s",
        datefmt="%Y-%m-%d %H:%M:%S",
    )

    root = logging.getLogger()
    root.setLevel(logging.INFO)
    root.handlers.clear()

    app_handler = logging.FileHandler(paths.logs_dir / "app.log", encoding="utf-8")
    app_handler.setFormatter(formatter)
    root.addHandler(app_handler)

    for logger_name, filename in (
        ("toolchain", "toolchain.log"),
        ("downloads", "downloads.log"),
    ):
        logger = logging.getLogger(logger_name)
        logger.setLevel(logging.INFO)
        logger.propagate = True
        handler = logging.FileHandler(paths.logs_dir / filename, encoding="utf-8")
        handler.setFormatter(formatter)
        logger.addHandler(handler)

    logging.getLogger("app").info("Application started")


def get_logger(name: str) -> logging.Logger:
    return logging.getLogger(name)


def build_diagnostics(
    *,
    runtime_dir: str,
    ytdlp_path: str,
    ytdlp_version: str,
    ffmpeg_path: str,
    ffmpeg_version: str,
    last_update_check: str,
    last_error: str,
    settings_summary: Mapping[str, Any],
) -> str:
    safe_settings = {
        key: value
        for key, value in settings_summary.items()
        if "token" not in key.lower() and "secret" not in key.lower()
    }
    lines = [
        "Video Downloader Pro Diagnostics",
        f"App version: {APP_VERSION}",
        f"OS: {platform.platform()}",
        f"Python: {sys.version.replace(chr(10), ' ')}",
        f"Runtime dir: {runtime_dir}",
        f"yt-dlp: {ytdlp_version or 'unknown'} ({ytdlp_path or 'missing'})",
        f"ffmpeg: {ffmpeg_version or 'unknown'} ({ffmpeg_path or 'missing'})",
        f"Last update check: {last_update_check or 'never'}",
        f"Last error: {last_error or 'none'}",
        "Settings:",
    ]
    lines.extend(f"  {key}: {value}" for key, value in sorted(safe_settings.items()))
    return "\n".join(lines)


def tail_file(path: Path, limit: int = 40) -> str:
    if not path.exists():
        return ""
    try:
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    except Exception:
        return ""
    return "\n".join(lines[-limit:])
