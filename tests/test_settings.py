from core.settings import AppSettings


def test_settings_save_and_reload(tmp_path):
    settings_path = tmp_path / "settings.json"
    settings = AppSettings(settings_path=settings_path)

    settings.set("default_format", "mp3")
    settings.set("parallel_downloads", 5)
    settings.set("auto_open_file", True)
    settings.save()

    reloaded = AppSettings(settings_path=settings_path)
    assert reloaded.get("default_format") == "mp3"
    assert reloaded.get("parallel_downloads") == 5
    assert reloaded.get("auto_open_file") is True


def test_settings_clamps_parallel_downloads(tmp_path):
    settings = AppSettings(settings_path=tmp_path / "settings.json")

    settings.set("parallel_downloads", 99)

    assert settings.get("parallel_downloads") == 5
