from pathlib import Path

from core.paths import AppPaths, get_app_base_dir


def test_windows_base_dir_uses_localappdata(tmp_path):
    base = get_app_base_dir(
        app_name="VideoDownloaderPro",
        platform_name="win32",
        env={"LOCALAPPDATA": str(tmp_path)},
        home=tmp_path / "home",
    )

    assert base == tmp_path / "VideoDownloaderPro"


def test_linux_base_dir_uses_xdg_data_home(tmp_path):
    base = get_app_base_dir(
        app_name="VideoDownloaderPro",
        platform_name="linux",
        env={"XDG_DATA_HOME": str(tmp_path / "xdg")},
        home=tmp_path / "home",
    )

    assert base == tmp_path / "xdg" / "VideoDownloaderPro"


def test_app_paths_runtime_layout(tmp_path):
    paths = AppPaths.from_base(tmp_path)

    assert paths.runtime_dir == tmp_path / "runtime"
    assert paths.ytdlp_dir == tmp_path / "runtime" / "yt-dlp"
    assert paths.ffmpeg_dir == tmp_path / "runtime" / "ffmpeg"
    assert paths.ffmpeg_bin_dir == tmp_path / "runtime" / "ffmpeg" / "bin"
    assert paths.data_dir == tmp_path / "data"
    assert paths.logs_dir == tmp_path / "logs"
    assert paths.cache_dir == tmp_path / "cache"
    assert paths.manifest_path == tmp_path / "runtime" / "manifest.json"


def test_app_paths_create_directories(tmp_path):
    paths = AppPaths.from_base(tmp_path)
    paths.ensure()

    for directory in (
        paths.runtime_dir,
        paths.ytdlp_dir,
        paths.ffmpeg_bin_dir,
        paths.data_dir,
        paths.logs_dir,
        paths.cache_dir,
    ):
        assert directory.exists()
        assert directory.is_dir()
