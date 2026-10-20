from __future__ import annotations

import asyncio
import json
import os
from dataclasses import dataclass
from typing import AsyncIterator

from ..sessions import Session


@dataclass
class Turn:
    session: Session
    text: str  # the new user utterance
    locale: str
    system_prompt: str
    now: str = ""
    spare: dict | None = None


class Backend:
    """A backend yields reply text pieces (str) and optionally action dicts."""

    name = "base"

    async def stream(self, turn: Turn) -> AsyncIterator[str | dict]:
        raise NotImplementedError
        yield ""  # pragma: no cover

    async def close(self) -> None:
        pass


async def run_jsonl(cmd: list[str], stdin_data: str | None = None, cwd: str | None = None,
                    env: dict | None = None) -> AsyncIterator[dict]:
    """Spawn a CLI and yield one parsed JSON object per stdout line.

    The process is killed when the consumer stops iterating (client disconnected).
    """
    full_env = {**os.environ, **(env or {})}
    proc = await asyncio.create_subprocess_exec(
        *cmd,
        stdin=asyncio.subprocess.PIPE if stdin_data is not None else asyncio.subprocess.DEVNULL,
        stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.PIPE,
        cwd=cwd or None, env=full_env, limit=4 * 1024 * 1024)
    try:
        if stdin_data is not None:
            proc.stdin.write(stdin_data.encode("utf-8"))
            await proc.stdin.drain()
            proc.stdin.close()
        async for raw in proc.stdout:
            line = raw.decode("utf-8", "replace").strip()
            if not line.startswith("{"):
                continue
            try:
                yield json.loads(line)
            except ValueError:
                continue
        rc = await proc.wait()
        if rc != 0:
            err = (await proc.stderr.read()).decode("utf-8", "replace").strip()
            raise RuntimeError(f"{cmd[0]} exited with {rc}: {err[-200:]}")
    finally:
        if proc.returncode is None:
            try:
                proc.kill()
            except ProcessLookupError:
                pass
            await proc.wait()
