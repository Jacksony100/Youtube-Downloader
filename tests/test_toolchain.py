from core.paths import AppPaths
from core.toolchain import ToolchainManager


class EmptyBundledToolchainManager(ToolchainManager):
    def __init__(self, paths, bundled_dir):
        self._test_bundled_dir = bundled_dir
        super().__init__(paths)

    def bundled_dir(self):
        return self._test_bundled_dir


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
