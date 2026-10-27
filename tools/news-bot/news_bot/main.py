"""CLI e ciclo principale."""

from __future__ import annotations

import argparse
import logging
import os
import signal
import threading
import time
from functools import partial

from .config import ConfigError, Settings, load_feeds
from .feeds import fetch_feed
from .publisher import DryRunPublisher, MqttPublisher
from .runner import run_once
from .state import State

log = logging.getLogger("news_bot")
HEARTBEAT = "heartbeat"


def _touch_heartbeat(state_path: str) -> None:
    try:
        path = os.path.join(os.path.dirname(state_path) or ".", HEARTBEAT)
        with open(path, "w") as fh:
            fh.write(str(int(time.time())))
    except OSError:
        pass


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(prog="news_bot", description="Pubblica titoli RSS su MQTT per il BBS MeshCore")
    ap.add_argument("--once", action="store_true", help="esegue un solo ciclo ed esce")
    ap.add_argument("--dry-run", action="store_true",
                    help="stampa i messaggi senza pubblicare e senza modificare lo stato (implica --once)")
    ap.add_argument("--feeds", help="percorso di feeds.yaml (default: FEEDS_FILE)")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args(argv)

    logging.basicConfig(
        level=logging.DEBUG if args.verbose else getattr(logging, os.environ.get("LOG_LEVEL", "INFO").upper(), logging.INFO),
        format="%(asctime)s %(levelname)s %(message)s",
    )
    try:
        settings = Settings.from_env(require_key=not args.dry_run)
        feeds = load_feeds(args.feeds or settings.feeds_file)
    except ConfigError as exc:
        log.error("configurazione non valida: %s", exc)
        return 2
    if args.dry_run and not os.environ.get("NEWS_HMAC_KEY"):
        log.warning("NEWS_HMAC_KEY assente: dry-run con chiave nulla (firme non valide)")

    fetch = partial(fetch_feed, user_agent=settings.user_agent, timeout=settings.http_timeout)
    if not args.dry_run:
        os.makedirs(os.path.dirname(settings.state_path) or ".", exist_ok=True)
    state = State.open(settings.state_path, copy_in_memory=args.dry_run)
    if args.dry_run:
        publisher = DryRunPublisher()
    else:
        publisher = MqttPublisher(settings.mqtt_host, settings.mqtt_port, settings.mqtt_user,
                                  settings.mqtt_password, settings.topic, settings.publish_timeout)

    stop = threading.Event()
    for sig in (signal.SIGTERM, signal.SIGINT):
        signal.signal(sig, lambda *_: stop.set())

    log.info("news-bot avviato: %d feed, topic %s, ogni %d min", len(feeds), settings.topic, settings.poll_minutes)
    try:
        while not stop.is_set():
            try:
                run_once(settings, feeds, state, fetch, publisher, dry_run=args.dry_run)
            except Exception:
                log.exception("errore inatteso nell'esecuzione")
            if not args.dry_run:
                _touch_heartbeat(settings.state_path)
            if args.once or args.dry_run:
                break
            stop.wait(settings.poll_minutes * 60)
    finally:
        publisher.close()
        state.close()
    return 0
