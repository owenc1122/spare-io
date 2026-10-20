"""aiohttp server implementing the Spare Agent Protocol v1."""
from __future__ import annotations

import asyncio
import hmac
import json
import logging
import time
import uuid
from datetime import datetime, timezone

from aiohttp import web

from . import PROTOCOL_VERSION, __version__
from .actions import ActionFilter
from .backends import Backend, Turn, create_backend
from .chunker import SentenceChunker
from .config import Config
from .prompt import normalize_locale, system_prompt
from .sessions import SessionStore
from .speech import asr_handler, make_transcriber, tts_handler

log = logging.getLogger("spare_bridge")

try:
    from aiohttp.web import AppKey
    K_STATE = AppKey("state", dict)
except ImportError:  # pragma: no cover
    K_STATE = "state"


def _json_error(status: int, message: str, typ: str = "error") -> web.Response:
    return web.json_response({"error": {"message": message, "type": typ}}, status=status)


def _authorized(request: web.Request, token: str | None) -> bool:
    if token is None:
        return True
    got = request.headers.get("Authorization", "")
    if not got.lower().startswith("bearer "):
        return False
    return hmac.compare_digest(got[7:].strip().encode(), token.encode())


def _chunk(cid: str, delta: dict, finish: str | None = None) -> bytes:
    obj = {"id": cid, "object": "chat.completion.chunk", "created": int(time.time()), "model": "spare-agent",
           "choices": [{"index": 0, "delta": delta, "finish_reason": finish}]}
    return b"data: " + json.dumps(obj, ensure_ascii=False).encode() + b"\n\n"


def _action(act: dict) -> bytes:
    return b"event: spare.action\ndata: " + json.dumps(act, ensure_ascii=False).encode() + b"\n\n"


async def handshake(request: web.Request) -> web.Response:
    st = request.app[K_STATE]
    if not _authorized(request, st["token"]):
        return _json_error(401, "invalid token", "unauthorized")
    cfg: Config = st["cfg"]
    caps = ["streaming", "session-memory"] + (["actions"] if cfg.actions else [])
    return web.json_response({"protocol": PROTOCOL_VERSION, "name": cfg.name, "model": st["backend"].name,
                              "capabilities": caps, "bridge": __version__})


async def health(request: web.Request) -> web.Response:
    return web.json_response({"ok": True})


def _disconnected(request: web.Request) -> bool:
    t = request.transport
    return t is None or t.is_closing()


async def chat(request: web.Request) -> web.StreamResponse:
    st = request.app[K_STATE]
    cfg: Config = st["cfg"]
    backend: Backend = st["backend"]
    if not _authorized(request, st["token"]):
        return _json_error(401, "invalid token", "unauthorized")
    try:
        body = await request.json()
        msgs = [m for m in body["messages"] if m.get("role") in ("user", "assistant")]
        text = str(msgs[-1]["content"]).strip()
        if msgs[-1]["role"] != "user" or not text:
            raise ValueError
    except (ValueError, KeyError, IndexError, TypeError):
        return _json_error(400, "expected {messages:[...,{role:user,content}]}", "invalid_request")

    spare = body.get("spare") if isinstance(body.get("spare"), dict) else {}
    sid = request.headers.get("X-Spare-Session") or spare.get("session") or "anon-" + uuid.uuid4().hex[:8]
    locale = normalize_locale(request.headers.get("X-Spare-Locale") or spare.get("locale"))
    now = spare.get("time") or datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")

    session = st["sessions"].get(sid)
    if not session.history and len(msgs) > 1:  # client sent history (stateless mode): seed from it
        for m in msgs[:-1]:
            session.add(m["role"], str(m["content"]))

    resp = web.StreamResponse(status=200, headers={
        "Content-Type": "text/event-stream; charset=utf-8", "Cache-Control": "no-cache, no-transform",
        "X-Accel-Buffering": "no", "Connection": "keep-alive"})
    await resp.prepare(request)
    await resp.write(b": ok\n\n")  # flush headers and the first bytes immediately

    cid = "chatcmpl-" + uuid.uuid4().hex[:16]
    q: asyncio.Queue = asyncio.Queue()

    async def produce() -> None:
        try:
            async with session.lock:
                turn = Turn(session, text, locale, system_prompt(locale, now, cfg.actions, cfg.extra_prompt), now, spare)
                gen = backend.stream(turn)
                try:
                    async for item in gen:
                        await q.put(("item", item))
                finally:
                    await gen.aclose()
            await q.put(("done", None))
        except asyncio.CancelledError:
            raise
        except Exception as e:  # noqa: BLE001
            log.warning("backend error: %s", e)
            await q.put(("error", str(e)))

    task = asyncio.create_task(produce())
    flt = ActionFilter(cfg.actions)
    chunker = SentenceChunker(cfg.chunking)
    reply: list[str] = []
    last_write = time.monotonic()
    poll = max(0.01, min(0.2, cfg.ping_interval))

    async def emit(items: list) -> None:
        nonlocal last_write
        for it in items:
            if isinstance(it, dict):
                await resp.write(_action(it))
            else:
                for piece in chunker.feed(it):
                    reply.append(piece)
                    await resp.write(_chunk(cid, {"content": piece}))
            last_write = time.monotonic()

    async def emit_text_flush() -> None:
        nonlocal last_write
        for piece in chunker.flush():
            reply.append(piece)
            await resp.write(_chunk(cid, {"content": piece}))
            last_write = time.monotonic()

    completed = False
    try:
        while True:
            if _disconnected(request):
                return resp
            try:
                kind, val = await asyncio.wait_for(q.get(), timeout=poll)
            except asyncio.TimeoutError:
                if time.monotonic() - last_write >= cfg.ping_interval:
                    await resp.write(b": ping\n\n")
                    last_write = time.monotonic()
                continue
            if kind == "item":
                out = []
                for it in ([val] if isinstance(val, dict) else flt.feed(val)):
                    out.append(it)
                await emit(out)
            elif kind == "error":
                err = {"error": {"message": val[:200], "type": "backend_error"}}
                await resp.write(b"event: error\ndata: " + json.dumps(err, ensure_ascii=False).encode() + b"\n\n")
                break
            else:
                await emit(flt.flush())
                await emit_text_flush()
                await resp.write(_chunk(cid, {}, "stop"))
                await resp.write(b"data: [DONE]\n\n")
                completed = True
                break
        if completed:
            session.turns += 1
            session.add("user", text)
            session.add("assistant", "".join(reply))
        await resp.write_eof()
    except (ConnectionResetError, asyncio.CancelledError, RuntimeError):
        pass
    finally:
        if not task.done():
            task.cancel()
        try:
            await task
        except BaseException:  # noqa: BLE001
            pass
        session.last_used = time.monotonic()
    return resp


async def _janitor(app: web.Application):
    async def loop():
        st = app[K_STATE]
        while True:
            await asyncio.sleep(60)
            n = st["sessions"].cleanup()
            if n:
                log.info("cleaned %d idle sessions", n)
    task = asyncio.create_task(loop())
    yield
    task.cancel()
    http = app[K_STATE].get("http")
    if http is not None:
        await http.close()
    await app[K_STATE]["backend"].close()


def create_app(cfg: Config, token: str | None, backend: Backend | None = None, transcriber=None) -> web.Application:
    """token=None disables auth (development only)."""
    app = web.Application(client_max_size=2 * 1024 * 1024)
    app[K_STATE] = {"cfg": cfg, "token": token, "backend": backend or create_backend(cfg),
                    "sessions": SessionStore(cfg.session_ttl)}
    if cfg.asr.get("enabled"):  # optional extension, protocol section 11
        app[K_STATE]["asr"] = transcriber or make_transcriber(cfg.asr)
        app.router.add_post("/v1/asr", asr_handler)
    if cfg.tts.get("enabled"):  # optional extension, protocol section 12
        app.router.add_post("/v1/audio/speech", tts_handler)
    app.router.add_get("/v1/spare/agent", handshake)
    app.router.add_post("/v1/chat/completions", chat)
    app.router.add_get("/healthz", health)
    app.cleanup_ctx.append(_janitor)
    return app
