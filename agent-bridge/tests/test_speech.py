import json
import struct

import aiohttp
from aiohttp import web
from aiohttp.test_utils import TestServer
from helpers import TOKEN, hdrs, run, serve


def wav(n=3200):
    pcm = b"\x01\x00" * n
    return (b"RIFF" + struct.pack("<I", 36 + len(pcm)) + b"WAVEfmt " + struct.pack("<IHHIIHH", 16, 1, 1, 16000, 32000, 2, 16)
            + b"data" + struct.pack("<I", len(pcm)) + pcm)


WAV_H = {"Content-Type": "audio/wav; rate=16000; channels=1; bits=16", "Authorization": f"Bearer {TOKEN}"}


def test_disabled_by_default():
    async def go():
        async with serve() as (s, http, _):
            assert (await http.post(s.make_url("/v1/asr"), data=wav(), headers=WAV_H)).status == 404
            assert (await http.post(s.make_url("/v1/audio/speech"), json={"input": "x"}, headers=hdrs())).status == 404
    run(go())


def test_asr_with_fake_transcriber(monkeypatch):
    calls = []

    async def fake(data, language):
        calls.append((len(data), language))
        return "  快答  "

    import spare_bridge.server as srv
    monkeypatch.setattr(srv, "make_transcriber", lambda opts: fake)

    async def go():
        async with serve(asr={"enabled": True}) as (s, http, _):
            r = await http.post(s.make_url("/v1/asr"), data=wav(), headers={**WAV_H, "X-Spare-Locale": "zh-HK"})
            assert r.status == 200 and await r.json() == {"text": "快答"}
            assert calls[0][1] == "yue"
            r = await http.post(s.make_url("/v1/asr"), data=b"not wav" * 20, headers=WAV_H)
            assert r.status == 400
            r = await http.post(s.make_url("/v1/asr"), data=wav(), headers={"Content-Type": "audio/wav"})
            assert r.status == 401
    run(go())


def test_asr_failure_is_502(monkeypatch):
    async def boom(data, language):
        raise RuntimeError("x")

    import spare_bridge.server as srv
    monkeypatch.setattr(srv, "make_transcriber", lambda opts: boom)

    async def go():
        async with serve(asr={"enabled": True}) as (s, http, _):
            r = await http.post(s.make_url("/v1/asr"), data=wav(), headers=WAV_H)
            assert r.status == 502 and "error" in await r.json()
    run(go())


def test_asr_openai_proxy_against_fake_upstream():
    seen = {}

    async def transcriptions(request):
        seen["auth"] = request.headers.get("Authorization")
        form = await request.post()
        seen["model"] = form["model"]
        seen["file"] = form["file"].file.read()
        return web.json_response({"text": "你好"})

    async def go():
        up = web.Application()
        up.router.add_post("/v1/audio/transcriptions", transcriptions)
        up_srv = TestServer(up)
        await up_srv.start_server()
        try:
            opts = {"enabled": True, "provider": "openai", "base_url": str(up_srv.make_url("/v1")),
                    "api_key": "upstream-key", "model": "m1"}
            async with serve(asr=opts) as (s, http, _):
                data = wav()
                r = await http.post(s.make_url("/v1/asr"), data=data, headers=WAV_H)
                assert r.status == 200 and (await r.json())["text"] == "你好"
                assert seen["auth"] == "Bearer upstream-key" and seen["model"] == "m1" and seen["file"] == data
        finally:
            await up_srv.close()
    run(go())


def test_tts_proxy_streams_pcm():
    seen = {}

    async def speech(request):
        seen["body"] = await request.json()
        seen["auth"] = request.headers.get("Authorization")
        resp = web.StreamResponse(headers={"Content-Type": "audio/pcm"})
        await resp.prepare(request)
        for i in range(3):
            await resp.write(bytes([i]) * 1000)
        return resp

    async def go():
        up = web.Application()
        up.router.add_post("/v1/audio/speech", speech)
        up_srv = TestServer(up)
        await up_srv.start_server()
        try:
            opts = {"enabled": True, "base_url": str(up_srv.make_url("/v1")), "api_key": "k", "model": "tm", "voice": "v1"}
            async with serve(tts=opts) as (s, http, _):
                r = await http.post(s.make_url("/v1/audio/speech"), json={"input": "好的。", "stream": True}, headers=hdrs())
                body = await r.read()
                assert r.status == 200 and len(body) == 3000
                assert r.headers["Content-Type"] == "audio/pcm" and r.headers["X-Sample-Rate"] == "24000"
                assert seen["auth"] == "Bearer k"
                assert seen["body"] == {"model": "tm", "voice": "v1", "input": "好的。", "response_format": "pcm"}
                # app-supplied model/voice win over config
                await http.post(s.make_url("/v1/audio/speech"), json={"input": "a", "model": "m2", "voice": "v2"}, headers=hdrs())
                assert seen["body"]["model"] == "m2" and seen["body"]["voice"] == "v2"
                r = await http.post(s.make_url("/v1/audio/speech"), json={"input": "a"})
                assert r.status == 401
                r = await http.post(s.make_url("/v1/audio/speech"), json={"nope": 1}, headers=hdrs())
                assert r.status == 400
        finally:
            await up_srv.close()
    run(go())


def test_tts_upstream_error_and_json_200():
    async def bad(request):
        return web.json_response({"error": "x"}, status=200)

    async def go():
        up = web.Application()
        up.router.add_post("/v1/audio/speech", bad)
        up_srv = TestServer(up)
        await up_srv.start_server()
        try:
            async with serve(tts={"enabled": True, "base_url": str(up_srv.make_url("/v1"))}) as (s, http, _):
                r = await http.post(s.make_url("/v1/audio/speech"), json={"input": "a"}, headers=hdrs())
                assert r.status == 502
        finally:
            await up_srv.close()
        async with serve(tts={"enabled": True, "base_url": "http://127.0.0.1:1/v1"}) as (s, http, _):
            r = await http.post(s.make_url("/v1/audio/speech"), json={"input": "a"}, headers=hdrs())
            assert r.status == 502
    run(go())


def test_example_config_keeps_extensions_off():
    from pathlib import Path
    from spare_bridge.config import load_config
    cfg = load_config(str(Path(__file__).resolve().parents[1] / "config.example.toml"))
    assert cfg.asr["enabled"] is False and cfg.tts["enabled"] is False
