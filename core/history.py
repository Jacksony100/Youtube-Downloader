from __future__ import annotations

import sqlite3
from pathlib import Path
from typing import List, Optional

from core.models import DownloadRecord, utc_now_iso
from core.paths import AppPaths


class HistoryStore:
    def __init__(self, db_path: Optional[Path] = None):
        self.db_path = Path(db_path) if db_path else AppPaths.default().history_path
        self.db_path.parent.mkdir(parents=True, exist_ok=True)
        self._init_db()

    def _connect(self) -> sqlite3.Connection:
        conn = sqlite3.connect(str(self.db_path))
        conn.row_factory = sqlite3.Row
        return conn

    def _init_db(self) -> None:
        with self._connect() as conn:
            conn.execute(
                """
                CREATE TABLE IF NOT EXISTS downloads (
                    id TEXT PRIMARY KEY,
                    url TEXT NOT NULL,
                    title TEXT NOT NULL,
                    uploader TEXT,
                    duration INTEGER,
                    thumbnail_url TEXT,
                    output_path TEXT,
                    format_label TEXT,
                    status TEXT NOT NULL,
                    created_at TEXT NOT NULL,
                    finished_at TEXT,
                    error TEXT
                )
                """
            )
            conn.execute("CREATE INDEX IF NOT EXISTS idx_downloads_finished_at ON downloads(finished_at)")
            conn.execute("CREATE INDEX IF NOT EXISTS idx_downloads_title ON downloads(title)")

    def add_or_update(self, record: DownloadRecord) -> None:
        with self._connect() as conn:
            conn.execute(
                """
                INSERT INTO downloads (
                    id, url, title, uploader, duration, thumbnail_url, output_path,
                    format_label, status, created_at, finished_at, error
                )
                VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
                ON CONFLICT(id) DO UPDATE SET
                    url=excluded.url,
                    title=excluded.title,
                    uploader=excluded.uploader,
                    duration=excluded.duration,
                    thumbnail_url=excluded.thumbnail_url,
                    output_path=excluded.output_path,
                    format_label=excluded.format_label,
                    status=excluded.status,
                    created_at=excluded.created_at,
                    finished_at=excluded.finished_at,
                    error=excluded.error
                """,
                (
                    record.id,
                    record.url,
                    record.title,
                    record.uploader,
                    record.duration,
                    record.thumbnail_url,
                    record.output_path,
                    record.format_label,
                    record.status,
                    record.created_at,
                    record.finished_at,
                    record.error,
                ),
            )

    def mark_finished(self, task_id: str, output_path: str) -> None:
        with self._connect() as conn:
            conn.execute(
                "UPDATE downloads SET status=?, output_path=?, finished_at=?, error=? WHERE id=?",
                ("completed", output_path, utc_now_iso(), "", task_id),
            )

    def mark_error(self, task_id: str, error: str) -> None:
        with self._connect() as conn:
            conn.execute(
                "UPDATE downloads SET status=?, finished_at=?, error=? WHERE id=?",
                ("failed", utc_now_iso(), error, task_id),
            )

    def list(self, limit: int = 100, query: str = "") -> List[DownloadRecord]:
        params: list[object] = []
        sql = "SELECT * FROM downloads"
        if query.strip():
            sql += " WHERE lower(title) LIKE ? OR lower(url) LIKE ? OR lower(uploader) LIKE ?"
            needle = f"%{query.strip().lower()}%"
            params.extend([needle, needle, needle])
        sql += " ORDER BY COALESCE(finished_at, created_at) DESC LIMIT ?"
        params.append(int(limit))

        with self._connect() as conn:
            rows = conn.execute(sql, params).fetchall()
        return [self._row_to_record(row) for row in rows]

    def delete(self, task_id: str) -> None:
        with self._connect() as conn:
            conn.execute("DELETE FROM downloads WHERE id=?", (task_id,))

    def clear(self) -> None:
        with self._connect() as conn:
            conn.execute("DELETE FROM downloads")

    def _row_to_record(self, row: sqlite3.Row) -> DownloadRecord:
        return DownloadRecord(
            id=row["id"],
            url=row["url"],
            title=row["title"],
            uploader=row["uploader"] or "",
            duration=row["duration"],
            thumbnail_url=row["thumbnail_url"] or "",
            output_path=row["output_path"] or "",
            format_label=row["format_label"] or "",
            status=row["status"],
            created_at=row["created_at"],
            finished_at=row["finished_at"] or "",
            error=row["error"] or "",
        )
