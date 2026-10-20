# Spare agent bridge

[简体中文](README.zh-CN.md)

A small reference server for the [Spare Agent Protocol v1](../docs/agent/PROTOCOL.md). It lets **your own agent** take over the voice assistant of RayNeo iO glasses running Spare io: multi-turn, streamed sentence by sentence, with optional glasses cards.

```
glasses -> phone (speech to text) -> this bridge -> OpenAI-compatible endpoint | Codex CLI | Claude Code | echo
glasses <- phone (lens + voice)   <- SSE sentences + spare.action events
```

## 5-minute quick start

Needs Python 3.10+.

```sh
cd agent-bridge
python3 -m venv .venv && . .venv/bin/activate
pip install -r requirements.txt

# 1) smoke test with no AI at all
python -m spare_bridge --backend echo
```

On first run the bridge prints an **access token once** (it is saved to `~/.spare-agent-bridge/token`, mode 600) and the address(es) to type into the app, e.g. `http://192.168.x.y:8787`. Set your own with `SPARE_BRIDGE_TOKEN=...`.

Pick a real backend (copy `config.example.toml` to `config.toml` for more options):

| Backend | Run | Notes |
|---|---|---|
| Ollama / LM Studio / vLLM | `SPARE_OPENAI_MODEL=qwen2.5:7b python -m spare_bridge --backend openai` | defaults to `http://127.0.0.1:11434/v1`; set `SPARE_OPENAI_BASE_URL` |
| OpenAI / DeepSeek / any compatible API | `SPARE_OPENAI_BASE_URL=https://api.openai.com/v1 SPARE_OPENAI_API_KEY=... SPARE_OPENAI_MODEL=... python -m spare_bridge --backend openai` | token-by-token streaming, fastest option |
| OpenClaw | enable its OpenAI-compatible Gateway endpoint (see OpenClaw docs), then use the `openai` backend with its base URL, its gateway token as `SPARE_OPENAI_API_KEY`, and the agent/model name it expects | the bridge adds the glasses style prompt and session memory in front |
| Codex CLI | `python -m spare_bridge --backend codex` | runs `codex exec --json`, then `codex exec resume <thread>` for follow-ups. Messages arrive whole, not per token |
| Claude Code | `python -m spare_bridge --backend claude` | runs `claude -p --output-format stream-json --include-partial-messages`, follow-ups use `--resume`. Streams text deltas |

The CLI backends run a coding agent on **your** machine. Point `workdir` at an empty scratch folder and keep its sandbox/permission settings conservative (`extra_args` in `config.toml`, e.g. `--sandbox read-only` for Codex). Anything you let it do from a spoken sentence, it will do.

## Connect the app

1. Phone and computer on the same Wi-Fi (or use an HTTPS tunnel, below).
2. Spare io app: **Settings -> 开发者功能 -> 接入自己的 Agent**.
3. Fill in **地址** (`http://<computer LAN IP>:8787`) and **令牌** (the printed token).
4. Tap **测试连接**. You should see your bridge name.
5. Turn on **Agent 接管 AI 助手**. Wake the assistant and talk; follow-ups within the listening window stay in the same session.

The app allows plain `http://` only for IP addresses on the local network (iOS asks for local-network access once). A host **name** must be `https://`.

### Away from home: HTTPS

- **Tailscale**: `tailscale serve --bg 8787` (or `tailscale funnel`) gives an `https://<machine>.<tailnet>.ts.net` address; bind the bridge to `127.0.0.1` with `--host 127.0.0.1`.
- **Cloudflare Tunnel**: `cloudflared tunnel --url http://127.0.0.1:8787` for a quick test, or a named tunnel for a stable hostname.
- Behind nginx/Caddy: disable response buffering (`proxy_buffering off`), otherwise sentences arrive in one lump.

## Features

- `GET /v1/spare/agent` handshake: protocol 1, capabilities `streaming`, `session-memory`, `actions`.
- `POST /v1/chat/completions` SSE: headers and `: ok` are flushed at once; `: ping` every 10 s while the backend is silent; work is cancelled as soon as the phone disconnects (CLI processes are killed).
- Sessions keyed by `X-Spare-Session` (history, Codex thread id, Claude session id), idle cleanup after 15 min.
- Bearer-token auth (constant-time compare). `--no-auth` exists for throw-away LAN tests only.
- Glasses system prompt: answer only this turn, 40-80 Chinese characters (about 3 lines), plain text, language from `X-Spare-Locale`. Add your persona with `extra_prompt`.
- Actions: the model may write `[[card:Title|row 1|row 2|icon=check]]`, `[[end]]`, `[[listen:30]]`; the bridge strips them from the spoken text and sends `spare.action` events.
- Sentence chunking: the first chunk may end at the first comma so speech starts sooner. `chunking = "token"` forwards raw deltas instead.

## Optional: custom speech recognition and voice (off by default)

Two optional endpoints implement protocol sections 11 and 12. They are disabled unless you set `enabled = true` in `config.toml` (see `[asr]` and `[tts]` in `config.example.toml`) and use the same bearer token as the chat endpoint.

| Endpoint | What it does | Config |
|---|---|---|
| `POST /v1/asr` | Receives the app's WAV (16 kHz mono 16-bit, at most 15 s) and returns `{"text": "..."}`. | `provider = "openai"` forwards to any OpenAI-compatible `/audio/transcriptions` (set `base_url`, `api_key`, `model`). `provider = "local"` runs [faster-whisper](https://github.com/SYSTRAN/faster-whisper) on this machine; install it yourself (`pip install faster-whisper`), it is not a dependency of the bridge. |
| `POST /v1/audio/speech` | Forwards each sentence to an OpenAI-compatible TTS endpoint and streams the audio back as it arrives (`pcm`, 24 kHz unless you set `sample_rate`). | `[tts]` `base_url`, `api_key`, `model`, `voice`. The app's own Model/Voice fields win when filled in. |

In the app (**Settings -> 开发者功能**):

- **自定义语音识别**: turn it on, set the recognition address to your bridge's **HTTPS** address (the ASR address must be `https://`; see "Away from home" above) and leave the key empty to reuse the agent token (only works when it is the same host as the agent address). Tap the test button.
- **Agent 朗读声音**: choose "my own cloud TTS", address = your bridge (`http://<LAN IP>:8787` works on the local network), key = the bridge token, Model/Voice optional. Tap the listen button to hear the time to first audio.

The app falls back to the phone's recogniser and voice if the bridge fails or takes longer than 3 seconds, so a slow upstream makes the experience worse, never broken. Both extensions only apply while the agent has taken over the assistant.

## Latency tips

- First sentence under 1 s needs a streaming backend and a fast model: a small local model, `gpt-4.1-mini`-class, DeepSeek chat, or Claude Haiku. Reasoning models think silently before the first word; lower their effort (`extra_body = { reasoning_effort = "low" }`, Codex `-c model_reasoning_effort=low`).
- Keep `max_tokens` around 200 and the answer short; the lens shows about 40 useful characters.
- Run the bridge close to the model (same machine as Ollama) and avoid proxies that buffer.
- Codex returns whole messages, so its first sentence appears after the model finishes its first message. Prefer the `openai` backend when speed matters most.

## Security notes

- The token is the only protection. Keep it secret, use HTTPS outside your home network, and do not publish the port to the internet without it.
- The bridge never logs the token or the spoken text; request lines only contain method, path and status.
- Anything a CLI backend can do on your computer, a spoken sentence can trigger. Use a sandboxed, scratch working directory.
- The phone sends only the spoken text, recent turns, a random per-conversation id, an anonymous per-install id, locale and time.

## Tests

```sh
pip install -r requirements-dev.txt
python -m pytest
```
