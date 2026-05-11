from __future__ import annotations

import os
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Mapping, Optional

from core import APP_NAME


def get_app_base_dir(
    app_name: str = APP_NAME,
    platform_name: Optional[str] = None,
    env: Optional[Mapping[str, str]] = None,
    home: Optional[Path] = None,
) -> Path:
    platform_name = platform_name or sys.platform
    env = env or os.environ
    home = Path(home) if home is not None else Path.home()

    if platform_name.startswith("win"):
        root = env.get("LOCALAPPDATA")
        return Path(root) / app_name if root else home / "AppData" / "Local" / app_name

    if platform_name == "darwin":
        return home / "Library" / "Application Support" / app_name

    xdg_data = env.get("XDG_DATA_HOME")
    return (Path(xdg_data) if xdg_data else home / ".local" / "share") / app_name


@dataclass(frozen=True)
class AppPaths:
    base_dir: Path
    runtime_dir: Path
    ytdlp_dir: Path
    ffmpeg_dir: Path
    ffmpeg_bin_dir: Path
    logs_dir: Path
    data_dir: Path
    cache_dir: Path
    manifest_path: Path
    settings_path: Path
    history_path: Path

    @classmethod
    def from_base(cls, base_dir: Path) -> "AppPaths":
        base_dir = Path(base_dir)
        runtime_dir = base_dir / "runtime"
        data_dir = base_dir / "data"
        return cls(
            base_dir=base_dir,
            runtime_dir=runtime_dir,
            ytdlp_dir=runtime_dir / "yt-dlp",
            ffmpeg_dir=runtime_dir / "ffmpeg",
            ffmpeg_bin_dir=runtime_dir / "ffmpeg" / "bin",
            logs_dir=base_dir / "logs",
            data_dir=data_dir,
            cache_dir=base_dir / "cache",
            manifest_path=runtime_dir / "manifest.json",
            settings_path=data_dir / "settings.json",
            history_path=data_dir / "history.sqlite",
        )

    @classmethod
    def default(cls) -> "AppPaths":
        return cls.from_base(get_app_base_dir())

    def ensure(self) -> None:
        for directory in (
            self.base_dir,
            self.runtime_dir,
            self.ytdlp_dir,
            self.ffmpeg_dir,
            self.ffmpeg_bin_dir,
            self.logs_dir,
            self.data_dir,
            self.cache_dir,
        ):
            directory.mkdir(parents=True, exist_ok=True)


def resource_root() -> Path:
    meipass = getattr(sys, "_MEIPASS", None)
    if meipass:
        return Path(meipass)
    return Path(__file__).resolve().parents[1]
