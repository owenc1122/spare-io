# Spare Agent 协议 v1

[English](PROTOCOL.md)

开发者如何让自己的 Agent（Codex、OpenClaw、本地模型、脚本、自建服务）接管 Spare io 智能眼镜的语音助手。

本文自成一体，只描述 Spare io 手机 App 与你的 Agent 服务器之间的 HTTP 接口，不涉及眼镜硬件、固件或手机到眼镜的链路。

## 1. 模型

```
眼镜麦克风 -> 手机（语音转文字） -> POST /v1/chat/completions (SSE) -> 你的 Agent
眼镜镜片   <- 手机（卡片 + 文字） <- 流式返回的回答句子             <-+
```

- 语音识别在手机上完成。Agent 收到**文字**，回复**文字**。
- 用户在 **设置 > 开发者功能 > 接入自己的 Agent > Agent 接管 AI 助手** 里打开开关后，唤醒词或长按之后的每一句话都交给 Agent：没有本地指令、没有内置工具、没有快捷通道、中间没有第二个模型。同一段对话里的后续发言（每次回答后麦克风保持开启 20 到 40 秒）是同一会话的下一轮。
- 人设、记忆、工具和副作用都归 Agent。手机不会运行 Agent 的工具，也不会提供自己的工具。
- 回答显示在很小的屏幕上（约 3 行、每行 14 个汉字），并可选地边流式输出边逐句朗读。

传输格式是 **OpenAI Chat Completions 流式 API**，所以任何 OpenAI 兼容服务器无需改动即可使用。Spare 特有的部分都是可选且叠加的（几个请求头、一个额外的 body 字段、一种额外的 SSE 事件、一个握手端点）。

## 2. 地址与认证

用户填写一个**基础地址** `B`，例如 `https://agent.example.com`、`http://192.168.1.20:8787` 或 `https://agent.example.com/v1`。

| 用途 | URL |
|------|-----|
| 对话 | `POST {B}/v1/chat/completions` |
| 握手 | `GET  {B}/v1/spare/agent` |

`B` 已以 `/v1` 结尾时不重复添加；`B` 以 `/v1/chat/completions` 结尾时原样作为对话地址。

认证：用户填了令牌时，每个请求带 `Authorization: Bearer <token>`。令牌保存在手机安全存储中，不写日志。没填令牌就不发该头。暴露到可信网络之外的服务器必须要求令牌并使用 HTTPS。同一局域网内，IP 形式主机可以用明文 `http://`（手机会请求本地网络权限）；用主机名则需要 https。

## 3. 对话请求

`POST {B}/v1/chat/completions`，`Content-Type: application/json`，`Accept: text/event-stream`。

请求头（Agent 可选择使用）：

| 头 | 值 |
|----|----|
| `X-Spare-Protocol` | `1` |
| `X-Spare-Session` | 会话 id，如 `sg-3f2a...`。同一段对话的所有轮次保持不变；**5 分钟没有请求后更换**（新对话）。当作不透明字符串处理。 |
| `X-Spare-Device` | 匿名设备 id，如 `sd-9c41d0e27a5b13f8`：对每次安装的随机值做哈希，不可逆、不关联账号、每次安装稳定。 |
| `X-Spare-Locale` | 对话语言：`zh-CN`、`zh-HK`（粤语）或 `en-US`。请用该语言回答。 |

Body：

```json
{
  "model": "spare-agent",
  "stream": true,
  "messages": [
    {"role": "user", "content": "提醒我买牛奶"},
    {"role": "assistant", "content": "好的，我会提醒你。"},
    {"role": "user", "content": "我刚才让你记什么"}
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

- `model` 是固定占位值，可忽略或自行映射。
- **没有 system 消息**，人设由 Agent 自己提供（需要系统提示的服务器自己加）。也不会发送任何工具定义。
- `messages` 有两种模式：
  - **历史模式（默认）**：手机发送最近最多 8 轮（用户和助手文字，旧的在前，已截断），新的用户发言放最后。Agent 可以完全无状态。
  - **会话记忆模式**：如果握手（第 5 节）声明了 `session-memory`，`messages` **只含新的这一句**（一条 `user` 消息），Agent 需要自己按 `X-Spare-Session` 记住对话。
- `spare.turn` 是当前会话内的请求序号，从 1 开始。`spare.time` 是手机当前时间（UTC，ISO 8601）。`spare.display` 描述屏幕；`speaks` 为 true 表示回答同时会朗读。不认识 `spare` 字段的服务器照常忽略它。
- 一句话对应一个请求。手机绝不会重发相同请求（无重试、无对冲），因为 Agent 可能有副作用。请求失败时眼镜上会提示用户，用户再说一次。

## 4. 对话响应（SSE）

标准 OpenAI 流式：`Content-Type: text/event-stream`，事件形如

```
data: {"choices":[{"index":0,"delta":{"content":"好的，"}}]}

data: {"choices":[{"index":0,"delta":{"content":"我会提醒你。"}}]}

data: {"choices":[{"index":0,"delta":{},"finish_reason":"stop"}]}

data: [DONE]
```

- 只使用 `choices[0].delta.content`。每段到达即追加到镜片上，完整的句子（以 `. ! ? ; : ,` 或对应中文标点结尾）一完成就进入朗读队列。用户感知的响应时间就是第一个可见文字出现的时间，见第 7 节。
- `delta.tool_calls` 和其他字段被忽略（手机绝不执行 Agent 以 OpenAI tool call 发来的任何内容）。
- 流以 `finish_reason` 和/或 `data: [DONE]` 结束。输出一些文字后直接关闭连接也可接受。既无文字也无结束标记就关闭属于错误；结束但文字为空，显示通用的“没有回答”提示。
- SSE 注释行（`: ping`）被忽略，用于保活（见第 7 节）。
- 回复应为纯文本：不要 Markdown、列表、链接、代码、emoji。内容原样显示和朗读。

### 4.1 眼镜动作

Agent 可以在流中的任何位置用**具名 SSE 事件**发出动作：

```
event: spare.action
data: {"type":"show_card","title":"购物","rows":["牛奶","鸡蛋"],"icon":"check"}

```

标准 OpenAI 客户端会忽略未知事件名，所以同一个 Agent 在别处仍可使用。动作集合刻意很小，不能控制设备：只改变对话显示什么、何时结束。未知或格式错误的动作会被忽略。

| `type` | 字段 | 效果 |
|--------|------|------|
| `show_card` | `title`（必填，建议不超过 10 个字符），可选 `value`、`unit`、`subtitle`、`status`、`rows`（最多 3 个字符串）、`icon`（`info`、`check`、`clock`、`pin`、`flag`、`car`、`nav`、`alert`、`spark`）、`progress`（0 到 100） | 立即在当前对话卡片上显示结构化卡片。同一句要朗读的话仍照常放在 `content` 里。 |
| `end_conversation` | 无 | 本次回答完成后关闭对话：麦克风不再为追问重新打开。不发送则麦克风保持开启等待追问（20 到 40 秒，长回答更久）。 |
| `keep_listening` | `seconds`（整数，限制在 5..120，默认 30） | 本次回答后，按此时长而不是默认值保持追问窗口。你提出问题并预计用户回答较慢时使用。 |

同一类型的动作在一轮内发多次，后者生效（卡片：按顺序逐个显示）。

### 4.2 错误

- 流开始前 HTTP 状态非 2xx：`401`/`403` 提示为令牌问题；`404`/`405` 提示地址错误；`429`、`408`、`5xx` 提示“Agent 繁忙或出错（状态码）”；其他 `4xx` 提示“Agent 出错”。JSON body 可选：`{"error":{"message":"...","type":"..."}}`（消息会被显示，截短到约 40 个字符）。
- 流开始后出错：发送 `data: {"error":{"message":"...","type":"..."}}` 或使用相同 JSON 的 `event: error`，然后关闭。已经流出的文字保留在镜片上，错误消息替换之。
- 网络失败或无响应：显示“连不上你的 Agent”或“Agent 响应太久”。不会自动重试。

以上情况都会在眼镜上给出简短可读的提示，数秒后对话关闭。眼镜不会无限停留在思考状态。

## 5. 握手

`GET {B}/v1/spare/agent`（设置了令牌时带 `Authorization`）。用于 App 的**测试连接**按钮，以及对话开始时的连接预热（预热时快速返回 `401`/`404` 也没问题）。返回 `200` 和 JSON：

```json
{
  "protocol": 1,
  "name": "My Codex bridge",
  "model": "codex",
  "capabilities": ["streaming", "session-memory", "actions"]
}
```

| 字段 | 含义 |
|------|------|
| `protocol` | 必填，整数。本文档对应 `1`。其他数字会向用户报告为不支持。 |
| `name` | 可选显示名（在设置中显示，40 个字符）。 |
| `model` | 可选，仅供参考的字符串。 |
| `capabilities` | 可选数组。`streaming`（v1 始终为 true）、`actions`（Agent 可能发出 4.1 的事件）、`session-memory`（只发新的这一句，见第 3 节）。未知字符串被忽略。 |

握手返回 `404`/`405` 时，服务器按普通 OpenAI 兼容服务器处理：使用历史模式，没有动作。`401`/`403` 表示令牌错误。握手结果记住上一次成功的测试；用户修改地址后，在下次测试前会被忘记。

## 6. 会话与记忆

- 每段对话一个 `X-Spare-Session`：唤醒词或长按开始（或延续）它；追问沿用它；**5 分钟没有任何请求**后，下一个请求带新 id。用户关闭再打开 Agent 或修改地址时，该 id 同样会重置。
- 无状态 Agent 只用 `messages`。有状态 Agent 以 `X-Spare-Session` 为键保存记忆并声明 `session-memory`；必须容忍某个会话 id 再也不会出现，并应自行淘汰空闲会话。
- 手机不发送用户的位置、联系人、日历或其他数据。`spare.time` 和语言是仅有的上下文。Agent 知道的其他信息都来自它自己的工具。

## 7. 超时与延迟

体验目标是**用户说完后 1 秒内出现第一个可见文字**，并在第一个完整句子时就开始朗读。

- 立即返回响应头并刷出第一批 SSE 字节，不要缓冲整个流。关闭代理缓冲（如 nginx `proxy_buffering off`、`X-Accel-Buffering: no`）。
- 按句子或更小的粒度流式输出。Agent 需要用工具时先给一句简短的确认（“我查一下日历。”），再给真正的答案。不要输出冗长的开场白；屏幕大约容纳 40 个字符的有效文字。除非用户要求详细，否则 1 到 2 句、最多约 80 字。
- Agent 工作但没有文字输出时，至少每 10 秒发一行 SSE 注释（`: ping`）。连接在**60 秒没有任何字节**后超时；如果用户说完后 **60 秒内完全没有回答文字**，对话以“没有回答”结束。
- 握手超时为 8 秒。
- 使用 HTTP keep-alive（尽量 HTTP/2）；对话开始时手机会预热到 Agent 的连接。
- v1 **不做**推测性请求：手机只在用户说完后才发请求，所以 Agent 不会看到说到一半或被取消的话。如果 Agent 还在工作时用户又开口说话，手机通过关闭连接来取消请求：客户端断开时请尽快停止工作。

## 8. 安全与隐私摘要

- 认证是用户自选的单个 bearer 令牌。可信局域网之外请使用 HTTPS。
- 手机发送：说出的文字、近期轮次（历史模式）、随机的每对话 id、匿名的每安装 id、语言和时间。不发送账号、手机号、位置或联系人数据。
- Agent 只能：说文字、显示卡片、结束对话，或让麦克风保持开启。不能启动导航、更改设置、安装任何东西，也不能触碰眼镜系统。
- 令牌不会写入日志；App 的请求跟踪只含主机名和耗时。

## 9. 最小服务器（参考，Python）

功能完整、带测试的参考桥（支持 Codex、Claude Code、OpenAI 兼容端点和测试后端）见 [`agent-bridge/`](../../agent-bridge/)。下面的最简版本只展示传输格式。

```python
# pip install fastapi uvicorn   ->   uvicorn agent:app --host 0.0.0.0 --port 8787
import json
from fastapi import FastAPI, Request
from fastapi.responses import StreamingResponse

app = FastAPI()

@app.get("/v1/spare/agent")
def hello():
    return {"protocol": 1, "name": "Echo agent", "capabilities": ["streaming", "actions"]}

@app.post("/v1/chat/completions")
async def chat(req: Request):
    body = await req.json()
    text = body["messages"][-1]["content"]

    def chunk(delta, finish=None):
        return "data: " + json.dumps({"choices": [{"index": 0, "delta": delta, "finish_reason": finish}]}, ensure_ascii=False) + "\n\n"

    def stream():
        yield chunk({"content": "你说："})
        yield chunk({"content": text + "。"})
        yield 'event: spare.action\ndata: {"type":"show_card","title":"Echo","rows":["' + text[:20].replace('"', "") + '"]}\n\n'
        yield chunk({}, "stop")
        yield "data: [DONE]\n\n"

    return StreamingResponse(stream(), media_type="text/event-stream", headers={"X-Accel-Buffering": "no"})
```

## 10. 版本

`protocol` 是整数。旧版 App 可以忽略的新增内容（新的可选字段、新的动作类型、新的 capability 字符串）不改变它。破坏性变更会使它加一，Agent 声明了 App 不支持的版本时，App 会告知用户。

## 11. 可选扩展（提案）：自定义语音识别

以上各节都假设识别由手机自己完成。由于 Agent 只收到**文字**、永远收不到音频，识别错了
在服务端无法修复；而眼镜上恰恰是识别这一环最弱：麦克风是远场，音频经蓝牙传输，交互又建立
在短句上——短句留给声学模型的上下文最少。两个音节的中文指令是典型失败场景（「快答」→
「块答」，「克劳德」→「克劳得」）。

本节是**提案**，不是已实现的行为。它增加一条可选的、默认关闭的路径，不改变 protocol `1`、
不改 §2 认证，也不影响 §3–§6 与任何现有服务器。

### 11.1 设置

在 设置 → 开发者功能 → 接入自己的 Agent 中，紧邻 地址 与 令牌：

| 字段 | 取值 |
|---|---|
| 语音识别 | `系统`（默认） / `自定义端点` |
| 识别端点 `{ASR_BASE}` | `https://…`；选「自定义端点」时必填 |
| 识别令牌 | 可选；留空表示复用 Agent 令牌 |

### 11.2 请求

`POST {ASR_BASE}/v1/asr`，一次请求一句话。

| 项 | 值 |
|---|---|
| body | 原始音频，单声道，16 kHz，≤ 15 秒 |
| `Content-Type` | `audio/wav; rate=16000; channels=1; bits=16`（或 `audio/opus`） |
| `Authorization` | 配置了令牌时带 `Bearer <token>` |

### 11.3 响应

- `200 {"text": "…"}` —— UTF-8。文本为空或只有空白，表示「没识别出内容」。
- 非 2xx、超时（建议 3 秒）或 body 无法解析：手机回落使用自己的识别器，因此这条扩展
  不会让助手变得不可用。
- 请求不重试，与 §3 一致。

### 11.4 隐私

只有在显式打开「自定义端点」时音频才离开手机，App 会在打开的那一刻给出一行提示。端点必须
是 `https://` 且应当要求令牌，见 §8。
