import asyncio

from helpers import TOKEN, ask, body, hdrs, parse_sse, run, serve, text_of


def test_handshake():
    async def go():
        async with serve() as (s, http, _):
            r = await http.get(s.make_url("/v1/spare/agent"), headers=hdrs())
            j = await r.json()
            assert r.status == 200 and j["protocol"] == 1
            assert "session-memory" in j["capabilities"] and "streaming" in j["capabilities"]
            assert "actions" in j["capabilities"]
    run(go())


def test_auth_required():
    async def go():
        async with serve() as (s, http, _):
            for h in ({}, {"Authorization": "Bearer nope"}):
                r = await http.get(s.make_url("/v1/spare/agent"), headers=h)
                assert r.status == 401
                r = await http.post(s.make_url("/v1/chat/completions"), json=body("hi"), headers=h)
                assert r.status == 401
                assert "error" in await r.json()
        async with serve(token=None) as (s, http, _):  # auth disabled
            r = await http.get(s.make_url("/v1/spare/agent"))
            assert r.status == 200
    run(go())


def test_sse_format_and_chunks():
    async def go():
        async with serve() as (s, http, _):
            r, ev, raw = await ask(s, http, "你好")
            assert r.status == 200
            assert r.headers["Content-Type"].startswith("text/event-stream")
            assert r.headers["X-Accel-Buffering"] == "no"
            assert raw.endswith("data: [DONE]\n\n")
            msgs = [d for e, d in ev if e == "message" and d != "[DONE]"]
            assert all(m["choices"][0]["index"] == 0 for m in msgs)
            assert msgs[-1]["choices"][0]["finish_reason"] == "stop"
            contents = [m["choices"][0]["delta"]["content"] for m in msgs if m["choices"][0]["delta"].get("content")]
            assert contents == ["你说：你好。", "这是第1轮。"]  # one SSE chunk per sentence
    run(go())


def test_bad_request():
    async def go():
        async with serve() as (s, http, _):
            r = await http.post(s.make_url("/v1/chat/completions"), data="nope", headers=hdrs())
            assert r.status == 400
            assert (await r.json())["error"]["type"] == "invalid_request"
    run(go())


def test_session_memory_and_isolation():
    async def go():
        async with serve() as (s, http, app):
            await ask(s, http, "买牛奶", session="sg-a")
            _, ev, _ = await ask(s, http, "我上一句说了什么", session="sg-a")
            assert "买牛奶" in text_of(ev)
            _, ev, _ = await ask(s, http, "我上一句说了什么", session="sg-b")
            assert "买牛奶" not in text_of(ev)
            _, ev, _ = await ask(s, http, "再来", session="sg-a")
            assert "第3轮" in text_of(ev)
    run(go())


def test_locale_english():
    async def go():
        async with serve() as (s, http, _):
            _, ev, _ = await ask(s, http, "hello", locale="en-US")
            assert text_of(ev).startswith("You said: hello.")
    run(go())


def test_actions_events():
    async def go():
        async with serve() as (s, http, _):
            _, ev, _ = await ask(s, http, "show card please, bye, listen")
            acts = [d for e, d in ev if e == "spare.action"]
            assert [a["type"] for a in acts] == ["show_card", "end_conversation", "keep_listening"]
            assert acts[0]["title"] == "Echo" and acts[2]["seconds"] == 45
            _, ev, raw = await ask(s, http, "marker")  # inline marker -> action, not spoken
            acts = [d for e, d in ev if e == "spare.action"]
            assert acts == [{"type": "show_card", "title": "Marker", "rows": ["row a"], "icon": "check"}]
            assert "[[" not in text_of(ev)
    run(go())


def test_actions_disabled():
    async def go():
        async with serve(actions=False) as (s, http, _):
            r = await http.get(s.make_url("/v1/spare/agent"), headers=hdrs())
            assert "actions" not in (await r.json())["capabilities"]
    run(go())


def test_ping_while_backend_silent():
    async def go():
        async with serve(ping_interval=0.2) as (s, http, _):
            r = await http.post(s.make_url("/v1/chat/completions"), json=body("slow"), headers=hdrs())
            buf = ""
            async with asyncio.timeout(5) if hasattr(asyncio, "timeout") else _nullcm():
                while buf.count(": ping") < 2:
                    buf += (await r.content.readany()).decode()
            ev = parse_sse(buf)
            assert ("comment", "ping") in ev
            assert text_of(ev) == "稍等一下。"  # first sentence arrived before the slow part
            r.close()
    run(go())


def test_first_bytes_flush_immediately():
    async def go():
        async with serve() as (s, http, _):
            r = await http.post(s.make_url("/v1/chat/completions"), json=body("slow"), headers=hdrs())
            first = await asyncio.wait_for(r.content.readany(), 2)
            assert first.startswith(b": ok")
            r.close()
    run(go())


def test_client_disconnect_cancels_backend():
    async def go():
        async with serve(ping_interval=0.2) as (s, http, app):
            backend = [v for v in app.values() if isinstance(v, dict)][0]["backend"]
            r = await http.post(s.make_url("/v1/chat/completions"), json=body("slow"), headers=hdrs())
            await r.content.readany()
            assert backend.started == 1
            r.close()  # phone hangs up
            for _ in range(50):
                if backend.cancelled:
                    break
                await asyncio.sleep(0.1)
            assert backend.cancelled == 1
            # session is not poisoned: a following turn works and the slow turn left no history
            _, ev, _ = await ask(s, http, "hi")
            assert "第1轮" in text_of(ev)
    run(go())


class _nullcm:
    async def __aenter__(self): return self
    async def __aexit__(self, *a): return False
