# Spare 创意工坊开发者套件

给 Spare 智能眼镜（540×220 单色绿屏）写 SpareScript 应用、发布到 Spare 创意工坊。

| 文件 | 内容 |
|---|---|
| [`guide.md`](guide.md) | 开发者指南（和 <https://spare.cool/dev/docs> 同一份） |
| [`AGENTS.md`](AGENTS.md) | 给 AI 编程助手看的速查：屏幕、配额、SpareScript 语法、手机控制面板、托管后端 |
| [`PROTOCOL-apps.md`](PROTOCOL-apps.md) | 应用包格式、SpareScript、手机与眼镜之间的线协议（runtime 1） |
| [`template/`](template/) | 新应用起步模板（`app.json`、`main.ss`、冒烟场景） |
| [`examples/pomodoro/`](examples/pomodoro/) | 番茄钟示例，带手机控制面板（`controls`）和模拟器场景 |

## 怎么开始

1. 在 <https://spare.cool/dev/> 用 Spare 账号登录，就是开发者。
2. 把 `spare-workshop` MCP 接到你的 AI 编程助手，让它 `create_app`：会给你一个带模板的私有 git 仓库，里面有打包工具 `tools/spac.py`。
3. 改 `app.json` / `main.ss`，`simulate` 看眼镜画面，`install_on_my_glasses` 在自己眼镜上试，`submit_for_review` 发布。

手机控制面板（`controls`）：iOS 版 Spare App 已支持，Android / 鸿蒙版跟进中。

打包工具 `spac.py`、眼镜运行时和模拟器不在本仓库中，通过创意工坊平台提供。

---

**English:** Developer kit for writing SpareScript apps for Spare smart glasses and publishing them to the Spare Workshop: developer guide, app package and wire format, a starter template and an example app. The docs are in Chinese.
