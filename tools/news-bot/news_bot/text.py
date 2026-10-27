"""Pulizia e troncamento dei titoli."""

from __future__ import annotations

import html
import re

MAX_TITLE_BYTES = 140
ELLIPSIS = "…"

_TAG_RE = re.compile(r"<[^>]*>")
_WS_RE = re.compile(r"\s+")
_CTRL_RE = re.compile(r"[\x00-\x08\x0b-\x1f\x7f]")


def clean_title(raw: str) -> str:
    """Rimuove tag HTML, decodifica le entita', toglie a-capo e spazi doppi."""
    text = _TAG_RE.sub(" ", raw or "")
    text = html.unescape(text)
    text = _CTRL_RE.sub(" ", text)
    return _WS_RE.sub(" ", text).strip()


def truncate_bytes(text: str, max_bytes: int = MAX_TITLE_BYTES) -> str:
    """Tronca a max_bytes UTF-8 (compresi i 3 byte di "…") a un confine di parola."""
    if len(text.encode("utf-8")) <= max_bytes:
        return text
    budget = max_bytes - len(ELLIPSIS.encode("utf-8"))
    # errors="ignore" scarta un eventuale punto di codice troncato a meta'.
    cut = text.encode("utf-8")[:budget].decode("utf-8", errors="ignore")
    if len(cut) < len(text) and not text[len(cut)].isspace():
        space = cut.rfind(" ")
        if space > 0:
            cut = cut[:space]
        # altrimenti nessuno spazio: taglio netto
    cut = cut.rstrip(" ,;:-–—")
    return cut + ELLIPSIS
