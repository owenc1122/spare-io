from __future__ import annotations

from ..config import Config
from .base import Backend, Turn


def create_backend(cfg: Config) -> Backend:
    name = cfg.backend
    o = lambda k, d=None: cfg.opt(name, k, d)  # noqa: E731
    if name == "echo":
        from .echo import EchoBackend
        return EchoBackend()
    if name == "openai":
        from .openai import OpenAIBackend
        base = o("base_url", "http://127.0.0.1:11434/v1")  # Ollama default
        return OpenAIBackend(base, o("model", "qwen2.5:7b"), o("api_key", ""), int(o("max_tokens", 200)),
                             o("temperature"), int(o("history_messages", 16)), o("extra_body", {}))
    if name == "codex":
        from .codex import CodexBackend
        return CodexBackend(o("command", "codex"), o("workdir", ""), o("extra_args", []))
    if name == "claude":
        from .claude import ClaudeBackend
        return ClaudeBackend(o("command", "claude"), o("workdir", ""), o("model", ""), o("extra_args", []))
    raise SystemExit(f"unknown backend {name!r}; use echo, openai, codex or claude")


__all__ = ["Backend", "Turn", "create_backend"]
