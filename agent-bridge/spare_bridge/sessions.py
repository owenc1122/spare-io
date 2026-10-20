"""In-memory session store keyed by X-Spare-Session, with idle cleanup."""
from __future__ import annotations

import asyncio
import time
from dataclasses import dataclass, field


@dataclass
class Session:
    id: str
    history: list[dict] = field(default_factory=list)  # {"role", "content"}
    state: dict = field(default_factory=dict)  # backend-private (e.g. codex thread id)
    turns: int = 0
    last_used: float = field(default_factory=time.monotonic)
    lock: asyncio.Lock = field(default_factory=asyncio.Lock)

    def add(self, role: str, content: str, max_messages: int = 40) -> None:
        self.history.append({"role": role, "content": content})
        del self.history[:-max_messages]


class SessionStore:
    def __init__(self, ttl: float = 900.0, max_sessions: int = 500) -> None:
        self.ttl = ttl
        self.max_sessions = max_sessions
        self._s: dict[str, Session] = {}

    def __len__(self) -> int:
        return len(self._s)

    def get(self, sid: str) -> Session:
        s = self._s.get(sid)
        if s is None:
            if len(self._s) >= self.max_sessions:
                oldest = min(self._s.values(), key=lambda x: x.last_used)
                self._s.pop(oldest.id, None)
            s = self._s[sid] = Session(sid)
        s.last_used = time.monotonic()
        return s

    def cleanup(self) -> int:
        now = time.monotonic()
        dead = [k for k, s in self._s.items() if now - s.last_used > self.ttl and not s.lock.locked()]
        for k in dead:
            del self._s[k]
        return len(dead)
