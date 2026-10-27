"""Logica di una singola esecuzione: dai feed ai messaggi pubblicati."""

from __future__ import annotations

import logging
import time
from dataclasses import dataclass

from .config import Feed, Settings
from .feeds import Entry
from .protocol import ProtocolError, build_message, make_id
from .state import State
from .text import MAX_TITLE_BYTES, clean_title, truncate_bytes

log = logging.getLogger(__name__)


@dataclass
class Item:
    feed: Feed
    id: str
    title: str
    sort_ts: int
    published: int | None


@dataclass
class RunResult:
    published: int = 0
    skipped: int = 0
    held: int = 0
    failed: bool = False


def _items_for(feed: Feed, entries: list[Entry], now: int) -> list[Item]:
    items, ids = [], set()
    # i feed sono dal piu' recente: invertendo, a parita' di data si pubblica il piu' vecchio per primo
    for e in reversed(entries):
        title = truncate_bytes(clean_title(e.title), MAX_TITLE_BYTES)
        mid = make_id(feed.name, e.guid)
        if not title or mid in ids:
            continue
        ids.add(mid)
        items.append(Item(feed, mid, title, e.published if e.published is not None else now, e.published))
    items.sort(key=lambda i: i.sort_ts)  # stabile
    return items


def run_once(settings: Settings, feeds: list[Feed], state: State, fetch, publisher,
             now: int | None = None, dry_run: bool = False, out=print) -> RunResult:
    now = int(time.time() if now is None else now)
    result = RunResult()
    quiet = bool(settings.quiet_hours and settings.quiet_hours.is_quiet(now))
    if quiet:
        log.info("ore silenziose: nessuna pubblicazione in questa esecuzione")
    candidates: list[Item] = []

    for feed in feeds:
        try:
            entries = fetch(feed)
        except Exception as exc:
            log.error("feed %s: errore, salto (%s)", feed.name, exc)
            continue
        items = _items_for(feed, entries, now)

        if not state.feed_known(feed.name):
            log.info("feed %s: primo avvio, %d voci segnate come viste senza pubblicare",
                     feed.name, len(items))
            if dry_run:
                out(f"[dry-run] {feed.name}: primo avvio, nessuna pubblicazione. Ultime voci:")
                for it in items[-feed.max_per_run:]:
                    out(f"    {it.title}")
            state.mark_seen_many(feed.name, [i.id for i in items], now)
            state.mark_feed_known(feed.name)
            continue

        new = [i for i in items if not state.is_seen(i.id)]
        rejected = [i for i in new if not feed.accepts(i.title)]
        if rejected:
            state.mark_seen_many(feed.name, [i.id for i in rejected], now)
        new = [i for i in new if feed.accepts(i.title)]

        fresh = []
        for i in new:
            age = now - (i.published if i.published is not None else state.first_seen(i.id, now))
            if age > settings.max_age_seconds:
                log.info("feed %s: voce troppo vecchia (%d min), scartata: %s", feed.name, age // 60, i.title)
                state.mark_seen(feed.name, i.id, now)
                result.skipped += 1
            else:
                fresh.append(i)

        if quiet:
            result.held += len(fresh)
            continue

        if len(fresh) > feed.max_per_run:
            extra = fresh[:-feed.max_per_run]  # restano le piu' recenti
            log.info("feed %s: %d voci oltre max_per_run, scartate", feed.name, len(extra))
            state.mark_seen_many(feed.name, [i.id for i in extra], now)
            result.skipped += len(extra)
            fresh = fresh[-feed.max_per_run:]
        candidates.extend(fresh)

    candidates.sort(key=lambda i: i.sort_ts)
    if len(candidates) > settings.max_per_run:
        extra = candidates[:-settings.max_per_run]
        log.info("limite globale MAX_PER_RUN=%d: %d voci scartate", settings.max_per_run, len(extra))
        for i in extra:
            state.mark_seen(i.feed.name, i.id, now)
        result.skipped += len(extra)
        candidates = candidates[-settings.max_per_run:]

    for i in candidates:
        try:
            payload = build_message(settings.hmac_key, i.id, now, i.feed.label, i.title)
        except ProtocolError as exc:
            log.error("voce non valida (%s), scartata: %s", exc, i.title)
            state.mark_seen(i.feed.name, i.id, now)
            result.skipped += 1
            continue
        if not publisher.publish(payload):
            log.error("pubblicazione non confermata, mi fermo: riprovo alla prossima esecuzione")
            result.failed = True
            break
        state.mark_seen(i.feed.name, i.id, now)  # solo dopo l'ack del broker
        result.published += 1
        log.info("pubblicato [%s] %s", i.feed.label, i.title)

    state.prune(now)
    log.info("esecuzione terminata: %d pubblicati, %d scartati, %d in attesa",
             result.published, result.skipped, result.held)
    return result
