from core.history import HistoryStore
from core.models import DownloadRecord


def make_record(task_id="task-1", title="Video title", status="completed"):
    return DownloadRecord(
        id=task_id,
        url="https://example.com/watch?v=1",
        title=title,
        uploader="Author",
        duration=120,
        thumbnail_url="https://example.com/thumb.jpg",
        output_path="/tmp/video.mp4",
        format_label="Лучшее",
        status=status,
        created_at="2026-05-11T12:00:00Z",
        finished_at="2026-05-11T12:05:00Z",
        error="",
    )


def test_history_add_update_and_query(tmp_path):
    store = HistoryStore(tmp_path / "history.sqlite")
    store.add_or_update(make_record())
    store.add_or_update(make_record(title="Updated title"))

    records = store.list(query="updated")

    assert len(records) == 1
    assert records[0].title == "Updated title"
    assert records[0].status == "completed"


def test_history_mark_error(tmp_path):
    store = HistoryStore(tmp_path / "history.sqlite")
    store.add_or_update(make_record(status="queued"))
    store.mark_error("task-1", "Network error")

    record = store.list()[0]
    assert record.status == "failed"
    assert record.error == "Network error"
    assert record.finished_at


def test_history_delete_and_clear(tmp_path):
    store = HistoryStore(tmp_path / "history.sqlite")
    store.add_or_update(make_record("task-1"))
    store.add_or_update(make_record("task-2"))

    store.delete("task-1")
    assert [record.id for record in store.list()] == ["task-2"]

    store.clear()
    assert store.list() == []
