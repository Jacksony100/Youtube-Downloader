from core.models import FORMAT_PRESETS, get_format_preset


def test_format_presets_include_required_choices():
    keys = {preset.key for preset in FORMAT_PRESETS}

    assert {"best", "1080p", "720p", "480p", "mp3"}.issubset(keys)


def test_best_format_selector():
    preset = get_format_preset("best")

    assert preset.label == "Лучшее"
    assert preset.selector == "bestvideo+bestaudio/best"
    assert preset.extract_audio is False


def test_mp3_format_uses_audio_extraction():
    preset = get_format_preset("mp3")

    assert preset.selector == "bestaudio/best"
    assert preset.extract_audio is True
    assert preset.extension == "mp3"


def test_unknown_format_falls_back_to_best():
    assert get_format_preset("unknown").key == "best"
