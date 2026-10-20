"""Sentence-level chunking so the glasses can start speaking the first sentence early."""
from __future__ import annotations

import re

STRONG = set("。！？!?；;\n…")
CLAUSE = set("，,、：:")
CLOSERS = set("”’\"')）」』】")
_MD = re.compile(r"[*`#>]+|(?<!\w)_{1,2}|_{1,2}(?!\w)")
_LINK = re.compile(r"\[([^\]]*)\]\([^)]*\)")


def clean(text: str) -> str:
    """Strip the Markdown that a model sometimes adds; the lens shows plain text."""
    text = _LINK.sub(r"\1", text)
    return _MD.sub("", text)


class SentenceChunker:
    """Feed token deltas, get back complete sentences.

    The first chunk of a reply may also end at a clause boundary (comma) once it is
    at least `first_min` characters long, which gets the first words out sooner.
    """

    def __init__(self, mode: str = "sentence", first_min: int = 6) -> None:
        self.mode = mode
        self.first_min = first_min
        self.buf = ""
        self.sent_any = False
        self.last = ""

    def _cut(self, final: bool) -> int:
        b = self.buf
        for i, ch in enumerate(b):
            hit = ch in STRONG
            if not hit and ch == ".":
                nxt = b[i + 1] if i + 1 < len(b) else None
                hit = (nxt is None and final) or (nxt is not None and nxt.isspace())
            if not hit and not self.sent_any and ch in CLAUSE and i + 1 >= self.first_min:
                hit = True
            if hit:
                j = i + 1
                while j < len(b) and (b[j] in CLOSERS or b[j] in STRONG):
                    j += 1
                if j == len(b) and not final and ch == ".":
                    return -1
                return j
        return -1

    def _join(self, piece: str) -> str:
        """Keep the space between Latin-script sentences that strip() removed."""
        if piece and self.last and self.last[-1].isascii() and piece[0].isascii():
            piece = " " + piece
        if piece:
            self.last = piece
        return piece

    def feed(self, text: str) -> list[str]:
        if self.mode == "token":
            t = clean(text)
            return [t] if t else []
        self.buf += text
        out: list[str] = []
        while True:
            j = self._cut(final=False)
            if j < 0:
                break
            piece, self.buf = self.buf[:j], self.buf[j:]
            piece = self._join(clean(piece).strip())
            if piece:
                out.append(piece)
                self.sent_any = True
        return out

    def flush(self) -> list[str]:
        if self.mode == "token":
            return []
        rest = self._join(clean(self.buf).strip())
        self.buf = ""
        if rest:
            self.sent_any = True
            return [rest]
        return []
