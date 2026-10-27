"""Formato del messaggio v1 (condiviso con il firmware del BBS)."""

from __future__ import annotations

import base64
import binascii
import hashlib
import hmac
import re

VERSION = "v1"
_ID_RE = re.compile(r"^[0-9a-zA-Z_-]{1,32}$")
_HEX64_RE = re.compile(r"^[0-9a-f]{64}$")
MAX_SOURCE_BYTES = 16
MAX_TITLE_BYTES = 140
KEY_BYTES = 32


class ProtocolError(ValueError):
    pass


def decode_key(b64: str) -> bytes:
    try:
        key = base64.b64decode(b64.strip(), validate=True)
    except (binascii.Error, ValueError) as exc:
        raise ProtocolError("NEWS_HMAC_KEY non e' base64 valido") from exc
    if len(key) != KEY_BYTES:
        raise ProtocolError(f"NEWS_HMAC_KEY deve essere di {KEY_BYTES} byte (trovati {len(key)})")
    return key


def make_id(feed_name: str, guid: str) -> str:
    return hashlib.sha256(f"{feed_name}\n{guid}".encode("utf-8")).hexdigest()[:16]


def validate_fields(msg_id: str, ts: int, source: str, title: str) -> None:
    if not _ID_RE.match(msg_id):
        raise ProtocolError("id non valido")
    if not isinstance(ts, int) or isinstance(ts, bool) or ts < 0:
        raise ProtocolError("ts non valido")
    for name, value, limit in (("source", source, MAX_SOURCE_BYTES), ("title", title, MAX_TITLE_BYTES)):
        n = len(value.encode("utf-8"))
        if n < 1 or n > limit:
            raise ProtocolError(f"{name}: lunghezza {n} byte fuori dall'intervallo 1..{limit}")
        if "\n" in value or "\r" in value:
            raise ProtocolError(f"{name} contiene un a-capo")


def sign(key: bytes, msg_id: str, ts: int, source: str, title: str) -> str:
    data = f"{VERSION}\n{msg_id}\n{ts}\n{source}\n{title}".encode("utf-8")
    return hmac.new(key, data, hashlib.sha256).hexdigest()


def build_message(key: bytes, msg_id: str, ts: int, source: str, title: str) -> str:
    validate_fields(msg_id, ts, source, title)
    mac = sign(key, msg_id, ts, source, title)
    return "\n".join((VERSION, msg_id, str(ts), source, title, mac))


def parse_message(payload: str, key: bytes) -> dict:
    """Verifica un messaggio (usata dai test; stessa logica del firmware)."""
    parts = payload.split("\n")
    if len(parts) != 6 or parts[0] != VERSION:
        raise ProtocolError("formato non valido")
    _, msg_id, ts_s, source, title, mac = parts
    if not ts_s.isdigit():
        raise ProtocolError("ts non valido")
    ts = int(ts_s)
    validate_fields(msg_id, ts, source, title)
    if not _HEX64_RE.match(mac) or not hmac.compare_digest(mac, sign(key, msg_id, ts, source, title)):
        raise ProtocolError("firma non valida")
    return {"id": msg_id, "ts": ts, "source": source, "title": title}
