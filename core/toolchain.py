from __future__ import annotations

import hashlib
import json
import os
import shutil
import stat
import subprocess
import sys
import tempfile
import urllib.error
import urllib.request
import zipfile
from datetime import datetime, timedelta, timezone
from pathlib import Path
from typing import Any, Dict, Optional, Tuple

from core.logger import get_logger
from core.models import ToolInfo, ToolchainStatus, UpdateCheckResult, UpdateResult, utc_now_iso
from core.paths import AppPaths, resource_root
from core.update_sources import (
    FFMPEG_WINDOWS,
    USER_AGENT,
    YTDLP_MACOS,
    YTDLP_POSIX,
    YTDLP_RELEASE_API,
    YTDLP_WINDOWS,
    DownloadSource,
)


log = get_logger("toolchain")


class ToolchainManager:
    def __init__(self, paths: Optional[AppPaths] = None):
        self.paths = paths or AppPaths.default()
        self.paths.ensure()
        self._manifest: Dict[str, Any] = self._read_manifest()
        self.last_error = ""

    def runtime_dir(self) -> Path:
        return self.paths.runtime_dir

    def bundled_dir(self) -> Path:
        root = resource_root()
        candidates = [
            root / "toolchain",
            root / "build_assets" / "toolchain",
            Path(sys.executable).resolve().parent / "toolchain",
        ]
        for candidate in candidates:
            if candidate.exists():
                return candidate
        return candidates[0]

    def ensure_runtime(self) -> ToolchainStatus:
        self.paths.ensure()
        self._copy_or_find_ytdlp()
        self._copy_or_find_ffmpeg()
        status = self.get_status()
        self._update_manifest_from_status(status)
        log.info("runtime ensured: ytdlp=%s ffmpeg=%s ffprobe=%s", status.ytdlp.path, status.ffmpeg.path, status.ffprobe.path)
        return status

    def get_status(self, refresh_versions: bool = False) -> ToolchainStatus:
        ytdlp_path = self.get_ytdlp_path()
        ffmpeg_path = self.get_ffmpeg_path()
        ffprobe_path = self.get_ffprobe_path()

        ytdlp = ToolInfo(
            name="yt-dlp",
            path=str(ytdlp_path or ""),
            exists=bool(ytdlp_path and ytdlp_path.exists()),
            version=self._tool_version("yt_dlp", ytdlp_path, ["--version"], refresh_versions),
            source=self._tool_source("yt_dlp", ytdlp_path),
            verified=bool(self._manifest.get("yt_dlp", {}).get("verified", True)),
        )
        ffmpeg = ToolInfo(
            name="ffmpeg",
            path=str(ffmpeg_path or ""),
            exists=bool(ffmpeg_path and ffmpeg_path.exists()),
            version=self._ffmpeg_version(ffmpeg_path, refresh_versions),
            source=self._tool_source("ffmpeg", ffmpeg_path),
            verified=bool(self._manifest.get("ffmpeg", {}).get("verified", True)),
        )
        ffprobe = ToolInfo(
            name="ffprobe",
            path=str(ffprobe_path or ""),
            exists=bool(ffprobe_path and ffprobe_path.exists()),
            version=self._tool_version("ffprobe", ffprobe_path, ["-version"], refresh_versions),
            source=self._tool_source("ffprobe", ffprobe_path),
            verified=ffmpeg.verified,
        )
        warning = ""
        if not ytdlp.exists:
            warning = "yt-dlp не найден"
        elif not ffmpeg.exists or not ffprobe.exists:
            warning = "ffmpeg/ffprobe не найден"

        return ToolchainStatus(
            ytdlp=ytdlp,
            ffmpeg=ffmpeg,
            ffprobe=ffprobe,
            runtime_dir=str(self.paths.runtime_dir),
            manifest_path=str(self.paths.manifest_path),
            last_update_check=str(self._manifest.get("last_update_check", "")),
            auto_update_enabled=bool(self._manifest.get("auto_update_enabled", True)),
            warning=warning,
            last_error=self.last_error or str(self._manifest.get("last_error", "")),
        )

    def get_ytdlp_path(self) -> Optional[Path]:
        candidate = self.paths.ytdlp_dir / self._exe_name("yt-dlp")
        if candidate.exists():
            return candidate
        system = shutil.which("yt-dlp.exe" if self._is_windows() else "yt-dlp")
        return Path(system) if system else None

    def get_ffmpeg_path(self) -> Optional[Path]:
        candidate = self.paths.ffmpeg_bin_dir / self._exe_name("ffmpeg")
        if candidate.exists():
            return candidate
        system = shutil.which("ffmpeg.exe" if self._is_windows() else "ffmpeg")
        return Path(system) if system else None

    def get_ffprobe_path(self) -> Optional[Path]:
        candidate = self.paths.ffmpeg_bin_dir / self._exe_name("ffprobe")
        if candidate.exists():
            return candidate
        system = shutil.which("ffprobe.exe" if self._is_windows() else "ffprobe")
        return Path(system) if system else None

    def get_ffmpeg_location_arg(self) -> str:
        ffmpeg_path = self.get_ffmpeg_path()
        if not ffmpeg_path:
            return ""
        return str(ffmpeg_path.parent)

    def get_ytdlp_version(self, refresh: bool = True) -> str | None:
        path = self.get_ytdlp_path()
        if not path:
            return None
        return self._tool_version("yt_dlp", path, ["--version"], refresh) or None

    def get_ffmpeg_version(self, refresh: bool = True) -> str | None:
        path = self.get_ffmpeg_path()
        if not path:
            return None
        return self._ffmpeg_version(path, refresh) or None

    def check_updates(self, force: bool = False) -> UpdateCheckResult:
        status = self.get_status(refresh_versions=force)
        if force:
            self._update_manifest_from_status(status)
        if not force and not self._is_update_due():
            return UpdateCheckResult(
                checked_at=utc_now_iso(),
                ytdlp_current=status.ytdlp.version or "",
                ffmpeg_current=status.ffmpeg.version or "",
                skipped=True,
                message="Проверка уже выполнялась менее 24 часов назад.",
            )

        result = UpdateCheckResult(
            checked_at=utc_now_iso(),
            ytdlp_current=status.ytdlp.version or "",
            ffmpeg_current=status.ffmpeg.version or "",
        )
        try:
            latest_ytdlp = self._fetch_latest_ytdlp_version()
            result.ytdlp_latest = latest_ytdlp
            result.ytdlp_update_available = bool(latest_ytdlp and latest_ytdlp != result.ytdlp_current)
            result.ffmpeg_latest = "latest essentials"
            result.ffmpeg_update_available = not bool(self.get_ffmpeg_path() and self.get_ffprobe_path())
            result.message = "Проверка обновлений завершена."
            self._manifest["last_update_check"] = result.checked_at
            self._manifest["last_error"] = ""
            self._write_manifest()
        except Exception as exc:
            self.last_error = str(exc)
            result.error = str(exc)
            result.message = "Не удалось проверить обновления."
            self._manifest["last_update_check"] = result.checked_at
            self._manifest["last_error"] = str(exc)
            self._write_manifest()
            log.exception("update check failed")
        return result

    def update_ytdlp(self) -> UpdateResult:
        source = self._ytdlp_source()
        target = self.paths.ytdlp_dir / self._exe_name("yt-dlp")
        staging = self._prepare_staging("yt-dlp")
        staged = staging / target.name

        try:
            self._download(source.url, staged)
            expected_hash = self._fetch_checksum(source, source.checksum_filename or target.name)
            verified = False
            if expected_hash:
                actual_hash = self._sha256(staged)
                if actual_hash.lower() != expected_hash.lower():
                    raise RuntimeError("SHA256 yt-dlp не совпал с опубликованной контрольной суммой.")
                verified = True
            elif source.verified_by_default:
                raise RuntimeError("Не удалось получить SHA256 для yt-dlp.")

            self._make_executable(staged)
            version = self._run_version(staged, ["--version"])
            if not version:
                raise RuntimeError("Скачанный yt-dlp не запускается.")
            self._atomic_install_file(staged, target)
            self._manifest["yt_dlp"] = {
                "version": version,
                "path": str(target),
                "updated_at": utc_now_iso(),
                "source": "github-release",
                "verified": verified,
            }
            self._manifest["last_error"] = ""
            self._write_manifest()
            return UpdateResult(True, "yt-dlp", "yt-dlp обновлён.", version, str(target), verified)
        except Exception as exc:
            self.last_error = str(exc)
            self._manifest["last_error"] = str(exc)
            self._write_manifest()
            log.exception("yt-dlp update failed")
            return UpdateResult(False, "yt-dlp", "Не удалось обновить yt-dlp.", error=str(exc))
        finally:
            shutil.rmtree(staging, ignore_errors=True)

    def update_ffmpeg(self) -> UpdateResult:
        if not self._is_windows():
            return UpdateResult(
                False,
                "ffmpeg",
                "Автозагрузка ffmpeg сейчас поддержана только для Windows-сборки.",
                error="unsupported-platform",
            )

        staging = self._prepare_staging("ffmpeg")
        archive = staging / "ffmpeg.zip"
        extracted = staging / "extracted"
        target_dir = self.paths.ffmpeg_bin_dir

        try:
            self._download(FFMPEG_WINDOWS.url, archive)
            ffmpeg_staged, ffprobe_staged = self._extract_ffmpeg_tools(archive, extracted)
            ffmpeg_version = self._run_version(ffmpeg_staged, ["-version"])
            ffprobe_version = self._run_version(ffprobe_staged, ["-version"])
            if not ffmpeg_version or not ffprobe_version:
                raise RuntimeError("Скачанные ffmpeg/ffprobe не запускаются.")

            target_dir.mkdir(parents=True, exist_ok=True)
            self._atomic_install_file(ffmpeg_staged, target_dir / "ffmpeg.exe")
            self._atomic_install_file(ffprobe_staged, target_dir / "ffprobe.exe")

            version = self._parse_ffmpeg_version(ffmpeg_version) or "installed"
            self._manifest["ffmpeg"] = {
                "version": version,
                "path": str(target_dir / "ffmpeg.exe"),
                "updated_at": utc_now_iso(),
                "source": "downloaded-unverified",
                "verified": False,
            }
            self._manifest["ffprobe"] = {
                "version": (ffprobe_version.splitlines()[0] if ffprobe_version else ""),
                "path": str(target_dir / "ffprobe.exe"),
                "updated_at": utc_now_iso(),
                "source": "downloaded-unverified",
                "verified": False,
            }
            self._manifest["last_error"] = ""
            self._write_manifest()
            return UpdateResult(
                True,
                "ffmpeg",
                "ffmpeg и ffprobe установлены. Источник без публичной checksum-ссылки, поэтому помечен как unverified.",
                version,
                str(target_dir),
                verified=False,
            )
        except Exception as exc:
            self.last_error = str(exc)
            self._manifest["last_error"] = str(exc)
            self._write_manifest()
            log.exception("ffmpeg update failed")
            return UpdateResult(False, "ffmpeg", "Не удалось обновить ffmpeg.", error=str(exc), verified=False)
        finally:
            shutil.rmtree(staging, ignore_errors=True)

    def update_all(self) -> UpdateResult:
        ytdlp_result = self.update_ytdlp()
        ffmpeg_result = self.update_ffmpeg() if self._is_windows() else UpdateResult(True, "ffmpeg", "ffmpeg update skipped on this platform")
        if ytdlp_result.ok and ffmpeg_result.ok:
            return UpdateResult(True, "all", "Инструменты обновлены.")
        errors = "; ".join(result.error or result.message for result in (ytdlp_result, ffmpeg_result) if not result.ok)
        return UpdateResult(False, "all", "Не все инструменты удалось обновить.", error=errors)

    def repair_runtime(self) -> ToolchainStatus:
        self.ensure_runtime()
        status = self.get_status()
        if not status.ytdlp.exists:
            self.update_ytdlp()
        if self._is_windows() and (not status.ffmpeg.exists or not status.ffprobe.exists):
            self.update_ffmpeg()
        return self.get_status()

    def set_auto_update_enabled(self, enabled: bool) -> None:
        self._manifest["auto_update_enabled"] = bool(enabled)
        self._write_manifest()

    def _copy_or_find_ytdlp(self) -> None:
        target = self.paths.ytdlp_dir / self._exe_name("yt-dlp")
        if target.exists():
            return

        bundled = self.bundled_dir() / self._exe_name("yt-dlp")
        if bundled.exists():
            self._copy_tool(bundled, target)
            self._manifest["yt_dlp"] = {
                "version": self._run_version(target, ["--version"]) or "",
                "path": str(target),
                "updated_at": utc_now_iso(),
                "source": "bundled",
                "verified": True,
            }
            return

        system = shutil.which("yt-dlp.exe" if self._is_windows() else "yt-dlp")
        if system:
            try:
                self._copy_tool(Path(system), target)
                source = "system-copy"
            except Exception:
                target = Path(system)
                source = "system"
            self._manifest["yt_dlp"] = {
                "version": self._run_version(target, ["--version"]) or "",
                "path": str(target),
                "updated_at": utc_now_iso(),
                "source": source,
                "verified": False,
            }

    def _copy_or_find_ffmpeg(self) -> None:
        targets = {
            self._exe_name("ffmpeg"): self.paths.ffmpeg_bin_dir / self._exe_name("ffmpeg"),
            self._exe_name("ffprobe"): self.paths.ffmpeg_bin_dir / self._exe_name("ffprobe"),
        }
        if all(target.exists() for target in targets.values()):
            return

        bundled = self.bundled_dir()
        copied = False
        for name, target in targets.items():
            source = bundled / name
            if source.exists() and not target.exists():
                self._copy_tool(source, target)
                copied = True

        if copied:
            ffmpeg_raw_version = self._run_version(targets[self._exe_name("ffmpeg")], ["-version"])
            self._manifest["ffmpeg"] = {
                "version": self._parse_ffmpeg_version(ffmpeg_raw_version),
                "path": str(targets[self._exe_name("ffmpeg")]),
                "updated_at": utc_now_iso(),
                "source": "bundled",
                "verified": True,
            }
            self._manifest["ffprobe"] = {
                "version": self._run_version(targets[self._exe_name("ffprobe")], ["-version"]) or "",
                "path": str(targets[self._exe_name("ffprobe")]),
                "updated_at": utc_now_iso(),
                "source": "bundled",
                "verified": True,
            }
            return

        for binary in ("ffmpeg", "ffprobe"):
            name = self._exe_name(binary)
            system = shutil.which(name)
            if system and not targets[name].exists():
                try:
                    self._copy_tool(Path(system), targets[name])
                    path = targets[name]
                    source = "system-copy"
                except Exception:
                    path = Path(system)
                    source = "system"
                raw_version = self._run_version(path, ["-version"]) or ""
                self._manifest[binary] = {
                    "version": self._parse_ffmpeg_version(raw_version) if binary == "ffmpeg" else raw_version,
                    "path": str(path),
                    "updated_at": utc_now_iso(),
                    "source": source,
                    "verified": False,
                }

    def _copy_tool(self, source: Path, target: Path) -> None:
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
        self._make_executable(target)

    def _read_manifest(self) -> Dict[str, Any]:
        if not self.paths.manifest_path.exists():
            return {"schema": 1, "auto_update_enabled": True}
        try:
            payload = json.loads(self.paths.manifest_path.read_text(encoding="utf-8"))
            if isinstance(payload, dict):
                payload.setdefault("schema", 1)
                payload.setdefault("auto_update_enabled", True)
                return payload
        except Exception:
            pass
        return {"schema": 1, "auto_update_enabled": True}

    def _write_manifest(self) -> None:
        self.paths.runtime_dir.mkdir(parents=True, exist_ok=True)
        self.paths.manifest_path.write_text(
            json.dumps(self._manifest, ensure_ascii=False, indent=2),
            encoding="utf-8",
        )

    def _update_manifest_from_status(self, status: ToolchainStatus) -> None:
        changed = False
        if self._manifest.get("schema") != 1:
            self._manifest["schema"] = 1
            changed = True
        if self._manifest.get("auto_update_enabled", True) != status.auto_update_enabled:
            self._manifest["auto_update_enabled"] = status.auto_update_enabled
            changed = True
        for key, info in (("yt_dlp", status.ytdlp), ("ffmpeg", status.ffmpeg), ("ffprobe", status.ffprobe)):
            if info.exists and self._merge_manifest_tool(key, info):
                changed = True
        if changed or not self.paths.manifest_path.exists():
            self._write_manifest()

    def _merge_manifest_tool(self, key: str, info: ToolInfo) -> bool:
        current = self._manifest_tool(key)
        entry = {
            "version": info.version or str(current.get("version", "")),
            "path": info.path,
            "updated_at": str(current.get("updated_at") or utc_now_iso()),
            "source": info.source,
            "verified": bool(info.verified),
        }
        if current == entry:
            return False
        self._manifest[key] = entry
        return True

    def _manifest_tool(self, key: str) -> Dict[str, Any]:
        value = self._manifest.get(key)
        return value if isinstance(value, dict) else {}

    def _tool_version(self, manifest_key: str, path: Optional[Path], args: list[str], refresh: bool) -> str:
        if not path or not path.exists():
            return ""
        if not refresh:
            return self._cached_tool_version(manifest_key, path)
        return self._run_version(path, args)

    def _ffmpeg_version(self, path: Optional[Path], refresh: bool) -> str:
        if not path or not path.exists():
            return ""
        if not refresh:
            return self._cached_tool_version("ffmpeg", path)
        return self._parse_ffmpeg_version(self._run_version(path, ["-version"]))

    def _cached_tool_version(self, manifest_key: str, path: Path) -> str:
        manifest = self._manifest_tool(manifest_key)
        manifest_path = str(manifest.get("path", ""))
        if manifest_path and self._same_path(Path(manifest_path), path):
            return str(manifest.get("version", ""))
        return ""

    def _parse_ffmpeg_version(self, output: str) -> str:
        if not output:
            return ""
        first_line = output.splitlines()[0] if output.splitlines() else output
        parts = first_line.split()
        if len(parts) >= 3 and parts[0].lower() == "ffmpeg":
            return parts[2]
        return first_line[:80]

    def _tool_source(self, manifest_key: str, path: Optional[Path]) -> str:
        if not path:
            return "missing"
        manifest_path = str(self._manifest.get(manifest_key, {}).get("path", ""))
        if manifest_path and self._same_path(Path(manifest_path), path):
            return str(self._manifest.get(manifest_key, {}).get("source", "runtime"))
        if self.paths.runtime_dir in path.parents:
            return "runtime"
        return "system"

    def _same_path(self, left: Path, right: Path) -> bool:
        try:
            return left.resolve() == right.resolve()
        except Exception:
            return left == right

    def _run_version(self, path: Optional[Path], args: list[str]) -> str:
        if not path:
            return ""
        try:
            completed = subprocess.run(
                [str(path), *args],
                check=False,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                timeout=10,
                **self._silent_subprocess_kwargs(),
            )
            return (completed.stdout or "").strip()
        except Exception as exc:
            log.warning("version check failed for %s: %s", path, exc)
            return ""

    def _silent_subprocess_kwargs(self) -> Dict[str, Any]:
        if not self._is_windows():
            return {}

        kwargs: Dict[str, Any] = {"creationflags": getattr(subprocess, "CREATE_NO_WINDOW", 0x08000000)}
        startupinfo_class = getattr(subprocess, "STARTUPINFO", None)
        if startupinfo_class:
            startupinfo = startupinfo_class()
            startupinfo.dwFlags |= getattr(subprocess, "STARTF_USESHOWWINDOW", 1)
            startupinfo.wShowWindow = 0
            kwargs["startupinfo"] = startupinfo
        return kwargs

    def _is_update_due(self) -> bool:
        raw = self._manifest.get("last_update_check")
        if not raw:
            return True
        try:
            checked = datetime.fromisoformat(str(raw).replace("Z", "+00:00"))
        except Exception:
            return True
        return datetime.now(timezone.utc) - checked >= timedelta(hours=24)

    def _fetch_latest_ytdlp_version(self) -> str:
        with self._open_url(YTDLP_RELEASE_API) as response:
            payload = json.loads(response.read().decode("utf-8"))
        tag = str(payload.get("tag_name") or "").strip()
        return tag[1:] if tag.startswith("v") else tag

    def _fetch_checksum(self, source: DownloadSource, filename: str) -> str:
        if not source.checksum_url:
            return ""
        try:
            with self._open_url(source.checksum_url) as response:
                text = response.read().decode("utf-8", errors="replace")
        except Exception as exc:
            log.warning("checksum fetch failed: %s", exc)
            return ""

        for line in text.splitlines():
            parts = line.strip().split()
            if len(parts) >= 2 and parts[-1].replace("*", "").endswith(filename):
                return parts[0]
        return ""

    def _download(self, url: str, target: Path) -> None:
        target.parent.mkdir(parents=True, exist_ok=True)
        tmp = target.with_suffix(target.suffix + ".download")
        try:
            with self._open_url(url, timeout=60) as response, tmp.open("wb") as file:
                while True:
                    chunk = response.read(1024 * 1024)
                    if not chunk:
                        break
                    file.write(chunk)
            if tmp.stat().st_size <= 0:
                raise RuntimeError("Скачанный файл пустой.")
            tmp.replace(target)
        except urllib.error.HTTPError as exc:
            raise RuntimeError(f"HTTP {exc.code}: {exc.reason}") from exc
        except urllib.error.URLError as exc:
            raise RuntimeError(f"Ошибка сети: {exc.reason}") from exc
        finally:
            tmp.unlink(missing_ok=True)

    def _open_url(self, url: str, timeout: int = 20):
        request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
        return urllib.request.urlopen(request, timeout=timeout)

    def _prepare_staging(self, name: str) -> Path:
        root = self.paths.runtime_dir / ".staging"
        root.mkdir(parents=True, exist_ok=True)
        return Path(tempfile.mkdtemp(prefix=f"{name}-", dir=str(root)))

    def _extract_ffmpeg_tools(self, archive: Path, target_dir: Path) -> Tuple[Path, Path]:
        target_dir.mkdir(parents=True, exist_ok=True)
        ffmpeg_target = target_dir / "ffmpeg.exe"
        ffprobe_target = target_dir / "ffprobe.exe"
        with zipfile.ZipFile(archive) as zf:
            names = zf.namelist()
            ffmpeg_name = next((name for name in names if name.replace("\\", "/").endswith("/bin/ffmpeg.exe")), "")
            ffprobe_name = next((name for name in names if name.replace("\\", "/").endswith("/bin/ffprobe.exe")), "")
            if not ffmpeg_name or not ffprobe_name:
                raise RuntimeError("В архиве ffmpeg не найдены ffmpeg.exe и ffprobe.exe.")
            with zf.open(ffmpeg_name) as source, ffmpeg_target.open("wb") as dest:
                shutil.copyfileobj(source, dest)
            with zf.open(ffprobe_name) as source, ffprobe_target.open("wb") as dest:
                shutil.copyfileobj(source, dest)
        return ffmpeg_target, ffprobe_target

    def _atomic_install_file(self, staged: Path, target: Path) -> None:
        target.parent.mkdir(parents=True, exist_ok=True)
        backup = target.with_suffix(target.suffix + ".bak")
        if backup.exists():
            backup.unlink()
        try:
            if target.exists():
                target.replace(backup)
            staged.replace(target)
            self._make_executable(target)
            backup.unlink(missing_ok=True)
        except Exception:
            if backup.exists() and not target.exists():
                backup.replace(target)
            raise

    def _sha256(self, path: Path) -> str:
        digest = hashlib.sha256()
        with path.open("rb") as file:
            for chunk in iter(lambda: file.read(1024 * 1024), b""):
                digest.update(chunk)
        return digest.hexdigest()

    def _make_executable(self, path: Path) -> None:
        if self._is_windows():
            return
        mode = path.stat().st_mode
        path.chmod(mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)

    def _exe_name(self, stem: str) -> str:
        return f"{stem}.exe" if self._is_windows() else stem

    def _is_windows(self) -> bool:
        return os.name == "nt" or sys.platform.startswith("win")

    def _ytdlp_source(self) -> DownloadSource:
        if self._is_windows():
            return YTDLP_WINDOWS
        if sys.platform == "darwin":
            return YTDLP_MACOS
        return YTDLP_POSIX
