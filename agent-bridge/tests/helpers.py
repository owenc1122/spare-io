import asyncio
import json
from contextlib import asynccontextmanager

import aiohttp
from aiohttp.test_utils import TestServer

from spare_bridge.config import Config
from spare_bridge.server import create_app

TOKEN = "test-token"


def run(coro):
    return asyncio.run(coro)


@asynccontextmanager
async def serve(token=TOKEN, **cfg_kw):
    cfg = Config(**cfg_kw)
    app = create_app(cfg, token)
    server = TestServer(app)
    await server.start_server()
    async with aiohttp.ClientSession() as http:
        try:
            yield server, http, app
        finally:
            await server.close()


def hdrs(session="sg-1", locale="zh-CN", token=TOKEN):
    h = {"X-Spare-Protocol": "1", "X-Spare-Session": session, "X-Spare-Locale": locale}
    if token:
        h["Authorization"] = f"Bearer {token}"
    return h


def body(text, **extra):
    return {"model": "spare-agent", "stream": True, "messages": [{"role": "user", "content": text}], **extra}


def parse_sse(raw: str):
    """-> list of (event, data) ; comments appear as ("comment", text)."""
    out = []
    for block in raw.split("\n\n"):
        block = block.strip("\n")
        if not block:
            continue
        ev, data = "message", None
        for line in block.split("\n"):
            if line.startswith(":"):
                out.append(("comment", line[1:].strip()))
            elif line.startswith("event:"):
                ev = line[6:].strip()
            elif line.startswith("data:"):
                data = line[5:].strip()
        if data is not None:
            out.append((ev, data if data == "[DONE]" else json.loads(data)))
    return out


async def ask(server, http, text, **kw):
    r = await http.post(server.make_url("/v1/chat/completions"), json=body(text), headers=hdrs(**kw))
    raw = await r.text()
    return r, parse_sse(raw), raw


def text_of(events):
    return "".join(d["choices"][0]["delta"].get("content", "") for e, d in events
                   if e == "message" and d != "[DONE]" and "choices" in d)
