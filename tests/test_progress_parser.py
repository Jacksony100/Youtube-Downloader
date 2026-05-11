from core.downloader import parse_progress_line


def test_parse_progress_template_line():
    progress = parse_progress_line("download: 12.3%|1.2MiB/s|00:04|12345|100000")

    assert progress is not None
    assert progress.percent == 12.3
    assert progress.speed_text == "1.2MiB/s"
    assert progress.eta_text == "00:04"
    assert progress.downloaded_bytes == 12345
    assert progress.total_bytes == 100000


def test_parse_progress_line_tolerates_na_values():
    progress = parse_progress_line("download: NA|NA|NA|NA|")

    assert progress is not None
    assert progress.percent == 0.0
    assert progress.speed_text == ""
    assert progress.eta_text == ""
    assert progress.downloaded_bytes is None
    assert progress.total_bytes is None


def test_parse_progress_line_ignores_unrelated_output():
    assert parse_progress_line("") is None
    assert parse_progress_line("[download] Destination: file.mp4") is None
    assert parse_progress_line("ERROR: unavailable") is None
