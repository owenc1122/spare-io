"""Codex / Claude adapters against tiny fake CLIs that print the documented JSONL shapes."""
import json
import stat
import sys

from helpers import ask, parse_sse, run, serve, text_of
from spare_bridge.config import Config
from spare_bridge.server import create_app

FAKE_CODEX = """#!{py}
import json, sys
a = sys.argv[2:]
resume = a[1] if a[:1] == ["resume"] else None
tid = resume or "thread-123"
print(json.dumps({{"type": "thread.started", "thread_id": tid}}))
print(json.dumps({{"type": "item.completed", "item": {{"type": "agent_message", "text": "收到。" + ("续接 " + resume if resume else "新会话") + "。"}}}}))
print(json.dumps({{"type": "turn.completed"}}))
"""

FAKE_CLAUDE = """#!{py}
import json, sys
prompt = sys.stdin.read()
a = sys.argv[1:]
sid = a[a.index("--resume") + 1] if "--resume" in a else "claude-sess-1"
print(json.dumps({{"type": "system", "subtype": "init", "session_id": sid}}))
for t in ["你说", "：" + prompt + "。", "好的"]:
    print(json.dumps({{"type": "stream_event", "event": {{"type": "content_block_delta", "delta": {{"type": "text_delta", "text": t}}}}}}))
print(json.dumps({{"type": "result", "session_id": sid, "result": "x", "is_error": False}}))
"""


def make(tmp_path, name, src):
    p = tmp_path / name
    p.write_text(src.format(py=sys.executable))
    p.chmod(p.stat().st_mode | stat.S_IEXEC)
    return str(p)


def test_codex_thread_resume(tmp_path):
    cmd = make(tmp_path, "codex", FAKE_CODEX)

    async def go():
        async with serve(backend="codex", backends={"codex": {"command": cmd}}) as (s, http, _):
            _, ev, _ = await ask(s, http, "你好")
            assert "新会话" in text_of(ev)
            _, ev, _ = await ask(s, http, "再问")
            assert "续接 thread-123" in text_of(ev)
    run(go())


def test_claude_stream_and_resume(tmp_path):
    cmd = make(tmp_path, "claude", FAKE_CLAUDE)

    async def go():
        async with serve(backend="claude", backends={"claude": {"command": cmd}}) as (s, http, app):
            _, ev, _ = await ask(s, http, "你好")
            assert text_of(ev) == "你说：你好。好的"
            st = [v for v in app.values() if isinstance(v, dict)][0]
            assert st["sessions"].get("sg-1").state["claude_session"] == "claude-sess-1"
    run(go())
