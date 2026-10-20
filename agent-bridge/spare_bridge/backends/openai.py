"""Any OpenAI-compatible chat endpoint (OpenAI, Ollama, LM Studio, vLLM, DeepSeek, OpenClaw gateway...)."""
from __future__ import annotations

import json
import re
from typing import AsyncIterator

import aiohttp

from .base import Backend, Turn


class OpenAIBackend(Backend):
    name = "openai"

    def __init__(self, base_url: str, model: str, api_key: str = "", max_tokens: int = 200,
                 temperature: float | None = None, history_messages: int = 16, extra_body: dict | None = None) -> None:
        base = base_url.rstrip("/")
        if base.endswith("/chat/completions"):
            self.url = base
        elif re.search(r"/v\d+$", base):
            self.url = base + "/chat/completions"
        else:
            self.url = base + "/v1/chat/completions"
        self.model, self.api_key = model, api_key
        self.max_tokens, self.temperature = max_tokens, temperature
        self.history_messages = history_messages
        self.extra_body = extra_body or {}
        self._http: aiohttp.ClientSession | None = None

    def _client(self) -> aiohttp.ClientSession:
        if self._http is None or self._http.closed:
            self._http = aiohttp.ClientSession(timeout=aiohttp.ClientTimeout(total=None, sock_connect=10, sock_read=60))
        return self._http

    async def close(self) -> None:
        if self._http:
            await self._http.close()

    async def stream(self, turn: Turn) -> AsyncIterator[str | dict]:
        msgs = [{"role": "system", "content": turn.system_prompt}]
        msgs += turn.session.history[-self.history_messages:]
        msgs.append({"role": "user", "content": turn.text})
        body = {"model": self.model, "messages": msgs, "stream": True, "max_tokens": self.max_tokens, **self.extra_body}
        if self.temperature is not None:
            body["temperature"] = self.temperature
        headers = {"Content-Type": "application/json", "Accept": "text/event-stream"}
        if self.api_key:
            headers["Authorization"] = f"Bearer {self.api_key}"
        async with self._client().post(self.url, json=body, headers=headers) as r:
            if r.status >= 400:
                detail = (await r.text())[:200]
                raise RuntimeError(f"upstream {r.status}: {detail}")
            async for raw in r.content:
                line = raw.decode("utf-8", "replace").strip()
                if not line.startswith("data:"):
                    continue
                data = line[5:].strip()
                if data == "[DONE]":
                    return
                try:
                    obj = json.loads(data)
                    delta = obj["choices"][0].get("delta") or {}
                except (ValueError, KeyError, IndexError):
                    continue
                piece = delta.get("content")  # reasoning_content is deliberately not spoken
                if piece:
                    yield piece
