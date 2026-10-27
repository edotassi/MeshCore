import datetime as dt
from zoneinfo import ZoneInfo

import pytest

from news_bot.config import ConfigError, Settings, load_feeds, parse_quiet_hours

ROME = ZoneInfo("Europe/Rome")


def epoch(h, m=0):
    return dt.datetime(2026, 10, 27, h, m, tzinfo=ROME).timestamp()


def test_quiet_wraps_midnight():
    q = parse_quiet_hours("23-7", ROME)
    assert q.is_quiet(epoch(23)) and q.is_quiet(epoch(2)) and q.is_quiet(epoch(6, 59))
    assert not q.is_quiet(epoch(7)) and not q.is_quiet(epoch(12)) and not q.is_quiet(epoch(22, 59))


def test_quiet_same_day_and_empty():
    q = parse_quiet_hours("1-5", ROME)
    assert q.is_quiet(epoch(3)) and not q.is_quiet(epoch(6))
    assert parse_quiet_hours("", ROME) is None


@pytest.mark.parametrize("bad", ["abc", "25-3", "5-5"])
def test_quiet_invalid(bad):
    with pytest.raises(ConfigError):
        parse_quiet_hours(bad, ROME)


def test_default_feeds_file_loads():
    import pathlib
    feeds = load_feeds(str(pathlib.Path(__file__).parent.parent / "feeds.yaml"))
    assert [f.label for f in feeds] == ["ANSA", "LeccePrima", "MeshCore", "DDay"]
    mc = feeds[2]
    assert mc.accepts("Companion Firmware v1.18.0") and not mc.accepts("Repeater Firmware v1.18.0")


def test_feed_validation(tmp_path):
    p = tmp_path / "f.yaml"
    p.write_text("feeds:\n  - {name: A, url: http://x, label: 'x*17chars_long_xx'}\n")
    with pytest.raises(ConfigError):
        load_feeds(str(p))
    p.write_text("feeds:\n  - {name: A, url: http://x, include: '('}\n")
    with pytest.raises(ConfigError):
        load_feeds(str(p))


def test_settings_from_env():
    s = Settings.from_env({"NEWS_HMAC_KEY": "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=", "QUIET_HOURS": "23-7"})
    assert s.poll_minutes == 15 and s.max_per_run == 5 and s.topic == "bbs/news" and s.quiet_hours
    with pytest.raises(ConfigError):
        Settings.from_env({})
