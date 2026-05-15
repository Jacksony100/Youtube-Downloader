from __future__ import annotations

from dataclasses import dataclass


USER_AGENT = "VideoDownloaderPro/3.1 (+https://github.com/Jacksony100/Youtube-Downloader)"


@dataclass(frozen=True)
class DownloadSource:
    name: str
    url: str
    checksum_url: str = ""
    verified_by_default: bool = False
    checksum_filename: str = ""


YTDLP_RELEASE_API = "https://api.github.com/repos/yt-dlp/yt-dlp/releases/latest"

YTDLP_WINDOWS = DownloadSource(
    name="yt-dlp GitHub release",
    url="https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp.exe",
    checksum_url="https://github.com/yt-dlp/yt-dlp/releases/latest/download/SHA2-256SUMS",
    verified_by_default=True,
)

YTDLP_POSIX = DownloadSource(
    name="yt-dlp GitHub release",
    url="https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp",
    checksum_url="https://github.com/yt-dlp/yt-dlp/releases/latest/download/SHA2-256SUMS",
    verified_by_default=True,
    checksum_filename="yt-dlp",
)

YTDLP_MACOS = DownloadSource(
    name="yt-dlp GitHub release",
    url="https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp_macos",
    checksum_url="https://github.com/yt-dlp/yt-dlp/releases/latest/download/SHA2-256SUMS",
    verified_by_default=True,
    checksum_filename="yt-dlp_macos",
)

# Gyan.dev is a widely used public Windows FFmpeg build provider. It does not
# expose a stable checksum endpoint for this generic latest zip, so installs from
# this source are explicitly marked as unverified in the manifest/UI.
FFMPEG_WINDOWS = DownloadSource(
    name="gyan.dev ffmpeg release essentials",
    url="https://www.gyan.dev/ffmpeg/builds/ffmpeg-release-essentials.zip",
    checksum_url="",
    verified_by_default=False,
)
