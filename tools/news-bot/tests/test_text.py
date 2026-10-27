from news_bot.text import clean_title, truncate_bytes


def test_clean_html_and_whitespace():
    assert clean_title("  Ciao <b>mondo</b>\n  &egrave; &amp; basta\t!\xa0") == "Ciao mondo è & basta !"


def test_clean_removes_newlines():
    assert "\n" not in clean_title("a\r\nb\nc")


def test_short_unchanged():
    assert truncate_bytes("breve") == "breve"


def test_exact_budget_unchanged():
    s = "a" * 140
    assert truncate_bytes(s) == s


def test_word_boundary_and_budget():
    s = "parola " * 40
    out = truncate_bytes(s.strip())
    assert out.endswith("…")
    assert len(out.encode()) <= 140
    assert out[:-1].endswith("parola")  # non spezza una parola


def test_hard_cut_without_spaces():
    out = truncate_bytes("x" * 300)
    assert out == "x" * 137 + "…"
    assert len(out.encode()) == 140


def test_never_splits_codepoint():
    out = truncate_bytes("è" * 200)  # 2 byte ciascuna
    out.encode("utf-8")
    assert len(out.encode()) <= 140 and out.endswith("…")
    out = truncate_bytes("\U0001F600" * 100)  # 4 byte
    assert len(out.encode()) <= 140
    assert out[:-1] == "\U0001F600" * 34


def test_cut_exactly_at_space():
    s = "a" * 136 + " bbbb cccc"
    out = truncate_bytes(s)
    assert out == "a" * 136 + "…"
