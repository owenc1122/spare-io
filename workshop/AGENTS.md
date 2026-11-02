# Spare 眼镜应用（给 AI 编程助手看的说明）

这个仓库是一个 **Spare 智能眼镜应用**：`app.json`（清单 + 界面）+ `main.ss`（SpareScript）+ 可选 `icon.png` / `assets/`。
它被编译成 `.spa`，经手机装到眼镜上，**在眼镜本地运行**，不用刷机。平台：Spare 创意工坊开发者平台（MCP 名 `spare-workshop`）。
完整的开发者指南（与本文件同步）：https://spare.cool/dev/docs

## 必须遵守

- 屏幕 **540 × 220**，单色（亮度 0–255，显示为绿色），原点左上。**重要内容放在 y ≤ 195**（最下面约 10 px 会被裁）。
- 界面元素 ≤ **48** 个；每个字符串 ≤ **63 字节**（一个汉字 3 字节）；包 ≤ **32 KB**；图片宽为偶数、≤ 128×128；图标 44×44 灰度。
- 只有两种值：**32 位整数**和**短字符串**。没有浮点、数组、对象。
- 手势：转表冠 = `on crown(d)`（d = +1 / -1），单击 = `on click()`。**双击永远是退出，长按永远是 AI**，应用收不到。
- 每次事件 ≤ 20,000 条指令、≤ 256 次界面调用；不要在局部变量里循环拼长字符串。超配额会「应用出错」。
- 应用只在打开时运行；关掉后要记住的东西用 `persist var`，改完调 `save()`。
- 网络：只能经 `app.json` 的 `requests` 让手机代取，主机写进 `permissions`（`network:<公网域名>`，必须 https）。
- **手机上调设置 / 遥控**（开关、滑块、步进、按钮、选项、短文字、只读数值）：在 `app.json` 写 `controls`，Spare App 里这个应用会出现原生控制面板，
  见「手机控制面板（controls）」。目前 iOS 版 Spare App 支持，Android / 鸿蒙版跟进中；要所有手机都能用，或者设置比控件复杂，用托管网页做设置页（见「用托管网页做设置页」）。
- **需要手机网页或服务器**（手机设置页、手机上导入文件、手机遥控、配对码同步、按时间显示）：用平台托管后端 `apps.spare.cool`（见下面「手机网页与托管后端」），
  **不要自己搭服务器，也不要把网页放到 chatgpt.site / github.io 这类只能放静态页的地方**（那里没有眼镜能读的 JSON 接口，大陆也常打不开）。
- **`app.json` 里的 `id` 不要改**（平台分配的 `dev.<handle>.<slug>`）。**`version` 不用管**（平台按构建号自动写）；展示版本写 `version_name`（semver）。
- **永远不要把 git token、MCP 地址或任何密钥写进仓库**（包括 README、注释、提交说明）。

## 工作流程

1. 改 `app.json` / `main.ss`。
2. 本地快速检查编译错误：`python3 tools/spac.py build . -o /tmp/app.spa`（2 秒内；需要 Pillow 处理图标时 `pip install pillow`）。
3. 调 MCP `simulate`（参数 `app` = 应用 id；`scenario` 可以给场景全文，默认 `scenarios/smoke.txt`）→ **看返回的截图**，检查文字有没有被裁、是否清楚、是否在 y ≤ 195 内。
   注意：`simulate` 跑的是仓库里 **已推送** 的代码，先 `git push`（或用 `push_files`）再模拟。
4. 满意了 `git commit && git push`（自动云端构建 + 全部检查），或调 MCP `build` 看问题列表。
5. 想在真眼镜上试：MCP `install_on_my_glasses`，让用户在 Spare App「创意工坊 › 开发者」里点安装。
6. 用户说可以发布：MCP `submit_for_review(version_name="1.0.0", notes="…")`；之后 `get_review_status` / `get_review_feedback`。
7. 用户说要下架：MCP `unpublish_app(app=…)`，马上从商店消失（已装的不删、仓库构建都保留）；之后改好了 `relist_app` 原样恢复，或者提交新版本审核，通过后再 `relist_app`。

`scenarios/*.txt` 会在每次云端构建时跑（外加平台压力场景：连按、表冠 ±50、断开重连、加速 30 分钟、对每个请求回 `_err`），**0 个 FAULT 才算通过**。
场景命令：`open` `close` `crown +1` `crown -1` `click` `tick <毫秒>` `connect 0|1` `clock <当天分钟数>` `reply k=v ...`（回复最近一次请求）
`control <控件id> [值]`（用户在手机控制面板上改控件，按钮不带值；open 后手机自动补发全部控件值）`panel`（打印面板上现在的值）`shot <名字>` `record <名字> <毫秒>`。
有 `controls` 时，平台压力场景还会把每个控件拨到两头、按钮连按 3 次。

## MCP 工具一览（`spare-workshop`）

| 工具 | 作用 |
|---|---|
| `whoami` | 我的开发者身份、已建应用、配额；装好 MCP 后先调一次 |
| `create_app` | 新建应用（分配 `dev.<handle>.<slug>`，建私有 git 仓库，返回带 30 天 token 的地址） |
| `get_git_remote` | token 过期后重新拿 git 地址 |
| `push_files` | 没有 git 时直接提交文件（会触发云端构建） |
| `build` | 云端构建 + 全部检查，返回问题列表和构建号 |
| `simulate` | 模拟器跑场景，返回眼镜画面截图和 FAULT 日志 |
| `install_on_my_glasses` | 放进用户 Spare App「创意工坊 › 开发者」，让用户在真眼镜上装 |
| `submit_for_review` / `get_review_status` / `get_review_feedback` | 提交上架审核 / 看审核状态 / 看审核意见 |
| `list_releases` / `promote` / `rollback` | 频道（dev / beta / stable）与构建历史 / 晋升 / 回滚到审核通过过的构建 |
| `unpublish_app` / `relist_app` | 自己下架（马上从商店消失，已装的不删）/ 原样重新上架（不重审） |
| `app_backend` | 托管后端地址、要写进 app.json 的权限和请求示例、用量 |
| `publish_page` | 把仓库 `web/` 发布成 `https://apps.spare.cool/<应用 id>/`（手机网页、设置页） |
| `set_app_data` | 写只读应用数据 `kv/<键>`（每日一句、题库、配置） |

## app.json

```json
{
 "id": "dev.owen.example", "name": "示例", "version_name": "1.0.0", "version": 1, "data_version": 1,
 "description": "商店里显示的说明",
 "permissions": ["network:api.example.com"],
 "requests": [{ "name": "quote", "kind": "http_json", "url": "https://api.example.com/q?s={arg}", "fields": { "px": "price" } }],
 "ui": [
  { "id": "time", "type": "text", "x": 0, "y": 40, "w": 540, "h": 100, "size": 96, "bold": true, "align": "center", "text": "25:00" },
  { "id": "bar",  "type": "bar",  "x": 70, "y": 170, "w": 400, "h": 8, "radius": 4, "value": 0 }
 ]
}
```

- `name` ≤ 23 字节（约 7 个汉字）。`data_version` 变了，更新时眼镜会清掉旧的持久数据。
- 元素：`text`（`size` 12–96、`bold`、`align` left/center/right、`text`、`gray` 默认 255）、`rect`（`radius`、`gray` 填充、`border` > 0 只描边、`children` 坐标相对它）、
  `bar`（`value` 0–100、`gray`、`track` 底色默认 55、`radius`）、`image`（`src` = `assets/` 里的文件名；不写 `src` = 空框，用 `image(el, -1)` 放手机传来的图）。
  公共字段：`id`（脚本里直接用这个名字）、`x y w h`、`hidden`。
- `requests` 种类：`http_json`（GET，`{arg}` 换成 URL 编码参数，`fields` 把 JSON 路径 `a.b.0.c` 映射成数据键；需要 `network:<主机>`）、
  `location`（`lat` `lon` `city`，需要 `location`）、`weather`（`temp` `text` `icon`，需要 `location`）、`time_sync`（`tz`）、`notify_count`（`count`，需要 `notifications`）。
  失败时只回一个键 `_err`（`bad` / `kind` / `perm` / `auth` / `net`），`on data` 照常触发。手机限频 ≤ 1 次/秒、≤ 60 次/分。
- 敏感权限（`location` `microphone` `spare_notes`）和网络主机一定会人工审核；只申请真正需要的。只用 `apps.spare.cool` 的不需要人工确认主机。

## 用托管网页做设置页（现在就能用）

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

## 手机控制面板（controls）

> 支持情况：iOS 版 Spare App 显示控制面板；Android / 鸿蒙版跟进中。在不支持的 App 上 `ctl("id")` 是 `default`、`on control(id)` 不触发、
> `ctl_set` 收到 `_err`（`kind`），所以控件的 `default` 要设成合理的值；必须所有手机都能改的设置，用上面的「用托管网页做设置页」。

在 `app.json` 里声明控件，Spare App「我的眼镜应用」里这个应用会有「控制」页（原生开关、滑块……），用户改了眼镜马上收到；
手机记住每个应用的值，眼镜没连或应用没开时改的，下次打开应用时补发。下面是已经定稿的写法。

```json
"requests": [{ "name": "setctl", "kind": "control_set" }],
"controls": [
  { "id": "minutes", "type": "slider",  "label": "专注时长", "unit": "分钟", "min": 5, "max": 60, "step": 5, "default": 25 },
  { "id": "rest",    "type": "stepper", "label": "休息时长", "unit": "分钟", "min": 1, "max": 15, "default": 5 },
  { "id": "bar",     "type": "switch",  "label": "显示进度条", "default": 1 },
  { "id": "style",   "type": "picker",  "label": "时间样式", "options": ["大字", "细字", "暗"], "default": 0 },
  { "id": "title",   "type": "text",    "label": "标题", "max": 21, "default": "番茄钟+", "hint": "眼镜左上角显示的文字" },
  { "id": "finished","type": "label",   "label": "累计完成", "default": "0" },
  { "id": "reset",   "type": "button",  "label": "清零完成数", "confirm": "确定把累计完成数清零？" }
]
```

| type | 手机上 | 值（脚本里 `ctl("id")`） | 字段 |
|---|---|---|---|
| `switch` | 开关 | 0 / 1 | `default` |
| `slider` | 滑块（松手才发） | 整数 | `min` `max` `step`（默认 1）`default` `unit` |
| `stepper` | − / + 步进 | 整数 | 同 slider |
| `picker` | 分段选择（`"style": "menu"` = 下拉菜单） | 选项下标 0.. | `options`（2–8 个，每个 ≤ 31 字节）`default` |
| `text` | 单行输入框（提交才发） | 字符串 | `max` 字节（1–63，默认 31）`default` |
| `label` | 只读的一行「名称　值」 | 字符串 | `default`；眼镜用 `ctl_set` 改它 |
| `button` | 按钮 | 没有值 | `confirm`（可选，先弹确认；清空/删除类按钮要写） |

公共字段：`id`（小写字母开头，1–12 个 `[a-z0-9_]`，不能和请求 `fields` 的键重名）、`label`（≤ 63 字节）、`hint`（可选，控件下面一行小字）。
限制：≤ 12 个控件；控件 + 请求返回的数据键总共 ≤ 24 个（眼镜数据表容量）；整数 ±100000 以内、档位 ≤ 1000 个。

```js
on open() { minutes = ctl("minutes"); render(); }       // ctl() 随时能用：没收到过手机的值时返回 default

on control(id) {                                       // 用户在手机上动了控件
  if (id == "minutes" || id == "*") { minutes = ctl("minutes"); }   // "*" = 应用刚打开，手机补发了全部值
  if (id == "reset") { done = 0; save(); ctl_set("finished", 0); }
  render();
}

on timer(id) { if (id == 2) { ctl_set("minutes", minutes); } }    // 眼镜上改了：让手机面板跟着变
```

- `on control(id)`：`id` 是变了的控件 id；按钮按一次触发一次；应用每次打开后会收到一次 `id == "*"`（手机补发全部值），在这里重新读一遍所有值。
- `ctl("id")`：整数控件返回整数，`text` / `label` 返回字符串。值从手机来，眼镜上不能直接改；要改用 `ctl_set`。
- `ctl_set("id", 值)`：告诉手机「这个控件现在是这个值」（手机面板跟着变，`ctl()` 也会变）。要先在 `requests` 声明 `{"name": "setctl", "kind": "control_set"}`。
  它就是一次请求：参数 ≤ 10 字节（`序号=值`，值大约 7 个字符以内，适合数字），受 **≤ 1 次/秒** 限频——转表冠时别每格都发，停手后用计时器发一次。
  没连手机时会丢；下次打开应用手机会把它记的值补发过来（以手机为准）。
- 也可以照旧写 `on data(seq)`：控件推送不会进 `on data`，请求的回复照常进。
- 什么时候还是用托管网页：**现在所有手机设置都用托管网页**（见上面）；以后 App 支持 controls 了，长列表、导入文件、大段文字、多人或网页同步也还是用托管网页。

## 手机网页与托管后端（apps.spare.cool，每个应用自动开通）

眼镜应用自己不能联网，也收不到手机网页里的数据；能走的只有「眼镜 → 手机代发 GET → JSON」。所以「手机上操作、眼镜上显示」要一个中转，平台已经替每个应用准备好了：
地址都在 `https://apps.spare.cool/<应用 id>/` 下，MCP `app_backend` 返回完整地址和用量。

| 地址 | 方法 | 作用 |
|---|---|---|
| `/<id>/` | GET | 你的手机网页：仓库 `web/` 目录，MCP `publish_page` 发布（≤ 2 MB、≤ 100 个静态文件、要有 `index.html`、不能有密码框） |
| `/<id>/relay/<配对码>` | PUT / PATCH / GET / DELETE | 配对码中转：存一份 JSON（≤ 512 KB，12 小时没动就过期）。PUT 整份替换，PATCH 合并顶层字段 |
| `/<id>/relay/<配对码>/now` | GET | **时间轴**：JSON 里有 `timeline`（`[{"t": 开始毫秒, "end": 结束毫秒, …任意字段}]`）时，服务器按播放进度返回当前这一条，给眼镜轮询 |
| `/<id>/relay/<配对码>/control` | POST | 播放控制：`{"action":"play"}` `{"action":"pause"}` `{"action":"seek","pos":毫秒}` `{"action":"nudge","delta":±毫秒}`，可带 `"rate"` 倍速 |
| `/<id>/kv/<键>` | GET | 只读应用数据：开发者用 MCP `set_app_data` 写（每日一句、题库、配置），任何人可读 |

`/now` 返回：`{"state":"play|pause|end","pos":当前毫秒,"i":条目序号（-1 = 两条之间）,"n":总条数,"next":离下次变化的毫秒（暂停时 -1）,"item":{当前条目除 t/end 外的字段},"data":{除 timeline 外的顶层字段}}`。
两条之间 `item` 是空对象，眼镜 `data("zh")` 拿到空串，正好清屏。

**配对码就是钥匙**：知道码就能读写那份数据。码由眼镜生成（`rand`），显示在眼镜上，用户在手机网页里输入；用 8–10 位、去掉易混字符（0/O、1/I/L）。

**限制**（都来自手机代发请求那一侧）：眼镜参数 ≤ 10 字节（配对码最多 10 位）；每个值 ≤ 63 字节（约 21 个汉字，超出截断，**长句在网页里先拆成两行字段**）；
每个应用 ≤ 1 次/秒、≤ 60 次/分；只能 GET。网页写入每个 IP ≤ 60 次/分。

示例：手机导入字幕，眼镜按时间显示。

```json
"permissions": ["network:apps.spare.cool"],
"requests": [{ "name": "cue", "kind": "http_json", "url": "https://apps.spare.cool/dev.owen.subs/relay/{arg}/now",
               "fields": { "zh": "item.zh", "en": "item.en", "st": "state", "nx": "next" } }]
```

```js
persist var code = "";
fn gen() { var a = "ABCDEFGHJKMNPQRSTUVWXYZ23456789"; var s = ""; var i = 0;
           while (i < 8) { s = s + sub(a, rand(31), 1); i += 1; } return s; }
on open()    { if (code == "") { code = gen(); save(); } text(hint, "配对码 " + code); timer(0, 1000, 1); }
on timer(id) { request(cue, code); }                       // 每秒问一次「现在是哪一句」
on data(seq) { if (data("_err") != "") { text(hint, "配对码 " + code); return; }
               text(zh, data("zh")); text(en, data("en")); }
```

手机网页（`web/index.html`，和接口同源，直接用相对地址）：

```js
const code = input.value.trim().toUpperCase();
await fetch(`relay/${code}`, { method: 'PUT', headers: { 'Content-Type': 'application/json' },
  body: JSON.stringify({ title: '电影名', timeline: cues.map(c => ({ t: c.start, end: c.end, zh: c.zh, en: c.en })) }) });
await fetch(`relay/${code}/control`, { method: 'POST', body: JSON.stringify({ action: 'play', pos: 0 }) });   // 对时：开始
await fetch(`relay/${code}/control`, { method: 'POST', body: JSON.stringify({ action: 'nudge', delta: -500 }) }); // 字幕早了半秒
```

所有应用的网页在同一个域名下：localStorage 别的应用也能读到，**不要在里面放敏感数据**；记住配对码之类没问题。

发布顺序：提交 `web/` → MCP `publish_page` → 手机浏览器打开 `https://apps.spare.cool/<id>/` 试 → 眼镜端照常 `build` / `install_on_my_glasses`。

## SpareScript 速查

```js
var left = 1500;                 // 全局变量（≤ 64 个），初值在打开时赋
persist var done = 0;            // 持久变量：关应用、重启后还在
fn fmt(sec) { return pad(sec / 60, 2) + ":" + pad(sec % 60, 2); }   // 函数 ≤ 8 个参数，调用深度 ≤ 8

on open()     { text(time, fmt(left)); }
on crown(d)   { left = clamp(left + d * 60, 60, 3600); text(time, fmt(left)); }
on click()    { timer(0, 1000, 1); }                    // 计时器 id 0–3，周期 ≥ 100 ms，repeat 1/0；ms = 0 停止
on timer(id)  { left -= 1; if (left == 0) { timer(0, 0, 0); done += 1; save(); } text(time, fmt(left)); }
on data(seq)  { if (data("_err") != "") { text(time, "--"); return; } text(time, data("px")); }
on close()    { }
```

- 语句：`var`、`=` `+=` `-=` `*=` `/=`、`if/else`、`while`、`break`、`continue`、`return`。运算：`|| && == != < <= > >= + - * / % ! -`。
- `+` 有一边是字符串就拼接；`/` `%` 除以 0 得 0；字符串参与其他算术按十进制解析。`true`/`false` = 1/0。
- 打开顺序：全局初值 → 恢复持久变量 → `open()`。

| 内建 | 作用 |
|---|---|
| `text(el, v)` `show(el, on)` `gray(el, g)` `move(el, x, y)` `size(el, w, h)` `value(el, pct)` `image(el, n)` | 界面 |
| `anim_x(el, from, to, ms)` `anim_opa(el, from, to, ms)` `anim_scale(el, from, to, ms)` | 本地动画（scale 256 = 1.0；≤ 2000 ms；部分固件 scale 退化为淡入） |
| `timer(id, ms, repeat)` `now()` `clock()` `seconds()` | 计时器 / 毫秒计时 / 当天分钟数 / 当前分钟内的秒 |
| `str(n)` `num(s)` `len(s)` `sub(s, start, n)` `pad(n, width)` | 字符串（按字节，不切断 UTF-8） |
| `min` `max` `clamp(x, lo, hi)` `abs` `rand(n)` | 数学 |
| `data(key)` `request(req, arg)` `connected()` | 手机数据（离线时是缓存）/ 发请求（返回序号，未连接 0）/ 是否连着 |
| `ctl("id")` `ctl_set("id", v)` / `on control(id)` | 手机控制面板的值 / 回写到手机 / 用户动了控件（见「手机控制面板」） |
| `battery()` `save()` `exit()` | 电量 / 写盘（最多 10 秒一次）/ 退出 |

## 设计建议

- 一屏只放一件事：大字（64–96 px）+ 一行提示（20–24 px，gray 100–160）。绿色单色屏上，亮度对比就是层次。
- 用户在走路、说话：不要闪烁、不要需要盯着看的小字。
- 离线也要能打开：`connected()` 为假时显示缓存或「等待连接」。
