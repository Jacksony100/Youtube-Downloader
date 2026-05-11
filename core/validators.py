from __future__ import annotations

from urllib.parse import urlparse


def is_http_url(url: str) -> bool:
    try:
        parsed = urlparse((url or "").strip())
    except Exception:
        return False
    return parsed.scheme in {"http", "https"} and bool(parsed.netloc)


def sanitize_error_message(message: str) -> str:
    text = (message or "").strip()
    lowered = text.lower()
    if "private" in lowered:
        return "Видео приватное или недоступно для этого аккаунта."
    if "unavailable" in lowered or "not available" in lowered:
        return "Видео недоступно. Возможно, оно удалено или ограничено платформой."
    if "age" in lowered or "region" in lowered or "geo" in lowered:
        return "Видео ограничено по возрасту или региону. Приложение не обходит такие ограничения."
    if "ffmpeg" in lowered and ("not found" in lowered or "missing" in lowered):
        return "FFmpeg не найден. Откройте раздел «Инструменты» и переустановите runtime-инструменты."
    if "http error 429" in lowered or "too many requests" in lowered:
        return "Платформа временно ограничила запросы. Попробуйте позже или проверьте сетевой маршрут."
    if "timeout" in lowered or "connection" in lowered or "network" in lowered:
        return "Сетевая ошибка. Проверьте подключение и попробуйте снова."
    return text or "Неизвестная ошибка."
