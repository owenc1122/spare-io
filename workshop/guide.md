# Spare 眼镜应用开发者指南

给 Spare 智能眼镜写小应用：装到眼镜上就能用，不用刷机。界面、计时、动画都在眼镜本地运行，手机只帮你取声明过的数据。

任何 Spare 账号登录后都可以发布应用，不需要申请。

## 快速开始

1. **登录**：在 Spare App「创意工坊」右上角点 **+**，或在电脑上打开 [spare.cool/dev](https://spare.cool/dev/)，用 Spare 账号登录。第一次登录会自动成为开发者，handle 取自你的昵称（建应用前可以改），应用 id 是 `dev.<handle>.<名字>`。
2. **复制给你的 AI**：点「让 AI 帮我做」→「复制给你的 AI」，粘给 Codex、Claude Code、Cursor 或 ChatGPT。它会装上你的专属开发者 MCP（一个地址，当密码对待）。
3. **说出你的想法**：例如「给眼镜做一个番茄钟并发布到 Spare 创意工坊」。AI 会建仓库、读模板里的规则、写代码。
4. **模拟器预览**：AI 调用 `simulate`，拿回眼镜画面截图，自己检查文字有没有被裁。
5. **装到自己的眼镜**：AI 调用 `install_on_my_glasses`，你在 App「创意工坊 › 开发者」里点「安装」（需要打开开发者模式）。
6. **提交审核**：你说「可以发布了」，AI 调用 `submit_for_review`。自动检查和 AI 初审几十秒内完成，然后由 Spare 人工终审。
7. **上架**：通过后所有用户都能在创意工坊看到并安装。

也可以不用 AI：网页控制台新建应用、用 git 或 `spare` 命令行推代码，或在 App 里「上传应用包 (.spa)」。

## 应用结构

```
my-app/
├── app.json      清单 + 界面（必需）
├── main.ss       SpareScript 脚本（必需）
├── icon.png      44×44 灰度图标（列表里 2×3 格子用）
├── assets/       可选图片：PNG，宽为偶数，≤ 128×128
└── scenarios/    模拟器场景（每次云端构建都会跑）
```

### app.json

| 字段 | 说明 |
|---|---|
| `id` | 平台分配的 `dev.<handle>.<slug>`，不要改 |
| `name` | 列表里的名字，≤ 23 字节（约 7 个汉字） |
| `version_name` | 展示版本，semver，如 `1.0.0` |
| `version` | 不用管：平台按构建号自动写（眼镜要求每次更新变大） |
| `data_version` | 持久数据格式版本；变了，更新时眼镜清掉旧数据 |
| `description` | 商店说明 |
| `permissions` | 权限白名单，见下 |
| `requests` | 让手机代取的数据，见「手机数据桥」 |
| `ui` | 界面元素数组 |

### 界面元素

最多 48 个。公共字段：`id`（脚本里直接用这个名字）、`type`、`x` `y` `w` `h`、`hidden`。

| 类型 | 字段 |
|---|---|
| `text` | `size` 12–96、`bold`、`align`（left / center / right）、`text`（≤ 63 字节）、`gray` |
| `rect` | `radius`、`gray`（填充）、`border`（> 0 只描边）、`children`（坐标相对它） |
| `bar` | 进度条：`value` 0–100、`gray`、`track`（底色，默认 55）、`radius` |
| `image` | `src` = `assets/` 里的文件名；不写 `src` 是空框，`image(el, -1)` 放手机传来的图 |

### main.ss

SpareScript 写法像 JavaScript，只有 **32 位整数** 和 **短字符串**（≤ 63 字节）两种值。

```js
var left = 1500;                 // 全局变量（≤ 64 个）
persist var done = 0;            // 持久变量：关掉、重启后还在；改完 save()
fn fmt(s) { return pad(s / 60, 2) + ":" + pad(s % 60, 2); }

on open()    { text(time, fmt(left)); }
on crown(d)  { left = clamp(left + d * 60, 60, 3600); text(time, fmt(left)); }
on click()   { timer(0, 1000, 1); }
on timer(id) { left -= 1; text(time, fmt(left)); }
```

事件：`open` `close` `crown(d)` `click` `timer(id)` `data(seq)`。语句：`var`、赋值、`if/else`、`while`、`break`、`continue`、`return`。

| 内建 | 作用 |
|---|---|
| `text` `show` `gray` `move` `size` `value` `image` | 改界面 |
| `anim_x` `anim_opa` `anim_scale` | 本地动画（≤ 2000 ms） |
| `timer(id, ms, repeat)` | 计时器 0–3，≥ 100 ms；ms = 0 停止 |
| `now()` `clock()` `seconds()` | 毫秒计时 / 当天分钟数 / 当前秒 |
| `str` `num` `len` `sub` `pad` | 字符串 |
| `min` `max` `clamp` `abs` `rand` | 数学 |
| `data` `request` `connected` | 手机数据桥 |
| `battery` `save` `exit` | 电量 / 写盘 / 退出 |

### 权限白名单

| 权限 | 用途 | 审核 |
|---|---|---|
| `network:<域名>` | `http_json` 请求的主机；必须是公网域名，URL 必须 https | 人工确认主机 |
| `location` | `location` / `weather` 请求 | 必须人工审 |
| `notifications` | `notify_count` 请求 | — |
| `microphone` `spare_notes` | 眼镜笔记相关 | 必须人工审 |

只申请真正用到的权限。

## 眼镜画面与交互规范

- **屏幕**：540 × 220，单色，亮度 0–255 显示为绿色。**重要内容放在 y ≤ 195**（最下面约 10 px 会被裁）。
- **图标**：44×44 灰度，出现在眼镜应用列表的 2×3 格子里，用简单、粗线条的形状。
- **手势**：转表冠 = `crown(+1 / -1)`，单击 = `click`。**双击永远是退出应用，长按永远是 AI**，应用收不到。
- **一屏一件事**：一个大字（64–96 px）+ 一行提示（20–24 px，亮度 100–160）。亮度对比就是层次。
- **对用户安全**：用户在走路、说话，不要闪烁，不要需要盯着看的小字。
- **配额**：每次事件 ≤ 20,000 条指令、≤ 256 次界面调用；包 ≤ 32 KB；眼镜最多装 8 个应用。超配额会显示「应用出错」。
- **只在打开时运行**：关掉后不占电。

## 手机数据桥

眼镜不联网。应用在 `requests` 里逐条声明数据，眼镜只说「第几个请求 + ≤ 10 字节参数」，URL 永远来自 `app.json`。

```json
"permissions": ["network:api.example.com"],
"requests": [{ "name": "quote", "kind": "http_json",
  "url": "https://api.example.com/q?s={arg}", "fields": { "px": "price" } }]
```

```js
on open()    { request(quote, "AAPL"); }
on data(seq) {
  if (data("_err") != "") { text(px, "--"); return; }
  text(px, data("px"));
}
```

| `kind` | 返回 | 权限 |
|---|---|---|
| `http_json` | `fields` 映射的键 | `network:<主机>` |
| `location` | `lat` `lon` `city` | `location` |
| `weather` | `temp` `text` `icon` | `location` |
| `time_sync` | `tz` | — |
| `notify_count` | `count` | `notifications` |

失败时只回一个键 `_err`（`bad` / `kind` / `perm` / `auth` / `net`）。手机限频 ≤ 1 次/秒。离线时 `data()` 是上次的缓存，`connected()` 为假，记得显示「等待连接」。

## 手机网页、设置页与托管后端

眼镜应用自己不能联网，也收不到手机网页里的数据，能走的只有「眼镜 → 手机代发 GET → JSON」。平台给每个应用自动开通了托管后端，
都在 `https://apps.spare.cool/<应用 id>/` 下（AI 调 MCP `app_backend` 拿完整地址和用量），不用自己搭服务器：

| 地址 | 方法 | 作用 |
|---|---|---|
| `/<id>/` | GET | 你的手机网页：仓库 `web/` 目录，MCP `publish_page` 发布（≤ 2 MB、≤ 100 个静态文件、要有 `index.html`、不能有密码框） |
| `/<id>/relay/<配对码>` | PUT / PATCH / GET / DELETE | 配对码中转：存一份 JSON（≤ 512 KB，12 小时没有再写就过期） |
| `/<id>/relay/<配对码>/now` | GET | 时间轴：按播放进度返回当前条目（字幕、提词、幻灯片） |
| `/<id>/relay/<配对码>/control` | POST | 播放控制：`play` / `pause` / `seek` / `nudge` |
| `/<id>/kv/<键>` | GET | 只读应用数据：开发者用 MCP `set_app_data` 写（每日一句、题库、配置） |

只用 `apps.spare.cool` 的应用，`permissions` 写 `network:apps.spare.cool`，审核不需要人工确认主机。

**手机上的设置**：开关、滑块、步进、按钮、选项、短文字这类，在 `app.json` 写 `controls`，Spare App 里会出现原生控制面板（iOS 版已支持，Android / 鸿蒙版跟进中）。
要所有手机都能用，或者设置比控件复杂，用托管网页做设置页（下面有完整示例）。

### 完整示例：手机设置页

用户想在手机上改设置（时长、开关、选项、标题、清零……），**现在的做法是托管网页**：网页把设置写到平台的配对码中转，眼镜每几秒读一次。
不用自己搭服务器，所有已发布的 Spare App（iOS / Android / 鸿蒙）都能用。

1. 眼镜生成一个**设置码**（8 位），显示在屏幕上，用 `persist var` 记住。
2. 用户用手机浏览器打开 `https://apps.spare.cool/<应用 id>/`，输入设置码，改设置，点保存：网页 `PUT relay/<设置码>`。
3. 眼镜用 `http_json` 读 `relay/<设置码>`（打开时一次，之后每 5 秒），读到新的就写进 `persist var`。

**设置一定要用 `persist var` 存在眼镜上**：服务器上的那份 **12 小时没有再保存就过期**，眼镜没连手机时也读不到；这些情况眼镜收到 `_err`，沿用上次存下的值就行。
网页用 `localStorage` 记住设置码，并在打开时读回当前设置（过期了就显示默认值，用户再保存一次即可）。

`app.json`（把 `dev.owen.focus` 换成你的应用 id）：

```json
"permissions": ["network:apps.spare.cool"],
"requests": [{ "name": "cfg", "kind": "http_json", "url": "https://apps.spare.cool/dev.owen.focus/relay/{arg}",
               "fields": { "min": "minutes", "snd": "sound", "ttl": "title", "rev": "rev" } }],
"ui": [
  { "id": "title", "type": "text", "x": 0, "y": 20,  "w": 540, "h": 36, "size": 28, "align": "center", "gray": 160, "text": "专注" },
  { "id": "time",  "type": "text", "x": 0, "y": 60,  "w": 540, "h": 96, "size": 88, "bold": true, "align": "center", "text": "25:00" },
  { "id": "hint",  "type": "text", "x": 0, "y": 160, "w": 540, "h": 30, "size": 22, "align": "center", "gray": 120, "text": "" }
]
```

`main.ss`：

```js
persist var code = "";        // 设置码：眼镜生成，用户在手机网页里输入
persist var minutes = 25;     // 设置用 persist 记在眼镜上：服务器那份过期了、没连手机时照样能用
persist var sound = 1;
persist var label = "专注";
persist var rev = "";         // 网页每次保存写一个新的 rev；没变就不重画、不写盘（save() 最多 10 秒一次）
var left = 1500;

fn gen() { var a = "ABCDEFGHJKMNPQRSTUVWXYZ23456789"; var s = ""; var i = 0;
           while (i < 8) { s = s + sub(a, rand(31), 1); i += 1; } return s; }
fn render() { text(title, label); text(time, pad(left / 60, 2) + ":" + pad(left % 60, 2));
              text(hint, "手机打开设置页，输入 " + code); }

on open()    { if (code == "") { code = gen(); save(); } left = minutes * 60; render();
               request(cfg, code); timer(1, 5000, 1); }          // 打开时取一次，之后每 5 秒问一次
on timer(id) { if (id == 1) { request(cfg, code); } }
on data(seq) {
  if (data("_err") != "") { return; }                            // 还没设置 / 已过期 / 没网：沿用眼镜上存的
  if (data("rev") == rev) { return; }
  rev = data("rev");
  minutes = clamp(num(data("min")), 1, 120);
  sound = num(data("snd")) != 0;
  if (data("ttl") != "") { label = data("ttl"); }
  left = minutes * 60; save(); render();
}
```

`web/index.html`（和接口同源，直接用相对地址 `relay/…`）：

```html
<!doctype html>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>专注计时 · 设置</title>
<style>body{font:17px system-ui;max-width:420px;margin:24px auto;padding:0 16px}label{display:block;margin:14px 0}
input:not([type=checkbox]){width:100%;font:inherit;padding:8px;box-sizing:border-box}button{font:inherit;padding:10px 18px}</style>
<label>设置码（眼镜上显示的 8 位）<input id="code" autocapitalize="characters" autocomplete="off"></label>
<label>专注时长（分钟）<input id="minutes" type="number" min="1" max="120" value="25"></label>
<label><input id="sound" type="checkbox" checked> 提示音</label>
<label>标题<input id="title" maxlength="20" value="专注"></label>
<button id="save">保存到眼镜</button>
<p id="msg"></p>
<script>
const $ = id => document.getElementById(id);
const KEY = 'dev.owen.focus.code';               // 所有应用的网页同一个域名：键名带上应用 id
const code = () => $('code').value.trim().toUpperCase();
try { $('code').value = localStorage.getItem(KEY) || ''; } catch (e) {}
async function load() {
  if (!code()) return;
  const r = await fetch(`relay/${code()}`);
  if (!r.ok) { $('msg').textContent = '还没有保存过设置（或已超过 12 小时），改好后点保存'; return; }
  const d = await r.json();
  $('minutes').value = d.minutes ?? 25; $('sound').checked = d.sound !== 0; $('title').value = d.title ?? '';
}
$('code').onchange = load; load();
$('save').onclick = async () => {
  try { localStorage.setItem(KEY, code()); } catch (e) {}
  const body = { minutes: +$('minutes').value, sound: $('sound').checked ? 1 : 0, title: $('title').value,
                 rev: String(Date.now()) };
  const r = await fetch(`relay/${code()}`, { method: 'PUT', headers: { 'Content-Type': 'application/json' },
                                              body: JSON.stringify(body) });
  $('msg').textContent = r.ok ? '已保存，眼镜几秒内更新（眼镜上要开着这个应用）'
                              : '保存失败：' + ((await r.json()).error?.message || r.status);
};
</script>
```

要点：
- 设置码就是钥匙，知道码的人能改设置；8–10 位、去掉易混字符（0/O、1/I/L）。设置码只能是 6–10 位字母数字。
- 每个值 ≤ 63 字节（约 21 个汉字）；眼镜参数 ≤ 10 字节（所以设置码最多 10 位）；眼镜请求 ≤ 1 次/秒——轮询 5 秒一次足够。
- 「清零」这类一次性动作：网页写一个新的计数（如 `"reset": 时间戳`），眼镜发现它变了就执行一次。
- 模拟器里测：场景用 `reply min=40 snd=0 ttl=读书 rev=1` 假装读到了设置，`reply _err=net` 假装过期 / 没网。
- 发布：提交 `web/` → MCP `publish_page` → 手机浏览器打开 `https://apps.spare.cool/<应用 id>/` 试 → 眼镜端照常 `build` / `install_on_my_glasses`。

## 调试与模拟器

- **本地编译**：`python3 tools/spac.py build . -o /tmp/app.spa`，2 秒内报编译错误。
- **云端模拟器**：MCP `simulate` 或 `spare sim`，用眼镜上同一份运行时跑场景，返回 1080×440 截图（2 倍）。
- **场景文件** `scenarios/*.txt`，每行一条：`open` `close` `crown +1` `crown -1` `click` `tick <毫秒>` `connect 0|1` `clock <分钟>` `reply k=v ...` `shot <名字>` `record <名字> <毫秒>`。
- **云端构建**（每次 `git push` 自动跑）：编译 → 清单与权限检查 → 大小与界面限制 → 眼镜字节码验证 → 你的场景 + 平台压力场景（连按、表冠 ±50、断开重连、加速 30 分钟、每个请求回 `_err`）。**0 个 FAULT 才算通过。**
- **真机**：`install_on_my_glasses` 后在 App「创意工坊 › 开发者」安装；开发者模式只影响安装没审核过的包，一个开关。

## 审核规则

上架前都要经过：自动检查 → AI 初审 → Spare 人工终审（通常 1 个工作日）。被打回的意见会写进你的仓库 issue，AI 用 `get_review_feedback` 就能读到。

常见拒审原因：

- 名字或描述与实际功能不符。
- 申请了用不到的权限，或网络主机不明。
- 冒充 Spare 官方或系统界面（假的系统提示、索要密码或验证码）。
- 违法违规内容（赌博、色情、毒品、诈骗、侵权等），在中国大陆和其他地区都不允许。
- 闪烁、诱导用户走路时盯着屏幕。
- 文字被裁（超出 y 195）或离线时一片空白。

用户可以举报应用；违规应用会被下架。

## 版本与渠道

| 渠道 | 谁能看到 | 签名 |
|---|---|---|
| `dev` | 只有你自己的手机（开发者区） | 平台开发者密钥 |
| `beta` | 测试者 | 创意工坊密钥（审核通过后） |
| `stable` | 所有人 | 创意工坊密钥（审核通过后） |

- **版本号**：平台给每次构建分配递增的构建号，你只写 `version_name`。
- **晋升**：`promote` dev → beta → stable；beta → stable 用同一个已签包，不重审。
- **回滚**：`rollback` 用旧版本的源码以新构建号重新签名（眼镜要求版本号变大），不重审。如果 `data_version` 变过，用户的持久数据会被清掉。
- **自己下架**：对 AI 说「帮我下架」，它调 `unpublish_app`，应用马上从商店消失；已经装在眼镜上的不删，托管后端照常可用，仓库和构建都保留。
  改好了说「重新上架」，AI 调 `relist_app` 用 stable 当前的构建原样放回（不重审）；被平台下架的不能自己恢复。

## 完整示例：喝水提醒

表冠调目标杯数，单击记一杯，进度条显示完成度，数字关掉也记得；完成目标后再单击从头开始。

`app.json`：

```json
{
 "id": "dev.yourname.water",
 "name": "喝水",
 "version_name": "1.0.0",
 "version": 1,
 "data_version": 1,
 "description": "单击记一杯水，转表冠调每天目标；完成后再单击从头开始。",
 "permissions": [],
 "requests": [],
 "ui": [
  { "id": "big", "type": "text", "x": 0, "y": 30, "w": 540, "h": 100, "size": 88, "bold": true, "align": "center", "text": "0/8" },
  { "id": "bar", "type": "bar", "x": 70, "y": 140, "w": 400, "h": 10, "radius": 5, "value": 0 },
  { "id": "hint", "type": "text", "x": 0, "y": 160, "w": 540, "h": 30, "size": 22, "align": "center", "gray": 130, "text": "单击记一杯 · 转表冠调目标" }
 ]
}
```

`main.ss`：

```js
persist var cups = 0;
persist var goal = 8;

fn draw() {
  text(big, str(cups) + "/" + str(goal));
  value(bar, min(100, cups * 100 / goal));
  if (cups >= goal) { text(hint, "完成了！单击从头开始"); } else { text(hint, "单击记一杯 · 转表冠调目标"); }
}

on open()   { draw(); }
on click()  {
  if (cups >= goal) { cups = 0; } else { cups += 1; anim_opa(big, 80, 255, 300); }
  save();
  draw();
}
on crown(d) { goal = clamp(goal + d, 1, 20); save(); draw(); }
```

`scenarios/smoke.txt`：

```
open
shot open
click
click
tick 400
shot two
crown +1
shot goal
```

## 常见问题

**要申请才能发布吗？** 不用。登录 Spare 账号就是开发者。

**handle 能改吗？** 建第一个应用之前可以（网页控制台或 App 开发者区），之后就固定了，因为应用 id 和仓库地址里用到它。

**AI 说调不到 MCP？** Claude Code 装好后要新开一个会话（或 `/mcp` 重连）。地址失效了就再「复制给你的 AI」一次，旧地址会作废。

**为什么我的应用在眼镜上「应用出错」？** 超了每次事件的指令数或界面调用数。检查循环，不要在一次事件里改太多元素。

**能在 Spare App 里给我的应用做原生设置页吗？** 能：在 `app.json` 写 `controls`（iOS 版 Spare App 已支持，Android / 鸿蒙版跟进中）。更复杂的设置页用托管网页（见「手机网页、设置页与托管后端」）。

**能收费吗？** 暂时都免费。

**有数量限制吗？** 每个账号最多 30 个应用，每天新建 5 个、提交审核 10 次，每小时构建 60 次。
