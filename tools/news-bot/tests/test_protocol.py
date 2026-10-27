import base64

import pytest

from news_bot.protocol import (ProtocolError, build_message, decode_key, make_id,
                               parse_message, sign)

KEY_B64 = "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8="
KEY = bytes(range(32))
TITLE = "Terremoto di magnitudo 3.2 al largo di Otranto, nessun danno"


def test_key_vector_matches_base64():
    assert decode_key(KEY_B64) == KEY


def test_signing_vector():
    mac = sign(KEY, "3f2a9c1d7e8b4a60", 1793091600, "ANSA", TITLE)
    assert mac == "0bb6e86b3ce7631bdd0382262ea63d32da0fcbf9e364b771d8e893976bfd91cd"


def test_message_format():
    msg = build_message(KEY, "3f2a9c1d7e8b4a60", 1793091600, "ANSA", TITLE)
    lines = msg.split("\n")
    assert lines == ["v1", "3f2a9c1d7e8b4a60", "1793091600", "ANSA", TITLE,
                     "0bb6e86b3ce7631bdd0382262ea63d32da0fcbf9e364b771d8e893976bfd91cd"]
    assert not msg.endswith("\n")
    assert parse_message(msg, KEY)["title"] == TITLE


def test_forged_message_rejected():
    msg = build_message(KEY, "abc", 1, "ANSA", "ciao")
    with pytest.raises(ProtocolError):
        parse_message(msg.replace("ciao", "ciaO"), KEY)
    with pytest.raises(ProtocolError):
        parse_message(msg, bytes(32))


def test_make_id():
    i = make_id("ANSA", "http://x/1")
    assert len(i) == 16 and all(c in "0123456789abcdef" for c in i)
    assert i == make_id("ANSA", "http://x/1")
    assert i != make_id("Altro", "http://x/1")


@pytest.mark.parametrize("args", [
    ("", 1, "A", "t"), ("bad id", 1, "A", "t"), ("x" * 33, 1, "A", "t"),
    ("ok", -1, "A", "t"), ("ok", 1, "", "t"), ("ok", 1, "A" * 17, "t"),
    ("ok", 1, "A", ""), ("ok", 1, "A", "a\nb"), ("ok", 1, "A", "x" * 141),
    ("ok", 1, "A", "è" * 71),  # 142 byte
])
def test_validation_rejects(args):
    with pytest.raises(ProtocolError):
        build_message(KEY, *args)


def test_validation_accepts_limits():
    build_message(KEY, "x" * 32, 0, "A" * 16, "x" * 140)
    build_message(KEY, "ok", 5, "è" * 8, "è" * 70)


def test_bad_keys():
    with pytest.raises(ProtocolError):
        decode_key(base64.b64encode(b"short").decode())
    with pytest.raises(ProtocolError):
        decode_key("!!!")
