"""Inline action markers -> Spare `spare.action` events.

A model (or any text backend) can write markers in its reply:

    [[card:Title|row 1|row 2|icon=check]]   show a small card (max 3 rows)
    [[end]]                                 close the conversation after this answer
    [[listen:30]]                           keep the microphone open for 30 seconds

The filter removes the markers from the spoken text and returns action dicts.
"""
from __future__ import annotations

ICONS = {"info", "check", "clock", "pin", "flag", "car", "nav", "alert", "spark"}


def parse_marker(inner: str) -> dict | None:
    name, _, arg = inner.strip().partition(":")
    name = name.strip().lower()
    if name in ("end", "end_conversation"):
        return {"type": "end_conversation"}
    if name in ("listen", "keep_listening"):
        try:
            secs = int(arg.strip() or 30)
        except ValueError:
            return None
        return {"type": "keep_listening", "seconds": max(5, min(120, secs))}
    if name in ("card", "show_card"):
        parts = [p.strip() for p in arg.split("|")]
        if not parts or not parts[0]:
            return None
        card: dict = {"type": "show_card", "title": parts[0][:10]}
        rows: list[str] = []
        for p in parts[1:]:
            if p.lower().startswith("icon="):
                icon = p[5:].strip().lower()
                if icon in ICONS:
                    card["icon"] = icon
            elif p:
                rows.append(p)
        if rows:
            card["rows"] = rows[:3]
        return card
    return None


class ActionFilter:
    def __init__(self, enabled: bool = True) -> None:
        self.enabled = enabled
        self.buf = ""

    def feed(self, text: str) -> list[str | dict]:
        if not self.enabled:
            return [text] if text else []
        self.buf += text
        out: list[str | dict] = []
        while True:
            i = self.buf.find("[[")
            if i < 0:
                keep = 1 if self.buf.endswith("[") else 0
                cut = len(self.buf) - keep
                if cut:
                    out.append(self.buf[:cut])
                self.buf = self.buf[cut:]
                return out
            if i:
                out.append(self.buf[:i])
            self.buf = self.buf[i:]
            j = self.buf.find("]]")
            if j < 0:
                if len(self.buf) > 300:  # not a marker after all
                    out.append(self.buf[:2])
                    self.buf = self.buf[2:]
                    continue
                return out
            act = parse_marker(self.buf[2:j])
            if act:
                out.append(act)
            self.buf = self.buf[j + 2:]

    def flush(self) -> list[str | dict]:
        rest, self.buf = self.buf, ""
        return [rest] if rest and not rest.startswith("[[") else []
