"""Configuration: config.toml (optional) + environment variables + CLI flags."""
from __future__ import annotations

import os
import secrets
from dataclasses import dataclass, field
from pathlib import Path

try:
    import tomllib  # 3.11+
except ModuleNotFoundError:  # pragma: no cover
    import tomli as tomllib


@dataclass
class Config:
    host: str = "0.0.0.0"
    port: int = 8787
    token: str = ""  # empty = generate / load from token file
    no_auth: bool = False
    name: str = "Spare agent bridge"
    backend: str = "echo"
    chunking: str = "sentence"  # "sentence" or "token"
    actions: bool = True
    ping_interval: float = 10.0
    session_ttl: float = 900.0
    extra_prompt: str = ""
    token_file: str = str(Path.home() / ".spare-agent-bridge" / "token")
    backends: dict = field(default_factory=dict)  # per-backend option tables

    def opt(self, name: str, key: str, default=None):
        return self.backends.get(name, {}).get(key, default)


def _env(cfg: Config) -> None:
    e = os.environ
    if e.get("SPARE_BRIDGE_TOKEN"):
        cfg.token = e["SPARE_BRIDGE_TOKEN"]
    if e.get("SPARE_BRIDGE_HOST"):
        cfg.host = e["SPARE_BRIDGE_HOST"]
    if e.get("SPARE_BRIDGE_PORT"):
        cfg.port = int(e["SPARE_BRIDGE_PORT"])
    if e.get("SPARE_BACKEND"):
        cfg.backend = e["SPARE_BACKEND"]
    for env, key in (("SPARE_OPENAI_BASE_URL", "base_url"), ("SPARE_OPENAI_API_KEY", "api_key"),
                     ("SPARE_OPENAI_MODEL", "model")):
        if e.get(env):
            cfg.backends.setdefault("openai", {})[key] = e[env]


def load_config(path: str | None = None) -> Config:
    cfg = Config()
    if path:
        data = tomllib.loads(Path(path).read_text(encoding="utf-8"))
        srv = data.get("server", {})
        for k, v in srv.items():
            if hasattr(cfg, k) and k != "backends":
                setattr(cfg, k, v)
        if "backend" in data:
            cfg.backend = data["backend"]
        for name in ("openai", "codex", "claude", "echo"):
            if name in data:
                cfg.backends[name] = data[name]
    _env(cfg)
    return cfg


def resolve_token(cfg: Config) -> tuple[str, bool]:
    """Return (token, newly_generated). Env/config first, then token file, else generate."""
    if cfg.token:
        return cfg.token, False
    p = Path(cfg.token_file).expanduser()
    try:
        t = p.read_text().strip()
        if t:
            return t, False
    except OSError:
        pass
    t = secrets.token_urlsafe(24)
    try:
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(t + "\n")
        os.chmod(p, 0o600)
    except OSError:
        pass
    return t, True
