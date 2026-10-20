"""OpenAI Codex CLI (`codex exec --json`), one resumable thread per Spare session.

`codex exec --json` prints JSON Lines: `thread.started` (thread_id), `item.completed`
(agent_message text) and `turn.completed`. Messages arrive whole, not token by token,
so the first sentence appears when the model finishes its first message; the bridge
then splits it into sentences. Use a fast model / low reasoning effort for latency.
"""
from __future__ import annotations

import shlex
from typing import AsyncIterator

from .base import Backend, Turn, run_jsonl


class CodexBackend(Backend):
    name = "codex"

    def __init__(self, command: str = "codex", workdir: str = "", extra_args: list[str] | str | None = None) -> None:
        self.command = shlex.split(command)
        self.workdir = workdir or None
        self.extra = shlex.split(extra_args) if isinstance(extra_args, str) else list(extra_args or [])

    def build_cmd(self, turn: Turn, prompt: str) -> list[str]:
        tid = turn.session.state.get("codex_thread")
        base = [*self.command, "exec"]
        if tid:
            base += ["resume", tid]
        return [*base, "--json", "--skip-git-repo-check", *self.extra, prompt]

    async def stream(self, turn: Turn) -> AsyncIterator[str | dict]:
        first = "codex_thread" not in turn.session.state
        prompt = (turn.system_prompt + "\n\n用户说：" if first else "(按眼镜助手规则简短作答，纯文本)\n用户说：") + turn.text
        sent = False
        async for ev in run_jsonl(self.build_cmd(turn, prompt), cwd=self.workdir):
            t = ev.get("type")
            if t == "thread.started" and ev.get("thread_id"):
                turn.session.state["codex_thread"] = ev["thread_id"]
            elif t == "item.completed":
                item = ev.get("item") or {}
                if item.get("type") == "agent_message" and item.get("text"):
                    yield ("\n" if sent else "") + item["text"]
                    sent = True
            elif t in ("error", "turn.failed"):
                msg = ev.get("message") or (ev.get("error") or {}).get("message") or "codex failed"
                raise RuntimeError(str(msg)[:200])
