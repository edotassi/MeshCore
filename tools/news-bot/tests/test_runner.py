import pathlib
import re
from datetime import datetime
from zoneinfo import ZoneInfo

import pytest

from news_bot.config import Feed, Settings, parse_quiet_hours
from news_bot.feeds import FeedError, parse_feed
from news_bot.protocol import parse_message
from news_bot.runner import run_once
from news_bot.state import State

FIX = pathlib.Path(__file__).parent / "fixtures"
KEY = bytes(range(32))
ROME = ZoneInfo("Europe/Rome")
# 2026-10-27 09:45 UTC = 10:45 a Roma (CET), subito dopo l'ultima voce di rss_sample.xml
NOW = int(datetime(2026, 10, 27, 9, 45, tzinfo=ZoneInfo("UTC")).timestamp())


class FakePublisher:
    def __init__(self, ack=True):
        self.ack, self.sent = ack, []

    def publish(self, payload):
        self.sent.append(payload)
        return self.ack

    def titles(self):
        return [parse_message(p, KEY)["title"] for p in self.sent]


def settings(**kw):
    return Settings(hmac_key=KEY, **kw)


def rss():
    return parse_feed((FIX / "rss_sample.xml").read_bytes())


@pytest.fixture
def state():
    return State.open(":memory:")


def run(feeds, state, pub, entries_by_feed, st=None, now=NOW):
    return run_once(st or settings(), feeds, state, lambda f: entries_by_feed[f.name], pub, now=now, out=lambda *_: None)


def test_parse_feed_fixture():
    e = rss()
    assert len(e) == 5 and e[0].guid == "g5" and e[0].published is not None


def test_parse_garbage_raises():
    with pytest.raises(FeedError):
        parse_feed(b"not xml at all")


def test_first_run_seeds_without_publishing(state):
    feed = Feed("F", "u", "Lab")
    pub = FakePublisher()
    r = run([feed], state, pub, {"F": rss()})
    assert pub.sent == [] and r.published == 0
    assert state.feed_known("F")
    # seconda esecuzione identica: niente di nuovo
    run([feed], state, pub, {"F": rss()})
    assert pub.sent == []


def test_new_items_published_oldest_first_with_clean_titles(state):
    feed = Feed("F", "u", "Lab", max_per_run=5)
    pub = FakePublisher()
    run([feed], state, pub, {"F": rss()[2:]})  # seed: g3,g2,g1
    run([feed], state, pub, {"F": rss()})
    assert pub.titles() == ["Quarta è grassetto su due righe", "Quinta notizia"]
    msg = parse_message(pub.sent[0], KEY)
    assert msg["source"] == "Lab" and msg["ts"] == NOW


def test_per_feed_cap_keeps_newest_and_marks_rest_seen(state):
    feed = Feed("F", "u", "Lab", max_per_run=2)
    pub = FakePublisher()
    run([feed], state, pub, {"F": rss()[4:]})  # seed solo g1
    r = run([feed], state, pub, {"F": rss()})
    assert pub.titles() == ["Quarta è grassetto su due righe", "Quinta notizia"]
    assert r.skipped == 2
    pub2 = FakePublisher()
    run([feed], state, pub2, {"F": rss()})
    assert pub2.sent == []  # g2,g3 scartate, non riproposte


def test_global_cap(state):
    f1, f2 = Feed("A", "u", "A", max_per_run=5), Feed("B", "u", "B", max_per_run=5)
    pub = FakePublisher()
    data = {"A": rss(), "B": rss()}
    run([f1, f2], state, pub, {"A": [], "B": []})  # seed vuoto
    r = run([f1, f2], state, pub, data, st=settings(max_per_run=3))
    assert len(pub.sent) == 3 and r.skipped == 7


def test_include_exclude(state):
    entries = parse_feed((FIX / "atom_releases.xml").read_bytes())
    feed = Feed("MC", "u", "MeshCore", max_per_run=5, include=re.compile("^Companion", re.I))
    pub = FakePublisher()
    run([feed], state, pub, {"MC": []})
    run([feed], state, pub, {"MC": entries})
    assert pub.titles() == ["Companion Firmware v1.18.0"]

    st2 = State.open(":memory:")
    feed2 = Feed("MC", "u", "MeshCore", max_per_run=5, exclude=re.compile("repeater|room", re.I))
    pub2 = FakePublisher()
    run([feed2], st2, pub2, {"MC": []})
    run([feed2], st2, pub2, {"MC": entries})
    assert pub2.titles() == ["Companion Firmware v1.18.0"]


def test_seen_only_after_ack(state):
    feed = Feed("F", "u", "Lab", max_per_run=5)
    run([feed], state, FakePublisher(), {"F": rss()[4:]})
    bad = FakePublisher(ack=False)
    r = run([feed], state, bad, {"F": rss()[:2] + rss()[4:]})
    assert r.failed and len(bad.sent) == 1  # si ferma al primo fallimento
    good = FakePublisher()
    run([feed], state, good, {"F": rss()[:2] + rss()[4:]})
    assert good.titles() == ["Quarta è grassetto su due righe", "Quinta notizia"]


def test_feed_error_skipped(state):
    bad, ok = Feed("Bad", "u", "Bad"), Feed("Ok", "u", "Ok")

    def fetch(f):
        if f.name == "Bad":
            raise FeedError("boom")
        return rss()

    pub = FakePublisher()
    run_once(settings(), [bad, ok], state, fetch, pub, now=NOW, out=lambda *_: None)
    assert state.feed_known("Ok") and not state.feed_known("Bad")


def _quiet_settings():
    return settings(quiet_hours=parse_quiet_hours("23-7", ROME))


def test_quiet_hours_hold_then_publish_then_expire(state):
    feed = Feed("F", "u", "Lab", max_per_run=5)
    pub = FakePublisher()
    run([feed], state, pub, {"F": rss()[4:]})
    # simula notte: 23:30 locale = 22:30 UTC; voci con data 22:00-22:20 UTC
    night = int(datetime(2026, 10, 27, 22, 30, tzinfo=ZoneInfo("UTC")).timestamp())  # 23:30 locale
    fresh = parse_feed(b"""<rss version="2.0"><channel><title>t</title>
      <item><title>Notte</title><guid>n1</guid><pubDate>Tue, 27 Oct 2026 22:20:00 GMT</pubDate></item>
      <item><title>Vecchia</title><guid>n0</guid><pubDate>Tue, 27 Oct 2026 14:00:00 GMT</pubDate></item>
      </channel></rss>""")
    r = run([feed], state, pub, {"F": rss()[4:] + fresh}, st=_quiet_settings(), now=night)
    assert pub.sent == [] and r.held == 1 and r.skipped == 1  # "Vecchia" (>6h) scartata
    # al mattino (07:30 locale = 06:30 UTC) "Notte" ha 8h: scaduta
    morning = int(datetime(2026, 10, 28, 6, 30, tzinfo=ZoneInfo("UTC")).timestamp())
    run([feed], state, pub, {"F": rss()[4:] + fresh}, st=_quiet_settings(), now=morning)
    assert pub.sent == []
    # voce trattenuta ma ancora fresca dopo la fine delle ore silenziose
    st2 = State.open(":memory:")
    pub2 = FakePublisher()
    run([feed], st2, pub2, {"F": []})
    late = int(datetime(2026, 10, 27, 5, 50, tzinfo=ZoneInfo("UTC")).timestamp())  # 06:50 locale: quieto
    item = parse_feed(b"""<rss version="2.0"><channel><title>t</title>
      <item><title>Presto</title><guid>p1</guid><pubDate>Tue, 27 Oct 2026 05:00:00 GMT</pubDate></item>
      </channel></rss>""")
    run([feed], st2, pub2, {"F": item}, st=_quiet_settings(), now=late)
    assert pub2.sent == []
    after = int(datetime(2026, 10, 27, 6, 5, tzinfo=ZoneInfo("UTC")).timestamp())  # 07:05 locale
    run([feed], st2, pub2, {"F": item}, st=_quiet_settings(), now=after)
    assert pub2.titles() == ["Presto"]
