from __future__ import annotations

from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Dict, List, Optional


def utc_now_iso() -> str:
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


@dataclass(frozen=True)
class FormatPreset:
    key: str
    label: str
    selector: str
    extract_audio: bool = False
    extension: str = "mp4"
    description: str = ""


FORMAT_PRESETS: List[FormatPreset] = [
    FormatPreset(
        key="best",
        label="Лучшее",
        selector="bestvideo+bestaudio/best",
        extension="mp4",
        description="Максимальное доступное качество видео",
    ),
    FormatPreset(
        key="1080p",
        label="1080p",
        selector="bestvideo[height<=1080]+bestaudio/best[height<=1080]/best",
        extension="mp4",
        description="Видео до 1080p",
    ),
    FormatPreset(
        key="720p",
        label="720p",
        selector="bestvideo[height<=720]+bestaudio/best[height<=720]/best",
        extension="mp4",
        description="Видео до 720p",
    ),
    FormatPreset(
        key="480p",
        label="480p",
        selector="bestvideo[height<=480]+bestaudio/best[height<=480]/best",
        extension="mp4",
        description="Видео до 480p",
    ),
    FormatPreset(
        key="mp3",
        label="MP3",
        selector="bestaudio/best",
        extract_audio=True,
        extension="mp3",
        description="Только аудио, MP3 192K",
    ),
]


def get_format_preset(key: str) -> FormatPreset:
    for preset in FORMAT_PRESETS:
        if preset.key == key:
            return preset
    return FORMAT_PRESETS[0]


@dataclass
class VideoMetadata:
    url: str
    title: str = "Без названия"
    uploader: str = ""
    duration: Optional[int] = None
    thumbnail_url: str = ""
    platform: str = ""
    webpage_url: str = ""
    format_summary: str = ""

    @classmethod
    def from_ytdlp_json(cls, payload: Dict[str, Any], fallback_url: str = "") -> "VideoMetadata":
        formats = payload.get("formats") or []
        heights = sorted(
            {
                int(fmt.get("height"))
                for fmt in formats
                if isinstance(fmt, dict) and isinstance(fmt.get("height"), int)
            },
            reverse=True,
        )
        if heights:
            summary = "Видео: " + ", ".join(f"{height}p" for height in heights[:5])
        else:
            summary = "Доступные форматы определит yt-dlp"

        return cls(
            url=payload.get("webpage_url") or fallback_url,
            title=payload.get("title") or "Без названия",
            uploader=payload.get("uploader") or payload.get("channel") or "",
            duration=payload.get("duration") if isinstance(payload.get("duration"), int) else None,
            thumbnail_url=payload.get("thumbnail") or "",
            platform=payload.get("extractor_key") or payload.get("extractor") or "",
            webpage_url=payload.get("webpage_url") or fallback_url,
            format_summary=summary,
        )


@dataclass
class DownloadProgress:
    percent: float = 0.0
    speed_text: str = ""
    eta_text: str = ""
    downloaded_bytes: Optional[int] = None
    total_bytes: Optional[int] = None


@dataclass
class DownloadRecord:
    id: str
    url: str
    title: str
    uploader: str
    duration: Optional[int]
    thumbnail_url: str
    output_path: str
    format_label: str
    status: str
    created_at: str
    finished_at: str = ""
    error: str = ""


@dataclass
class DownloadTask:
    id: str
    url: str
    preset: FormatPreset
    output_dir: Path
    title: str = "Получение информации..."
    uploader: str = ""
    duration: Optional[int] = None
    thumbnail_url: str = ""
    output_path: str = ""
    status: str = "queued"
    created_at: str = ""
    error: str = ""

    def to_record(self) -> DownloadRecord:
        return DownloadRecord(
            id=self.id,
            url=self.url,
            title=self.title,
            uploader=self.uploader,
            duration=self.duration,
            thumbnail_url=self.thumbnail_url,
            output_path=self.output_path,
            format_label=self.preset.label,
            status=self.status,
            created_at=self.created_at or utc_now_iso(),
            finished_at=utc_now_iso() if self.status in {"completed", "failed", "cancelled"} else "",
            error=self.error,
        )


@dataclass
class ToolInfo:
    name: str
    path: str = ""
    version: str = ""
    source: str = "missing"
    exists: bool = False
    verified: bool = True
    error: str = ""

    def to_manifest(self) -> Dict[str, Any]:
        return {
            "version": self.version,
            "path": self.path,
            "updated_at": utc_now_iso(),
            "source": self.source,
            "verified": self.verified,
        }


@dataclass
class ToolchainStatus:
    ytdlp: ToolInfo
    ffmpeg: ToolInfo
    ffprobe: ToolInfo
    runtime_dir: str
    manifest_path: str
    last_update_check: str = ""
    auto_update_enabled: bool = True
    warning: str = ""
    last_error: str = ""

    @property
    def ready(self) -> bool:
        return self.ytdlp.exists and self.ffmpeg.exists and self.ffprobe.exists


@dataclass
class UpdateCheckResult:
    checked_at: str
    ytdlp_current: str = ""
    ytdlp_latest: str = ""
    ytdlp_update_available: bool = False
    ffmpeg_current: str = ""
    ffmpeg_latest: str = ""
    ffmpeg_update_available: bool = False
    skipped: bool = False
    message: str = ""
    error: str = ""


@dataclass
class UpdateResult:
    ok: bool
    tool: str
    message: str
    version: str = ""
    path: str = ""
    verified: bool = True
    error: str = ""

    def as_dict(self) -> Dict[str, Any]:
        return asdict(self)
