from __future__ import annotations

import json
from pathlib import Path
from typing import Any, Dict, Optional

from core.paths import AppPaths


OLD_CONFIG_FILE = Path.home() / ".video_downloader_config.json"


DEFAULT_SETTINGS: Dict[str, Any] = {
    "output_dir": str(Path.home() / "Downloads"),
    "default_format": "best",
    "parallel_downloads": 2,
    "auto_open_file": False,
    "auto_update_tools": True,
    "last_update_check": "",
    "window_geometry": "",
    "active_page": "downloads",
    "theme": "dark",
}


class AppSettings:
    def __init__(self, settings_path: Optional[Path] = None):
        self.paths = AppPaths.default()
        self.settings_path = Path(settings_path) if settings_path else self.paths.settings_path
        self._values: Dict[str, Any] = dict(DEFAULT_SETTINGS)
        self.load()

    def load(self) -> None:
        self._values = dict(DEFAULT_SETTINGS)

        migrated = self._load_legacy_settings()
        if migrated:
            self._values.update(migrated)

        if self.settings_path.exists():
            try:
                payload = json.loads(self.settings_path.read_text(encoding="utf-8"))
                if isinstance(payload, dict):
                    self._values.update(payload)
            except Exception:
                pass

        self._normalize()

    def save(self) -> None:
        self._normalize()
        self.settings_path.parent.mkdir(parents=True, exist_ok=True)
        self.settings_path.write_text(
            json.dumps(self._values, ensure_ascii=False, indent=2),
            encoding="utf-8",
        )

    def get(self, key: str, default: Any = None) -> Any:
        return self._values.get(key, default)

    def set(self, key: str, value: Any) -> None:
        self._values[key] = value
        self._normalize()

    def update(self, **kwargs: Any) -> None:
        self._values.update(kwargs)
        self._normalize()

    def as_dict(self) -> Dict[str, Any]:
        self._normalize()
        return dict(self._values)

    def reset(self) -> None:
        self._values = dict(DEFAULT_SETTINGS)
        self.save()

    def _normalize(self) -> None:
        try:
            self._values["parallel_downloads"] = max(1, min(5, int(self._values.get("parallel_downloads", 2))))
        except Exception:
            self._values["parallel_downloads"] = 2

        if self._values.get("default_format") not in {"best", "1080p", "720p", "480p", "mp3"}:
            self._values["default_format"] = "best"

        output_dir = str(self._values.get("output_dir") or DEFAULT_SETTINGS["output_dir"])
        self._values["output_dir"] = output_dir
        self._values["auto_open_file"] = bool(self._values.get("auto_open_file"))
        self._values["auto_update_tools"] = bool(self._values.get("auto_update_tools"))
        self._values["theme"] = "dark"

    def _load_legacy_settings(self) -> Dict[str, Any]:
        if not OLD_CONFIG_FILE.exists():
            return {}

        try:
            legacy = json.loads(OLD_CONFIG_FILE.read_text(encoding="utf-8"))
        except Exception:
            return {}

        if not isinstance(legacy, dict):
            return {}

        format_index = int(legacy.get("format_index", 0) or 0)
        format_keys = ["best", "1080p", "720p", "480p", "mp3"]
        return {
            "output_dir": legacy.get("download_folder") or DEFAULT_SETTINGS["output_dir"],
            "default_format": format_keys[format_index] if 0 <= format_index < len(format_keys) else "best",
            "parallel_downloads": legacy.get("max_parallel", 2),
            "auto_open_file": legacy.get("auto_open", False),
        }
