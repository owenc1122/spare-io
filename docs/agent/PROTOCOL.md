# Spare Agent Protocol v1

[简体中文](PROTOCOL.zh-CN.md)

How a developer's own agent (Codex, OpenClaw, a local model, a script, a self-hosted service) takes over the voice assistant of
Spare io smart glasses.

This document is self-contained. It describes only the HTTP interface between the Spare io phone app and your agent
server. Nothing about the glasses hardware, firmware or the phone-to-glasses link is needed, or specified here.

## 1. Model

```
glasses mic -> phone (speech to text) -> POST /v1/chat/completions (SSE) -> your agent
glasses lens <- phone (card + text)   <- streamed reply sentences        <-+
```

- The phone does speech recognition. The agent receives **text** and answers with **text**.
- When the user turns on **Settings > 开发者功能 (Developer) > 接入自己的 Agent (Connect your own agent) > Agent 接管 AI 助手 (Agent takes over the AI assistant)**,
  every utterance after the wake word or the long press goes to the agent: no local commands, no built-in tools, no
  fast paths, no second model in between. Follow-up utterances in the same conversation (the mic stays open 20 to 40 seconds
  after each answer) are further turns of the same session.
- The agent owns persona, memory, tools and side effects. The phone never runs the agent's tools and never offers
  its own.
- The reply is shown on a very small display (about 3 lines of 14 CJK characters) and optionally read aloud sentence by sentence
  while it streams.

The wire format is the **OpenAI Chat Completions streaming API**, so any OpenAI-compatible server works with no changes.
Everything Spare-specific is optional and additive (headers, one extra body field, one extra SSE event type, one handshake endpoint).

## 2. Base URL and authentication

The user enters a **base URL** `B`, for example `https://agent.example.com`, `http://192.168.1.20:8787` or
`https://agent.example.com/v1`.

| Purpose   | URL                                                   |
|-----------|-------------------------------------------------------|
| Chat      | `POST {B}/v1/chat/completions`                        |
| Handshake | `GET  {B}/v1/spare/agent`                             |

If `B` already ends in `/v1` it is not repeated. If `B` ends in `/v1/chat/completions` it is used as the chat URL as is.

Authentication: if the user entered a token, every request carries `Authorization: Bearer <token>`. The token is stored in the phone's
secure storage and is never logged. Without a token no header is sent. Servers exposed beyond a trusted network MUST require a token and use HTTPS.
Plain `http://` works for IP-address hosts on the same local network (the phone asks the user for local network access).

## 3. Chat request

`POST {B}/v1/chat/completions`, `Content-Type: application/json`, `Accept: text/event-stream`.

Headers (all optional for the agent to use):

| Header              | Value |
|---------------------|-------|
| `X-Spare-Protocol`  | `1` |
| `X-Spare-Session`   | Conversation id, for example `sg-3f2a...`. Stable across all turns of one conversation; **rotates after 5 minutes without a request** (a new conversation). Treat it as an opaque string. |
| `X-Spare-Device`    | Anonymous device id, for example `sd-9c41d0e27a5b13f8`: a hash of a random per-install value. Not reversible, not tied to any account, stable per app install. |
| `X-Spare-Locale`    | Conversation language: `zh-CN`, `zh-HK` (Cantonese) or `en-US`. Answer in this language. |

Body:

```json
{
  "model": "spare-agent",
  "stream": true,
  "messages": [
    {"role": "user", "content": "remind me to buy milk"},
    {"role": "assistant", "content": "Done, I will remind you."},
    {"role": "user", "content": "what did I just ask you to remember"}
  ],
  "spare": {
    "session": "sg-3f2a...",
    "turn": 3,
    "locale": "zh-CN",
    "time": "2026-10-20T07:15:30Z",
    "display": {"lines": 3, "chars_per_line": 14, "speaks": false}
  }
}
```

- `model` is a fixed placeholder; ignore it or map it as you like.
- **There is no system message.** The agent supplies its own persona. (Servers that need a system prompt should add it themselves.)
  No tool definitions are ever sent.
- `messages` has two modes:
  - **History mode (default).** The phone sends the last up to 8 turns (user and assistant text, oldest first, clipped) and the new
    user utterance last. The agent may be completely stateless.
  - **Session-memory mode.** If the handshake (section 5) advertised `session-memory`, `messages` contains **only the new utterance**
    (one `user` message) and the agent is expected to remember the conversation itself, keyed by `X-Spare-Session`.
- `spare.turn` counts requests in the current session, starting at 1. `spare.time` is the phone's current time (UTC, ISO 8601).
  `spare.display` describes the screen; `speaks` is true when the answer is also read aloud. Servers that do not know the `spare`
  field ignore it, as OpenAI-compatible servers do for unknown fields.
- One utterance is one request. The phone never sends a second identical request (no retries, no hedging) because the agent may have
  side effects. If a request fails, the user is told on the glasses and speaks again.

## 4. Chat response (SSE)

Standard OpenAI streaming: `Content-Type: text/event-stream`, events of the form

```
data: {"choices":[{"index":0,"delta":{"content":"Sure, "}}]}

data: {"choices":[{"index":0,"delta":{"content":"I will remind you."}}]}

data: {"choices":[{"index":0,"delta":{},"finish_reason":"stop"}]}

data: [DONE]
```

- Only `choices[0].delta.content` text is used. Each piece is appended to the lens as it arrives, and complete sentences (ending in
  `. ! ? ; : , ` or the CJK equivalents) are queued for text-to-speech as soon as they are complete.
  The first visible text is what the user perceives as the response time, so see section 7.
- `delta.tool_calls` and any other fields are ignored (the phone never executes anything the agent sends as OpenAI tool calls).
- The stream ends with `finish_reason` and/or `data: [DONE]`. A stream that simply closes after some text is also accepted.
  A stream that closes with neither text nor a finish marker is an error; a finished stream with empty text shows a generic "no answer" line.
- SSE comment lines (`: ping`) are ignored and keep the connection alive (see section 7).
- Reply text should be plain: no Markdown, lists, links, code or emoji. It is displayed and spoken as is.

### 4.1 Glasses actions

The agent may emit actions with a **named SSE event**, anywhere in the stream:

```
event: spare.action
data: {"type":"show_card","title":"Shopping","rows":["milk","eggs"],"icon":"check"}

```

Standard OpenAI clients ignore unknown event names, so the same agent stays usable elsewhere. The set of actions is deliberately tiny and
cannot control the device: it only changes what the conversation shows or when it ends. Unknown or malformed actions are ignored.

| `type`             | Fields | Effect |
|--------------------|--------|--------|
| `show_card`        | `title` (required, at most 10 characters recommended), optional `value`, `unit`, `subtitle`, `status`, `rows` (array of up to 3 strings), `icon` (`info`, `check`, `clock`, `pin`, `flag`, `car`, `nav`, `alert`, `spark`), `progress` (0 to 100) | Shows a structured card on the open conversation card at once. Send the spoken sentence as normal `content` as well. |
| `end_conversation` | none | After this answer is complete, close the conversation: the microphone does not reopen for a follow-up. Without it, the mic stays open for a follow-up (20 to 40 seconds, longer for long answers). |
| `keep_listening`   | `seconds` (integer, clamped to 5..120, default 30) | After this answer, keep the follow-up window open for this long instead of the default. Use it when you asked a question and expect a slower reply. |

If the same action type is sent twice in one turn, the later one wins (cards: each one is shown in order).

### 4.2 Errors

- HTTP status other than 2xx before the stream: `401`/`403` is shown as a token problem; `404`/`405` as a wrong address; `429`, `408` and `5xx`
  as "agent is busy or failing (status)"; other `4xx` as "agent error". The JSON body is optional:
  `{"error":{"message":"...","type":"..."}}` (the message is shown, shortened to about 40 characters).
- An error after the stream started: send `data: {"error":{"message":"...","type":"..."}}` or `event: error` with the same JSON, then close.
  Text that was already streamed stays on the lens; the error message replaces it.
- Network failure or no response: "can not reach your agent" or "agent took too long". No automatic retry.

Everything above ends in a short readable message on the glasses and the conversation closes after a few seconds. The glasses never stay on
a thinking state indefinitely.

## 5. Handshake

`GET {B}/v1/spare/agent` (with `Authorization` when a token is set). Used by the app's **Test connection** button and as a connection warm-up
when a conversation starts (a quick `401`/`404` answer is fine for the warm-up). Response `200`, JSON:

```json
{
  "protocol": 1,
  "name": "My Codex bridge",
  "model": "codex",
  "capabilities": ["streaming", "session-memory", "actions"]
}
```

| Field          | Meaning |
|----------------|---------|
| `protocol`     | Required, integer. Must be `1` for this document. A different number is reported to the user as unsupported. |
| `name`         | Optional display name (shown in Settings, 40 characters). |
| `model`        | Optional informational string. |
| `capabilities` | Optional array. `streaming` (always true in v1), `actions` (the agent may emit section 4.1 events), `session-memory` (send only the new utterance, see section 3). Unknown strings are ignored. |

If the handshake returns `404`/`405`, the server is treated as a plain OpenAI-compatible server: it works in history mode with no actions.
`401`/`403` means the token is wrong. The handshake result is remembered from the last successful test; after the user changes the address it is forgotten
until the next test.

## 6. Sessions and memory

- One `X-Spare-Session` id per conversation: the wake word or long press starts (or continues) it; follow-ups reuse it; after **5 minutes
  without any request** the next request carries a new id. The same id is also reset when the user turns the agent off and on, or changes its address.
- A stateless agent just uses `messages`. A stateful agent keys its memory on `X-Spare-Session` and advertises `session-memory`; it must tolerate that
  a session id never comes back, and should expire idle sessions itself.
- The phone sends nothing about the user's location, contacts, calendar or other data. `spare.time` and the locale are the only context. Anything else
  the agent knows, it knows from its own tools.

## 7. Timeouts and latency

The experience target is **a first visible word in under 1 second after the user stops speaking**, and speech that starts at the first complete sentence.

- Return the response headers and flush the first SSE bytes immediately; do not buffer the stream. Disable proxy buffering (for example nginx `proxy_buffering off`, `X-Accel-Buffering: no`).
- Stream by sentence or smaller. Put a short acknowledging sentence first if the agent has to use tools ("Checking your calendar.") and the real answer after it.
  Never emit a long preamble; the display holds about 40 characters of useful text. Aim for 1 to 2 sentences, at most about 80 characters, unless the user asked for detail.
- While the agent works without producing text, send an SSE comment line (`: ping`) at least every 10 seconds. The connection times out after
  **60 seconds without any byte**; and if **no answer text at all** arrives within **60 seconds** of the end of the user's utterance, the conversation ends with a "no answer" message.
- The handshake times out after 8 seconds.
- Keep-alive HTTP (HTTP/2 where possible) is used; the phone warms the connection to the agent when a conversation starts.
- Speculative requests are **not** used in v1: the phone only sends a request once the user has finished speaking, so an agent never sees half-finished
  or cancelled utterances. If the user starts speaking again while the agent is still working, the request is cancelled by closing the connection: stop work promptly when the
  client disconnects.

## 8. Security and privacy summary

- Authentication is a single bearer token chosen by the user. Use HTTPS outside a trusted local network.
- The phone sends: the spoken text, recent turns (history mode), a random per-conversation id, an anonymous per-install id, locale and time. It sends no account, phone number, location or contact data.
- The agent can only: speak text, show a card, end the conversation, or hold the microphone open. It cannot start navigation, change settings, install anything or touch the glasses' system.
- The token is never written to logs; the app's request traces contain only the host name and timing.

## 9. Minimal server (reference, Python)

A complete, tested reference bridge with Codex, Claude Code, OpenAI-compatible and test backends lives in [`agent-bridge/`](../../agent-bridge/). The minimal version below only shows the wire format.

```python
# pip install fastapi uvicorn   ->   uvicorn agent:app --host 0.0.0.0 --port 8787
import json, time
from fastapi import FastAPI, Request
from fastapi.responses import StreamingResponse, JSONResponse

app = FastAPI()

@app.get("/v1/spare/agent")
def hello():
    return {"protocol": 1, "name": "Echo agent", "capabilities": ["streaming", "actions"]}

@app.post("/v1/chat/completions")
async def chat(req: Request):
    body = await req.json()
    session = req.headers.get("x-spare-session", "")
    text = body["messages"][-1]["content"]

    def chunk(delta, finish=None):
        return "data: " + json.dumps({"choices": [{"index": 0, "delta": delta, "finish_reason": finish}]}, ensure_ascii=False) + "\n\n"

    def stream():
        yield chunk({"content": "You said: "})
        yield chunk({"content": text + "."})
        yield 'event: spare.action\ndata: {"type":"show_card","title":"Echo","rows":["' + text[:20].replace('"', "") + '"]}\n\n'
        yield chunk({}, "stop")
        yield "data: [DONE]\n\n"

    return StreamingResponse(stream(), media_type="text/event-stream", headers={"X-Accel-Buffering": "no"})
```

## 10. Versioning

`protocol` is an integer. Additions that old apps can ignore (new optional fields, new action types, new capability strings) do not change it. A breaking change
increments it, and the app tells the user when the agent advertises a version it does not support.

## 11. Optional extension (proposal): custom speech recognition

Everything above assumes the phone's own recogniser. Because the agent receives **text**
and never audio, a mis-recognition cannot be repaired on the server — and on the glasses
the recogniser is the weak link: the microphone is far-field, the audio arrives over
Bluetooth, and the interaction is built on short utterances, where an acoustic model has
the least context to recover from. Two-syllable Chinese commands are the common failure
case ("快答" → "块答", "克劳德" → "克劳得").

This section is a **proposal**, not implemented behaviour. It adds an optional,
off-by-default path and leaves protocol `1`, §2 authentication, §3–§6 and every existing
server unchanged.

### 11.1 Setting

Under 设置 → 开发者功能 → 接入自己的 Agent, next to 地址 and 令牌:

| field | values |
|---|---|
| 语音识别 | `系统`（默认） / `自定义端点` |
| 识别端点 `{ASR_BASE}` | `https://…`; required when 自定义端点 is on |
| 识别令牌 | optional; empty = reuse the agent token |

### 11.2 Request

`POST {ASR_BASE}/v1/asr`, one utterance per request.

| item | value |
|---|---|
| body | raw audio, mono, 16 kHz, ≤ 15 s |
| `Content-Type` | `audio/wav; rate=16000; channels=1; bits=16` (or `audio/opus`) |
| `Authorization` | `Bearer <token>` when a token is configured |

### 11.3 Response

- `200 {"text": "…"}` — UTF-8. Empty or whitespace-only text means "nothing recognised".
- Any non-2xx, a timeout (suggested 3 s) or an unparsable body: the phone falls back to its
  own recogniser, so this extension can never make the assistant unusable.
- The request is not retried, matching §3.

### 11.4 Privacy

Audio leaves the phone only when `自定义端点` is explicitly enabled, and the app shows a
one-line warning at the moment it is turned on. The endpoint must be `https://` and should
require a token, per §8.
