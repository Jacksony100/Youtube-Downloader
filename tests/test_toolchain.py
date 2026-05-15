from core.models import utc_now_iso
from core.paths import AppPaths
from core.toolchain import ToolchainManager


class EmptyBundledToolchainManager(ToolchainManager):
    def __init__(self, paths, bundled_dir):
        self._test_bundled_dir = bundled_dir
        super().__init__(paths)

    def bundled_dir(self):
        return self._test_bundled_dir


class CountingToolchainManager(EmptyBundledToolchainManager):
    def __init__(self, paths, bundled_dir):
        self.version_calls = 0
        super().__init__(paths, bundled_dir)

    def _run_version(self, path, args):
        self.version_calls += 1
        return "unexpected-version-call"


def make_runtime_manifest(manager, paths):
    ytdlp = paths.ytdlp_dir / manager._exe_name("yt-dlp")
    ffmpeg = paths.ffmpeg_bin_dir / manager._exe_name("ffmpeg")
    ffprobe = paths.ffmpeg_bin_dir / manager._exe_name("ffprobe")
    for tool in (ytdlp, ffmpeg, ffprobe):
        tool.parent.mkdir(parents=True, exist_ok=True)
        tool.write_text("", encoding="utf-8")

    manager._manifest = {
        "schema": 1,
        "auto_update_enabled": True,
        "last_update_check": utc_now_iso(),
        "yt_dlp": {"version": "2026.01.01", "path": str(ytdlp), "source": "runtime", "verified": True},
        "ffmpeg": {"version": "7.1", "path": str(ffmpeg), "source": "runtime", "verified": True},
        "ffprobe": {"version": "ffprobe 7.1", "path": str(ffprobe), "source": "runtime", "verified": True},
    }


def test_toolchain_ensure_runtime_without_bundled_tools(tmp_path, monkeypatch):
    monkeypatch.setenv("PATH", "")
    paths = AppPaths.from_base(tmp_path)
    manager = EmptyBundledToolchainManager(paths, tmp_path / "empty-bundled")

    status = manager.ensure_runtime()

    assert paths.runtime_dir.exists()
    assert paths.manifest_path.exists()
    assert status.runtime_dir == str(paths.runtime_dir)
    assert status.ytdlp.name == "yt-dlp"
    assert status.ffmpeg.name == "ffmpeg"


def test_toolchain_status_uses_manifest_version_cache(tmp_path, monkeypatch):
    monkeypatch.setenv("PATH", "")
    paths = AppPaths.from_base(tmp_path)
    manager = CountingToolchainManager(paths, tmp_path / "empty-bundled")
    make_runtime_manifest(manager, paths)

    status = manager.get_status()

    assert manager.version_calls == 0
    assert status.ytdlp.version == "2026.01.01"
    assert status.ffmpeg.version == "7.1"
    assert status.ffprobe.version == "ffprobe 7.1"


def test_skipped_update_check_uses_manifest_version_cache(tmp_path, monkeypatch):
    monkeypatch.setenv("PATH", "")
    paths = AppPaths.from_base(tmp_path)
    manager = CountingToolchainManager(paths, tmp_path / "empty-bundled")
    make_runtime_manifest(manager, paths)

    result = manager.check_updates(force=False)

    assert result.skipped is True
    assert manager.version_calls == 0
    assert result.ytdlp_current == "2026.01.01"
    assert result.ffmpeg_current == "7.1"
