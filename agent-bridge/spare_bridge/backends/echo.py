"""Test backend: echoes the utterance, remembers the session, can emit actions."""
from __future__ import annotations

import asyncio
from typing import AsyncIterator

from .base import Backend, Turn


class EchoBackend(Backend):
    name = "echo"

    def __init__(self) -> None:
        self.cancelled = 0  # how many turns were cancelled mid-way
        self.started = 0

    async def stream(self, turn: Turn) -> AsyncIterator[str | dict]:
        text = turn.text
        self.started += 1
        users = [m["content"] for m in turn.session.history if m["role"] == "user"]
        en = turn.locale.startswith("en")
        try:
            if "slow" in text:
                yield "Hold on. " if en else "稍等一下。"
                await asyncio.sleep(30)
                yield "Done."
                return
            if "before" in text or "上一句" in text:
                prev = users[-1] if users else ("nothing" if en else "没有")
                yield f"Before that you said: {prev}." if en else f"你上一句说的是：{prev}。"
                return
            yield f"You said: {text}. " if en else f"你说：{text}。"
            yield f"This is turn {len(users) + 1}." if en else f"这是第{len(users) + 1}轮。"
            if "card" in text:
                yield {"type": "show_card", "title": "Echo", "rows": [text[:14]], "icon": "info"}
            if "marker" in text:
                yield "[[card:Marker|row a|icon=check]]"
            if "bye" in text or "再见" in text:
                yield {"type": "end_conversation"}
            if "listen" in text:
                yield {"type": "keep_listening", "seconds": 45}
        except asyncio.CancelledError:
            self.cancelled += 1
            raise
