from __future__ import annotations

import argparse
import logging
import socket

from aiohttp import web

from .config import load_config, resolve_token
from .server import create_app


def lan_ips() -> list[str]:
    try:
        infos = socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET)
    except OSError:
        return []
    return sorted({i[4][0] for i in infos if not i[4][0].startswith("127.")})


def main() -> None:
    ap = argparse.ArgumentParser(prog="spare_bridge", description="Spare Agent Protocol v1 reference bridge")
    ap.add_argument("--config", help="path to config.toml")
    ap.add_argument("--backend", help="echo | openai | codex | claude")
    ap.add_argument("--host")
    ap.add_argument("--port", type=int)
    ap.add_argument("--no-auth", action="store_true", help="disable the bearer token (trusted LAN testing only)")
    a = ap.parse_args()

    cfg = load_config(a.config)
    for k in ("backend", "host", "port"):
        if getattr(a, k) is not None:
            setattr(cfg, k, getattr(a, k))
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")

    token = None
    if a.no_auth:
        print("WARNING: authentication disabled")
    else:
        token, new = resolve_token(cfg)
        if new:
            print(f"\n  New access token (shown once; saved to {cfg.token_file}):\n\n    {token}\n")
        else:
            print("  Using the configured access token (not printed).")
    app = create_app(cfg, token)
    print(f"  Backend: {cfg.backend}   Listening on {cfg.host}:{cfg.port}")
    for ip in lan_ips():
        print(f"  In the Spare io app, address: http://{ip}:{cfg.port}")
    web.run_app(app, host=cfg.host, port=cfg.port, print=None, handler_cancellation=True)


if __name__ == "__main__":
    main()
