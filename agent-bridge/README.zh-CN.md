# Spare agent bridge（Agent 参考桥）

[English](README.md)

[Spare Agent 协议 v1](../docs/agent/PROTOCOL.zh-CN.md) 的小型参考服务器。让**你自己的 Agent** 接管运行 Spare io 的雷鸟 iO 眼镜语音助手：多轮对话、逐句流式输出、可选眼镜卡片。

```
眼镜 -> 手机（语音转文字） -> 本桥 -> OpenAI 兼容端点 | Codex CLI | Claude Code | echo
眼镜 <- 手机（镜片 + 语音） <- SSE 句子 + spare.action 事件
```

## 5 分钟上手

需要 Python 3.10+。

```sh
cd agent-bridge
python3 -m venv .venv && . .venv/bin/activate
pip install -r requirements.txt

# 1) 不接任何 AI，先冒烟测试
python -m spare_bridge --backend echo
```

首次启动会**只打印一次访问令牌**（同时保存到 `~/.spare-agent-bridge/token`，权限 600），并列出要填进 App 的地址，如 `http://192.168.x.y:8787`。也可以用 `SPARE_BRIDGE_TOKEN=...` 自己指定。

换成真正的后端（更多选项把 `config.example.toml` 复制为 `config.toml`）：

| 后端 | 运行 | 说明 |
|---|---|---|
| Ollama / LM Studio / vLLM | `SPARE_OPENAI_MODEL=qwen2.5:7b python -m spare_bridge --backend openai` | 默认 `http://127.0.0.1:11434/v1`，可用 `SPARE_OPENAI_BASE_URL` 修改 |
| OpenAI / DeepSeek / 任何兼容 API | `SPARE_OPENAI_BASE_URL=https://api.openai.com/v1 SPARE_OPENAI_API_KEY=... SPARE_OPENAI_MODEL=... python -m spare_bridge --backend openai` | 逐 token 流式，最快 |
| OpenClaw | 先按 OpenClaw 文档打开它的 OpenAI 兼容 Gateway 端点，再用 `openai` 后端，填它的地址，gateway 令牌作为 `SPARE_OPENAI_API_KEY`，模型/Agent 名按它要求填写 | 本桥在前面加上眼镜风格提示和会话记忆 |
| Codex CLI | `python -m spare_bridge --backend codex` | 运行 `codex exec --json`，追问用 `codex exec resume <thread>`。消息整条到达，不是逐 token |
| Claude Code | `python -m spare_bridge --backend claude` | 运行 `claude -p --output-format stream-json --include-partial-messages`，追问用 `--resume`，逐段流式 |

CLI 后端会在**你的**电脑上运行编码 Agent。请把 `workdir` 指向一个空的临时目录，并保守设置沙箱/权限（`config.toml` 里的 `extra_args`，如 Codex 的 `--sandbox read-only`）。你允许它做什么，一句话就能触发什么。

## 在 App 里连接

1. 手机和电脑连同一个 Wi-Fi（或使用下面的 HTTPS 隧道）。
2. Spare io App：**设置 -> 开发者功能 -> 接入自己的 Agent**。
3. 填 **地址**（`http://<电脑局域网 IP>:8787`）和 **令牌**（启动时打印的那串）。
4. 点 **测试连接**，应显示你的桥名称。
5. 打开 **Agent 接管 AI 助手**。唤醒助手开始说话；监听窗口内的追问属于同一个会话。

App 只允许对局域网内的 IP 地址使用明文 `http://`（iOS 会请求一次本地网络权限）。使用**主机名**必须是 `https://`。

### 不在家：HTTPS

- **Tailscale**：`tailscale serve --bg 8787`（或 `tailscale funnel`）得到 `https://<机器>.<tailnet>.ts.net`；桥用 `--host 127.0.0.1` 只监听本机。
- **Cloudflare Tunnel**：快速测试 `cloudflared tunnel --url http://127.0.0.1:8787`，长期使用请建命名隧道获得固定域名。
- 经过 nginx/Caddy 时关闭响应缓冲（`proxy_buffering off`），否则句子会攒成一坨才到。

## 功能

- `GET /v1/spare/agent` 握手：protocol 1，能力 `streaming`、`session-memory`、`actions`。
- `POST /v1/chat/completions` SSE：立刻刷出响应头和 `: ok`；后端沉默时每 10 秒 `: ping`；手机断开后立即取消工作（会杀掉 CLI 进程）。
- 按 `X-Spare-Session` 管理会话（历史、Codex thread id、Claude session id），空闲 15 分钟清理。
- Bearer 令牌认证（常量时间比较）。`--no-auth` 仅用于一次性局域网测试。
- 眼镜系统提示：只回答本轮、40 到 80 个汉字（约 3 行）、纯文本、语言取自 `X-Spare-Locale`。人设可通过 `extra_prompt` 追加。
- 动作：模型可在回答里写 `[[card:标题|行1|行2|icon=check]]`、`[[end]]`、`[[listen:30]]`；桥会把它们从朗读文字中去掉并发出 `spare.action` 事件。
- 逐句切分：第一块可在第一个逗号处结束，更早开口。`chunking = "token"` 则原样转发 token。

## 降低延迟

- 首句低于 1 秒需要流式后端和快模型：小型本地模型、`gpt-4.1-mini` 一档、DeepSeek chat 或 Claude Haiku。推理模型在第一个字之前会静默思考，请调低推理强度（`extra_body = { reasoning_effort = "low" }`，Codex 用 `-c model_reasoning_effort=low`）。
- `max_tokens` 设在 200 左右，回答要短；镜片只显示约 40 个有效字符。
- 桥尽量靠近模型（和 Ollama 同机），避开会缓冲的代理。
- Codex 整条消息返回，所以首句要等模型写完第一条消息才出现。追求速度时优先 `openai` 后端。

## 安全说明

- 令牌是唯一的保护。请保密，离开家庭网络要用 HTTPS，没有令牌不要把端口开放到公网。
- 桥不记录令牌，也不记录你说的话；请求日志只有方法、路径和状态码。
- CLI 后端能在你电脑上做的事，一句话都能触发。请使用沙箱和临时工作目录。
- 手机只发送：说出的文字、近期轮次、随机的每对话 id、匿名的每安装 id、语言和时间。

## 测试

```sh
pip install -r requirements-dev.txt
python -m pytest
```
