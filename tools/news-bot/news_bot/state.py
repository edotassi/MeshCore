"""Stato persistente (SQLite): feed conosciuti e id gia' visti."""

from __future__ import annotations

import sqlite3

PRUNE_AFTER_SECONDS = 180 * 86400


class State:
    def __init__(self, conn: sqlite3.Connection):
        self._conn = conn
        conn.executescript(
            """
            CREATE TABLE IF NOT EXISTS feeds (name TEXT PRIMARY KEY);
            CREATE TABLE IF NOT EXISTS seen (
                id TEXT PRIMARY KEY, feed TEXT NOT NULL, ts INTEGER NOT NULL);
            CREATE TABLE IF NOT EXISTS first_seen (id TEXT PRIMARY KEY, ts INTEGER NOT NULL);
            """
        )
        conn.commit()

    @classmethod
    def open(cls, path: str, copy_in_memory: bool = False) -> "State":
        """Con copy_in_memory le scritture non toccano il file (modo --dry-run)."""
        if not copy_in_memory:
            return cls(sqlite3.connect(path))
        mem = sqlite3.connect(":memory:")
        try:
            disk = sqlite3.connect(f"file:{path}?mode=ro", uri=True)
            disk.backup(mem)
            disk.close()
        except sqlite3.Error:
            pass  # nessuno stato su disco: si parte da vuoto
        return cls(mem)

    def close(self) -> None:
        self._conn.close()

    def feed_known(self, name: str) -> bool:
        return self._conn.execute("SELECT 1 FROM feeds WHERE name=?", (name,)).fetchone() is not None

    def mark_feed_known(self, name: str) -> None:
        self._conn.execute("INSERT OR IGNORE INTO feeds(name) VALUES (?)", (name,))
        self._conn.commit()

    def is_seen(self, msg_id: str) -> bool:
        return self._conn.execute("SELECT 1 FROM seen WHERE id=?", (msg_id,)).fetchone() is not None

    def mark_seen(self, feed: str, msg_id: str, now: int) -> None:
        self.mark_seen_many(feed, [msg_id], now)

    def mark_seen_many(self, feed: str, ids, now: int) -> None:
        self._conn.executemany(
            "INSERT OR IGNORE INTO seen(id, feed, ts) VALUES (?,?,?)", [(i, feed, now) for i in ids]
        )
        self._conn.commit()

    def first_seen(self, msg_id: str, now: int) -> int:
        """Quando abbiamo visto per la prima volta un id (per voci senza data)."""
        row = self._conn.execute("SELECT ts FROM first_seen WHERE id=?", (msg_id,)).fetchone()
        if row:
            return row[0]
        self._conn.execute("INSERT INTO first_seen(id, ts) VALUES (?,?)", (msg_id, now))
        self._conn.commit()
        return now

    def prune(self, now: int) -> None:
        cutoff = now - PRUNE_AFTER_SECONDS
        self._conn.execute("DELETE FROM seen WHERE ts < ?", (cutoff,))
        self._conn.execute("DELETE FROM first_seen WHERE ts < ?", (cutoff,))
        self._conn.commit()
