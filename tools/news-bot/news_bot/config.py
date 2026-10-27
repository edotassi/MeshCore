"""Configurazione: feeds.yaml e variabili d'ambiente."""

from __future__ import annotations

import os
import re
from dataclasses import dataclass, field
from datetime import datetime
from zoneinfo import ZoneInfo

import yaml

from .protocol import MAX_SOURCE_BYTES, decode_key


class ConfigError(ValueError):
    pass


@dataclass(frozen=True)
class Feed:
    name: str
    url: str
    label: str
    max_per_run: int = 2
    include: re.Pattern | None = None
    exclude: re.Pattern | None = None

    def accepts(self, title: str) -> bool:
        if self.include and not self.include.search(title):
            return False
        if self.exclude and self.exclude.search(title):
            return False
        return True


@dataclass(frozen=True)
class QuietHours:
    start: int
    end: int
    tz: ZoneInfo

    def is_quiet(self, epoch: float) -> bool:
        hour = datetime.fromtimestamp(epoch, self.tz).hour
        if self.start < self.end:
            return self.start <= hour < self.end
        return hour >= self.start or hour < self.end  # intervallo a cavallo della mezzanotte


def parse_quiet_hours(spec: str | None, tz: ZoneInfo) -> QuietHours | None:
    if not spec or not spec.strip():
        return None
    m = re.fullmatch(r"\s*(\d{1,2})\s*-\s*(\d{1,2})\s*", spec)
    if not m:
        raise ConfigError(f"QUIET_HOURS non valido: {spec!r} (atteso es. '23-7')")
    start, end = int(m.group(1)), int(m.group(2))
    if not (0 <= start <= 23 and 0 <= end <= 24) or start == end:
        raise ConfigError(f"QUIET_HOURS fuori intervallo: {spec!r}")
    return QuietHours(start, end % 24 if end == 24 else end, tz)


def _compile(value, what: str, feed: str) -> re.Pattern | None:
    if value in (None, ""):
        return None
    try:
        return re.compile(str(value), re.IGNORECASE)
    except re.error as exc:
        raise ConfigError(f"feed {feed!r}: regex {what} non valida: {exc}") from exc


def load_feeds(path: str) -> list[Feed]:
    try:
        with open(path, encoding="utf-8") as fh:
            data = yaml.safe_load(fh)
    except OSError as exc:
        raise ConfigError(f"impossibile leggere {path}: {exc}") from exc
    except yaml.YAMLError as exc:
        raise ConfigError(f"YAML non valido in {path}: {exc}") from exc
    items = data.get("feeds") if isinstance(data, dict) else None
    if not isinstance(items, list) or not items:
        raise ConfigError(f"{path}: manca la lista 'feeds'")
    feeds: list[Feed] = []
    seen: set[str] = set()
    for raw in items:
        if not isinstance(raw, dict):
            raise ConfigError("ogni feed deve essere una mappa")
        name, url = raw.get("name"), raw.get("url")
        if not name or not url:
            raise ConfigError(f"feed senza name/url: {raw}")
        name = str(name)
        if name in seen:
            raise ConfigError(f"nome feed duplicato: {name}")
        seen.add(name)
        label = str(raw.get("label") or name)
        n = len(label.encode("utf-8"))
        if not 1 <= n <= MAX_SOURCE_BYTES or "\n" in label:
            raise ConfigError(f"feed {name!r}: label deve essere 1..{MAX_SOURCE_BYTES} byte")
        try:
            max_per_run = int(raw.get("max_per_run", 2))
        except (TypeError, ValueError) as exc:
            raise ConfigError(f"feed {name!r}: max_per_run non valido") from exc
        if max_per_run < 1:
            raise ConfigError(f"feed {name!r}: max_per_run deve essere >= 1")
        feeds.append(
            Feed(
                name=name,
                url=str(url),
                label=label,
                max_per_run=max_per_run,
                include=_compile(raw.get("include"), "include", name),
                exclude=_compile(raw.get("exclude"), "exclude", name),
            )
        )
    return feeds


def _int_env(env, name: str, default: int, minimum: int = 1) -> int:
    raw = env.get(name)
    if raw is None or raw.strip() == "":
        return default
    try:
        value = int(raw)
    except ValueError as exc:
        raise ConfigError(f"{name} deve essere un intero") from exc
    if value < minimum:
        raise ConfigError(f"{name} deve essere >= {minimum}")
    return value


@dataclass(frozen=True)
class Settings:
    hmac_key: bytes
    mqtt_host: str = "mosquitto"
    mqtt_port: int = 1883
    mqtt_user: str = ""
    mqtt_password: str = ""
    topic: str = "bbs/news"
    poll_minutes: int = 15
    max_per_run: int = 5
    quiet_hours: QuietHours | None = None
    state_path: str = "/data/state.db"
    feeds_file: str = "/app/feeds.yaml"
    user_agent: str = "MeshCore-news-bot/1.0 (+https://github.com/meshcore-dev/MeshCore)"
    http_timeout: int = 20
    publish_timeout: int = 15
    max_age_seconds: int = 6 * 3600

    @classmethod
    def from_env(cls, env=None, require_key: bool = True) -> "Settings":
        env = os.environ if env is None else env
        key_b64 = env.get("NEWS_HMAC_KEY", "")
        if key_b64.strip():
            key = decode_key(key_b64)
        elif require_key:
            raise ConfigError("NEWS_HMAC_KEY mancante")
        else:
            key = bytes(32)
        tz_name = env.get("TZ") or "Europe/Rome"
        try:
            tz = ZoneInfo(tz_name)
        except Exception as exc:
            raise ConfigError(f"TZ non valido: {tz_name}") from exc
        topic = env.get("NEWS_TOPIC") or "bbs/news"
        return cls(
            hmac_key=key,
            mqtt_host=env.get("MQTT_HOST") or "mosquitto",
            mqtt_port=_int_env(env, "MQTT_PORT", 1883),
            mqtt_user=env.get("MQTT_PUBLISHER_USER", ""),
            mqtt_password=env.get("MQTT_PUBLISHER_PASSWORD", ""),
            topic=topic,
            poll_minutes=_int_env(env, "POLL_MINUTES", 15),
            max_per_run=_int_env(env, "MAX_PER_RUN", 5),
            quiet_hours=parse_quiet_hours(env.get("QUIET_HOURS"), tz),
            state_path=env.get("STATE_PATH") or "/data/state.db",
            feeds_file=env.get("FEEDS_FILE") or "/app/feeds.yaml",
        )
