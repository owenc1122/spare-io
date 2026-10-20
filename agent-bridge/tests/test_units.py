import time

from spare_bridge.actions import ActionFilter, parse_marker
from spare_bridge.chunker import SentenceChunker, clean
from spare_bridge.sessions import SessionStore


def feed_all(ch, s, step=1):
    out = []
    for i in range(0, len(s), step):
        out += ch.feed(s[i:i + step])
    return out + ch.flush()


def test_chunker_sentences_and_first_clause():
    assert feed_all(SentenceChunker(), "好的，我来查一下。结果是三十度！") == ["好的，我来查一下。", "结果是三十度！"]
    assert feed_all(SentenceChunker(), "好的我来帮你查一下，结果是三十度。") == ["好的我来帮你查一下，", "结果是三十度。"]
    assert feed_all(SentenceChunker(), "Sure. It is 3.5 degrees now. Bye") == ["Sure.", " It is 3.5 degrees now.", " Bye"]
    assert feed_all(SentenceChunker(), "你好，世界。", step=1) == ["你好，世界。"]  # comma < first_min


def test_chunker_cleans_markdown():
    assert clean("**好** [链接](http://x) `代码`") == "好 链接 代码"


def test_action_filter_split_across_deltas():
    f = ActionFilter()
    out = []
    for piece in ["好的[", "[card:天气|晴|2", "5度]", "] 再见[[end]]"]:
        out += f.feed(piece)
    out += f.flush()
    text = "".join(x for x in out if isinstance(x, str))
    acts = [x for x in out if isinstance(x, dict)]
    assert text == "好的 再见"
    assert acts == [{"type": "show_card", "title": "天气", "rows": ["晴", "25度"]}, {"type": "end_conversation"}]


def test_parse_marker_limits():
    assert parse_marker("listen:999")["seconds"] == 120
    assert parse_marker("listen:1")["seconds"] == 5
    assert len(parse_marker("card:t|1|2|3|4")["rows"]) == 3
    assert parse_marker("rm -rf") is None


def test_session_cleanup():
    st = SessionStore(ttl=0.05)
    st.get("a")
    time.sleep(0.1)
    st.get("b")
    assert st.cleanup() == 1 and len(st) == 1
