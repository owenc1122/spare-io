"""Claude Code in print mode (`claude -p --output-format stream-json`), resumed per Spare session.

Streams text deltas with --include-partial-messages. The Claude session id reported by the
CLI is kept in the Spare session and passed back with --resume on the next turn.
"""
from __future__ import annotations

import shlex
from typing import AsyncIterator

from .base import Backend, Turn, run_jsonl


class ClaudeBackend(Backend):
    name = "claude"

    def __init__(self, command: str = "claude", workdir: str = "", model: str = "",
                 extra_args: list[str] | str | None = None) -> None:
        self.command = shlex.split(command)
        self.workdir = workdir or None
        self.model = model
        self.extra = shlex.split(extra_args) if isinstance(extra_args, str) else list(extra_args or [])

    def build_cmd(self, turn: Turn) -> list[str]:
        cmd = [*self.command, "-p", "--output-format", "stream-json", "--verbose", "--include-partial-messages",
               "--append-system-prompt", turn.system_prompt]
        if self.model:
            cmd += ["--model", self.model]
        sid = turn.session.state.get("claude_session")
        if sid:
            cmd += ["--resume", sid]
        return [*cmd, *self.extra]

    async def stream(self, turn: Turn) -> AsyncIterator[str | dict]:
        streamed = False
        final = ""
        async for ev in run_jsonl(self.build_cmd(turn), stdin_data=turn.text, cwd=self.workdir):
            if ev.get("session_id"):
                turn.session.state["claude_session"] = ev["session_id"]
            t = ev.get("type")
            if t == "stream_event":
                d = (ev.get("event") or {}).get("delta") or {}
                if d.get("type") == "text_delta" and d.get("text"):
                    streamed = True
                    yield d["text"]
            elif t == "assistant" and not streamed:
                for blk in (ev.get("message") or {}).get("content") or []:
                    if blk.get("type") == "text" and blk.get("text"):
                        yield blk["text"]
                        final = "x"
            elif t == "result":
                if ev.get("is_error"):
                    raise RuntimeError(str(ev.get("result") or "claude error")[:200])
                if not streamed and not final and ev.get("result"):
                    yield ev["result"]
