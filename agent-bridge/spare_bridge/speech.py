"""Optional reference endpoints for the protocol's custom ASR (section 11) and TTS (section 12) extensions.

Both are OFF by default and are thin proxies:
  POST /v1/asr               WAV (16 kHz mono 16-bit) -> {"text": "..."}
  POST /v1/audio/speech      OpenAI-style JSON        -> streamed audio from an OpenAI-compatible TTS endpoint
"""
from __future__ import annotations

import asyncio
import logging
from typing import Awaitable, Callable

import aiohttp
from aiohttp import web

log = logging.getLogger("spare_bridge")

MAX_WAV_BYTES = 2 * 1024 * 1024  # 15 s of 16 kHz mono 16-bit is ~480 KB

Transcriber = Callable[[bytes, str], Awaitable[str]]


def _err(status: int, message: str, typ: str = "error") -> web.Response:
    return web.json_response({"error": {"message": message, "type": typ}}, status=status)


def _join(base: str, tail: str) -> str:
    """Accept {B}, {B}/v1 or the full URL, as the protocol does for the app's addresses."""
    base = base.rstrip("/")
    if base.endswith(tail):
        return base
    if base.endswith("/v1"):
        return base + tail[len("/v1"):]
    return base + tail


def is_wav(data: bytes) -> bool:
    return len(data) > 44 and data[:4] == b"RIFF" and data[8:12] == b"WAVE"


# ---------------------------------------------------------------- ASR transcribers

def make_openai_transcriber(opts: dict) -> Transcriber:
    base = opts.get("base_url", "https://api.openai.com/v1")
    url = _join(base, "/v1/audio/transcriptions")
    key = opts.get("api_key", "")
    model = opts.get("model", "whisper-1")
    timeout = float(opts.get("timeout", 10))

    async def run(wav: bytes, language: str) -> str:
        form = aiohttp.FormData()
        form.add_field("file", wav, filename="audio.wav", content_type="audio/wav")
        form.add_field("model", model)
        form.add_field("response_format", "json")
        if language:
            form.add_field("language", language)
        headers = {"Authorization": f"Bearer {key}"} if key else {}
        async with aiohttp.ClientSession(timeout=aiohttp.ClientTimeout(total=timeout)) as http:
            async with http.post(url, data=form, headers=headers) as r:
                if r.status != 200:
                    raise RuntimeError(f"upstream ASR status {r.status}")
                return str((await r.json(content_type=None)).get("text", ""))
    return run


def make_local_transcriber(opts: dict) -> Transcriber:
    """faster-whisper if installed (pip install faster-whisper); loaded lazily on the first request."""
    holder: dict = {}

    def load():
        if "m" not in holder:
            try:
                from faster_whisper import WhisperModel  # optional dependency
            except ImportError as e:
                raise RuntimeError("provider 'local' needs: pip install faster-whisper") from e
            holder["m"] = WhisperModel(opts.get("model", "small"), device=opts.get("device", "auto"),
                                       compute_type=opts.get("compute_type", "default"))
        return holder["m"]

    def work(wav: bytes, language: str) -> str:
        import io
        segments, _ = load().transcribe(io.BytesIO(wav), language=language or None, beam_size=1)
        return "".join(s.text for s in segments).strip()

    async def run(wav: bytes, language: str) -> str:
        return await asyncio.get_running_loop().run_in_executor(None, work, wav, language)
    return run


def make_transcriber(opts: dict) -> Transcriber:
    provider = opts.get("provider", "openai")
    if provider == "openai":
        return make_openai_transcriber(opts)
    if provider == "local":
        return make_local_transcriber(opts)
    raise ValueError(f"unknown asr provider {provider!r} (openai | local)")


_LANG = {"zh-CN": "zh", "zh-HK": "yue", "en-US": "en"}


async def asr_handler(request: web.Request) -> web.Response:
    from .server import K_STATE, _authorized
    st = request.app[K_STATE]
    if not _authorized(request, st["token"]):
        return _err(401, "invalid token", "unauthorized")
    data = await request.read()
    if not is_wav(data):
        return _err(400, "expected a WAV body (16 kHz mono 16-bit)", "invalid_request")
    language = st["cfg"].asr.get("language") or _LANG.get(request.headers.get("X-Spare-Locale", ""), "")
    try:
        text = await st["asr"](data, language)
    except Exception as e:  # noqa: BLE001
        log.warning("asr error: %s", e)
        return _err(502, "speech recognition failed", "backend_error")
    return web.json_response({"text": text.strip()})


# ---------------------------------------------------------------- TTS proxy

async def tts_handler(request: web.Request) -> web.StreamResponse:
    from .server import K_STATE, _authorized
    st = request.app[K_STATE]
    cfg = st["cfg"].tts
    if not _authorized(request, st["token"]):
        return _err(401, "invalid token", "unauthorized")
    try:
        req = await request.json()
        text = str(req["input"]).strip()
        if not text:
            raise ValueError
    except (ValueError, KeyError, TypeError):
        return _err(400, "expected {input: text, ...}", "invalid_request")

    payload = {"model": req.get("model") or cfg.get("model", "tts-1"),
               "voice": req.get("voice") or cfg.get("voice", "alloy"),
               "input": text, "response_format": req.get("response_format") or "pcm"}
    if cfg.get("speed") is not None:
        payload["speed"] = cfg["speed"]
    url = _join(cfg.get("base_url", "https://api.openai.com/v1"), "/v1/audio/speech")
    key = cfg.get("api_key", "")
    headers = {"Authorization": f"Bearer {key}"} if key else {}
    if "http" not in st:
        st["http"] = aiohttp.ClientSession()  # created lazily inside the running loop
    http: aiohttp.ClientSession = st["http"]
    try:
        up = await http.post(url, json=payload, headers=headers,
                             timeout=aiohttp.ClientTimeout(total=None, sock_connect=5, sock_read=float(cfg.get("timeout", 15))))
    except (aiohttp.ClientError, asyncio.TimeoutError) as e:
        log.warning("tts upstream error: %s", type(e).__name__)
        return _err(502, "speech synthesis failed", "backend_error")
    try:
        if up.status != 200:
            return _err(502, f"upstream TTS status {up.status}", "backend_error")
        ctype = up.headers.get("Content-Type", "application/octet-stream")
        if ctype.startswith(("application/json", "text/")):  # gateways that report errors with 200
            return _err(502, "upstream TTS returned no audio", "backend_error")
        h = {"Content-Type": ctype, "Cache-Control": "no-store", "X-Accel-Buffering": "no"}
        if payload["response_format"] == "pcm" and "rate=" not in ctype:
            h["X-Sample-Rate"] = str(cfg.get("sample_rate", 24000))
        resp = web.StreamResponse(status=200, headers=h)
        await resp.prepare(request)
        async for chunk in up.content.iter_chunked(4096):
            await resp.write(chunk)
        await resp.write_eof()
        return resp
    finally:
        up.release()
