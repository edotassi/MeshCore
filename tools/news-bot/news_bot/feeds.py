"""Download e parsing dei feed RSS/Atom."""

from __future__ import annotations

import calendar
import urllib.request
from dataclasses import dataclass

import feedparser

from .config import Feed


class FeedError(RuntimeError):
    pass


@dataclass(frozen=True)
class Entry:
    guid: str
    title: str
    published: int | None  # epoch secondi, se presente


def parse_feed(content: bytes) -> list[Entry]:
    parsed = feedparser.parse(content)
    if parsed.bozo and not parsed.entries:
        raise FeedError(f"feed non leggibile: {parsed.get('bozo_exception')}")
    entries = []
    for e in parsed.entries:
        title = e.get("title") or ""
        guid = e.get("id") or e.get("link") or title
        if not guid:
            continue
        t = e.get("published_parsed") or e.get("updated_parsed")
        entries.append(Entry(str(guid), title, calendar.timegm(t) if t else None))
    return entries


def fetch_feed(feed: Feed, user_agent: str, timeout: int) -> list[Entry]:
    req = urllib.request.Request(
        feed.url,
        headers={
            "User-Agent": user_agent,
            "Accept": "application/rss+xml, application/atom+xml, application/xml;q=0.9, */*;q=0.5",
        },
    )
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            content = resp.read(5 * 1024 * 1024)
    except Exception as exc:
        raise FeedError(f"download fallito: {exc}") from exc
    return parse_feed(content)
