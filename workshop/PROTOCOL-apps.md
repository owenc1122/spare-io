# Spare 眼镜应用：包格式、SpareScript、线协议（runtime 1）

Spare 原创规范，供固件、手机、打包工具和第三方开发者共用。
所有多字节整数为小端。「哈希」= 应用 id 字符串的 FNV-1a 32 位（offset 2166136261，prime 16777619）。

## 1. 开发者看到的应用

一个应用目录：

```
app.json     manifest + 界面
main.ss      SpareScript 源码
icon.png     44×44 灰度图标（可选；没有则用名字首字）
assets/*.png 可选图片（4-bit 灰度，宽为偶数）
```

`app.json`：

```json
{
  "id": "dev.spare.pomodoro",          // 反向域名，≤ 31 字节，[a-z0-9._-]
  "name": "番茄钟+",                    // 列表里显示的名字，≤ 23 字节 UTF-8
  "version": 3,                         // 1..65535，更新时必须变大
  "data_version": 1,                    // 持久数据格式版本；变了则更新时清掉旧数据
  "permissions": ["network:api.example.com", "location"],
  "requests": [
    { "name": "quote", "kind": "http_json",
      "url": "https://api.example.com/q?s={arg}",
      "fields": { "px": "price", "chg": "change" } }
  ],
  "ui": [
    { "id": "time", "type": "text", "x": 0, "y": 40, "w": 540, "h": 100, "size": 96, "bold": true, "align": "center", "text": "25:00" },
    { "id": "bar",  "type": "bar",  "x": 70, "y": 170, "w": 400, "h": 8, "radius": 4, "value": 0 }
  ]
}
```

- 屏幕 540×220，原点左上，单色：`gray` 0–255 是亮度（显示成绿色）。
- 界面元素（≤ 48 个）：
  - `text`：`size`（12–96 px）、`bold`、`align`（left/center/right）、`text`（初始文字，≤ 63 B）、`gray`（默认 255）。
  - `rect`：`radius`、`gray`（填充亮度）、`border`（描边宽度，>0 时只描边）、`children`（子元素坐标相对它）。
  - `bar`：进度条，`value` 0–100，`gray`（填充，默认 255）、`track`（底色，默认 55）、`radius`。
  - `image`：`src` 为 `assets/` 下的图片名。
  - 公共：`id`（脚本里按名字引用）、`x y w h`、`hidden`。
- `requests`：应用能让手机代办的事，**只有这里声明的才能请求**。眼镜只发「第几个请求 + ≤ 10 字节参数」，URL 和主机从不来自眼镜。
  - `http_json`：GET `url`（`{arg}` 替换为 URL 编码后的参数），主机必须在 `permissions` 的 `network:` 里；`fields` 把返回 JSON 的路径（`a.b.0.c`）映射成数据键。
  - `location`：返回 `lat`、`lon`（字符串，5 位小数）、`city`。需要 `location` 权限。
  - `weather`：返回 `temp`、`text`、`icon`（手机现有天气源）。需要 `location` 权限。
  - `time_sync`：返回 `tz`（UTC 偏移分钟）。无需权限。
  - `notify_count`：返回 `count`（未读通知数）。需要 `notifications` 权限。
- 手机对每个应用限频：≤ 1 次/秒、≤ 60 次/分；每个值 ≤ 63 字节（超出截断）。
- `controls`（可选，≤ 12 个）：手机 App 里这个应用的原生控制面板（iOS 已实现；Android / 鸿蒙未实现）。`switch`（0/1）、`slider` / `stepper`（整数，`min` `max` `step`）、
  `picker`（`options` 2–8 项，值是下标）、`text`（≤ `max` 字节，≤ 63）、`label`（只读文字，眼镜经 `control_set` 改）、`button`（无值，可带 `confirm`）。
  公共：`id`（`[a-z][a-z0-9_]{0,11}`）、`label`（≤ 63 B）、`hint`、`default`。控件 + 请求返回的键 + `_c` ≤ 24（眼镜数据表）。
- `control_set` 请求：参数 `<控件序号>=<值>`，手机把这个控件存成该值，回 `{控件id: 值}`（序号原样带回）。无需权限。
- 平台托管后端 `https://apps.spare.cool/<id>/`（每个创意工坊应用自动开通）：配对码中转 `relay/<码>`、时间轴 `relay/<码>/now`、只读数据 `kv/<键>`、托管手机网页。
  眼镜照常用 `http_json` 读，权限写 `network:apps.spare.cool`，URL 只能指向自己的 id。接口细节见模板仓库 `AGENTS.md`「手机网页与托管后端」。

## 2. SpareScript

类 JS 的小语言，只有两种值：**32 位整数**和**字符串**（≤ 63 字节，超出截断）。`true`/`false` 就是 1/0。

```js
var left = 1500;            // 全局变量（≤ 64 个）
persist var done = 0;       // 持久变量：关应用、重启后还在（写进眼镜 NAND）

fn fmt(sec) { return pad(sec / 60, 2) + ":" + pad(sec % 60, 2); }

on open()     { text(time, fmt(left)); }
on crown(d)   { left = clamp(left + d * 60, 60, 3600); text(time, fmt(left)); }   // d = +1 顺时针 / -1
on click()    { timer(0, 1000, 1); }
on timer(id)  { left -= 1; if (left == 0) { timer(0, 0, 0); done += 1; } text(time, fmt(left)); }
on data(seq)  { text(time, data("px")); }       // 手机回了数据
on close()    { }
```

- 语句：`var`、赋值（`=` `+=` `-=` `*=` `/=`）、`if/else`、`while`、`break`、`continue`、`return`、表达式语句。
- 运算：`|| && == != < <= > >= + - * / % ! -`。`+` 只要有一边是字符串就拼接；`/`、`%` 除以 0 得 0；字符串参与其他算术时按十进制解析。
- `fn` 定义函数（≤ 8 个参数，调用深度 ≤ 8）。界面元素和请求直接用名字（`time`、`quote`）。
- 事件：`open` `close` `crown(d)` `click` `timer(id)` `data(seq)`。双击永远是「退出应用」，长按永远是 AI，不交给应用。
- 配额：每次事件 ≤ 20,000 条指令。超了这次处理中止；连续 3 次 → 应用停下显示「应用出错」。

内建函数：

| 函数 | 作用 |
|---|---|
| `text(el, v)` | 设置文字（数字自动转字符串） |
| `show(el, on)` `gray(el, g)` `move(el, x, y)` `size(el, w, h)` | 显示/亮度/位置/大小 |
| `value(el, pct)` | 进度条 0–100 |
| `image(el, n)` | 换成第 n 张图（`assets` 顺序） |
| `anim_x(el, from, to, ms)` `anim_opa(el, from, to, ms)` `anim_scale(el, from, to, ms)` | 本地动画（scale 256 = 1.0；≤ 2000 ms） |
| `timer(id, ms, repeat)` | id 0–3；ms ≥ 100；ms = 0 停止 |
| `now()` | 毫秒计时 | 
| `clock()` | 本地时间：从 0 点起的分钟数 |
| `seconds()` | 本地时间：当前分钟内的秒 |
| `str(n)` `num(s)` `len(s)` `sub(s, start, n)` `pad(n, width)` | 字符串（`sub` 按字节、不切断 UTF-8） |
| `min(a,b)` `max(a,b)` `clamp(x,lo,hi)` `abs(x)` `rand(n)` | 数学 |
| `data(key)` | 手机回的最近一个值（没有则 `""`）；离线时是上次的缓存 |
| `request(req, arg)` | 让手机执行请求，返回序号（未连接 0） |
| `connected()` | 手机是否连着 |
| `battery()` | 眼镜电量 |
| `save()` | 立刻把持久变量写盘（关应用时也会自动写） |
| `exit()` | 退出应用 |

## 3. 包格式 `.spa`（打包工具输出，手机安装）

```
0   "SPA1"
4   u8  format = 1
5   u8  flags (bit0 = 开发者自签)
6   u16 section count
8   u32 total length（整个文件）
12  u32 CRC-32/IEEE of bytes [16, total)
16  sections: { u16 type, u16 0, u32 len, len bytes, 0-pad to 4 }
```

| type | 内容 | 眼镜用 |
|---|---|---|
| 1 META | `u32 hash, u16 version, u16 data_version, u8 runtime_min(=1), u8 n_globals, u8 n_persist, u8 n_requests, char id[32], char name[24]`（NUL 填充），共 68 B | ✓ |
| 2 ICON | 44×44 行程 4-bit：`byte = level<<4 \| (run-1)`，解到 1936 像素为止 | ✓ |
| 3 STRINGS | `u16 count, u16 off[count]`，后接 NUL 结尾的 UTF-8；每条 ≤ 63 B | ✓ |
| 4 UI | `u8 count, u8 0, u16 0`，后接 count × 20 B 元素记录（见下） | ✓ |
| 5 CODE | `u8 n_funcs, u8 events[6], u8 0`，`n_funcs × {u16 off, u16 len, u8 n_params, u8 n_locals, u16 0}`，后接字节码 | ✓ |
| 6 ASSETS | `u8 count, u8 0, u16 0`，`count × {u16 w, u16 h, u32 off}`，后接 4-bit 打包像素（高半字节 = 左像素，0 透明） | ✓ |
| 7 MANIFEST | `app.json` 原文（手机用：权限、请求定义） | — |
| 8 SIG | `u8 alg(1=Ed25519), u8 0[3], u8 pubkey[32], u8 sig[64]`；签名覆盖 `[16, 本节起点)`；必须是最后一节 | 手机验 |

界面元素记录（20 B）：`u8 type(1 text, 2 rect, 3 bar, 4 image), u8 parent(0xff = 根，否则是更早的 rect 下标), i16 x, i16 y, u16 w, u16 h, u8 gray, u8 size_or_radius, u8 flags(bit0 bold, bit1-2 align 0 中/1 左/2 右, bit3 hidden), u8 border, u16 str(初始文字串号；image: 图片号；0xffff 无), u8 value, u8 track, u16 0`。

`events` 依次是 open、close、crown、click、timer、data 的函数号（0xff = 无）。

### 字节码

栈机，每个函数自己的局部变量（参数在前）。操作数小端，跳转偏移相对下一条指令。

| 码 | 名 | 操作数 | 栈 |
|---|---|---|---|
| 0x01 | PUSH_I8 | i8 | → n |
| 0x02 | PUSH_I32 | i32 | → n |
| 0x03 | PUSH_S | u16 串号 | → s |
| 0x04/0x05 | LOAD_G / STORE_G | u8 | → v / v → |
| 0x06/0x07 | LOAD_L / STORE_L | u8 | → v / v → |
| 0x08 | POP | | v → |
| 0x09 | DUP | | v → v v |
| 0x10–0x15 | ADD SUB MUL DIV MOD NEG | | a b → r（NEG: a → r） |
| 0x16–0x1b | EQ NE LT LE GT GE | | a b → 0/1 |
| 0x1c | NOT | | a → 0/1 |
| 0x30 | JMP | i16 | |
| 0x31/0x32 | JZ / JNZ | i16 | c → |
| 0x40 | CALL | u8 函数号 | args → r |
| 0x41 | RET | | r → |
| 0x42 | SYS | u8 内建号, u8 参数个数 | args → r |

内建号按 §2 表格顺序：0 text 1 show 2 gray 3 move 4 size 5 value 6 image 7 anim_x 8 anim_opa 9 anim_scale 10 timer 11 now 12 clock 13 seconds 14 str 15 num 16 len 17 sub 18 pad 19 min 20 max 21 clamp 22 abs 23 rand 24 data 25 request 26 connected 27 battery 28 save 29 exit。

眼镜安装时验证（不过就拒装）：每条指令合法、操作数在范围内（串号、全局/局部下标、函数号、内建号 + 参数个数、界面元素和请求号由运行时再查）、跳转落在本函数的指令边界上、每个位置的栈深度唯一且 ≤ 32、函数不能从末尾掉出去（最后一条必须是 RET 或 JMP）。

## 4. 线协议

能力：SGE1 byte 16 bit7（`AI_CAPS2_RT`）= 眼镜有运行时。

### 安装 / 更新：文件 `spare-app.spi`（文件通道 business 0x11，每片一个文件）

```
"SGI1", u8 version 1, u8 part k (0..n-1), u8 parts n (1..16), u8 0,
u32 hash, u32 package length (≤ 32768), u32 package CRC-32, then ≤ 2048 bytes of the package
```

第 0 片开始一次新安装（丢掉没装完的）；片按顺序到。最后一片到齐后眼镜校验、验证、写盘、读回比对，回 SGE1 事件 12 op 4。同一 id 再装 = 更新；`data_version` 不同则清持久数据。最多 8 个应用。

### 管理：文件 `spare-rt.cfg`

```
"SGR1", u8 version 1, u8 op, u8 n, u8 0, then n × u32 hash
op 1 LIST（n = 0）  2 REMOVE  3 HIDE  4 SHOW  5 ORDER（n = 全部已装应用，新顺序）
```

### 眼镜 → 手机：SGE1 事件 12

SGE1 32 字节（Spare 眼镜事件帧），byte 5 = 12，byte 13 = op，byte 14 = a，byte 15 = b，bytes 17..31 = tail：

| op | a | b | tail |
|---|---|---|---|
| 1 OPENED | 槽 | 0 | u32 hash |
| 2 CLOSED | 槽 | 0 | u32 hash |
| 3 REQUEST | 请求号 | 序号 1..255 | u32 hash, u8 arg_len (≤ 10), arg |
| 4 INSTALL | 结果 | 槽 | u32 hash, u16 version |
| 5 LIST | 下标 | 总数 | u32 hash, u16 version, u8 flags(bit0 hidden), u8 槽（总数 0 = 没有应用：只发一条，下标 0） |
| 6 MANAGE | 结果 | op | u32 hash |
| 7 FAULT | 原因（1 配额 2 运行错误 3 内存） | 0 | u32 hash |

结果码：0 OK、1 片错乱、2 CRC 错、3 格式错、4 验证失败、5 太大、6 满了、7 写盘失败、8 没找到、9 运行时太旧、10 内存不足。

### 手机 → 眼镜：raw `SGD1`（business 15 消息体，一次写，无文件握手）

```
"SGD1", u8 version 1, u8 count (1..16), u32 hash, u8 seq (请求序号；0 = 主动推送), u8 flags (bit0 = 最后一条),
then count × { u8 key_len (1..15), key, u8 value_len (0..63), value }，总长 ≤ 240，必须正好用完
```

应用开着：值写进它的数据表（≤ 24 个键，满了替换最旧的），flags bit0 时触发 `on data(seq)`。应用没开：丢弃（手机在收到 OPENED 后补发）。数据表随持久变量一起写盘，所以离线打开时 `data()` 是上次的缓存。

**手机控制面板**（manifest 有 `controls` 的应用，不需要新消息）：手机给这个应用的**每一批** seq 0 推送都在最后一条里带键 `_c`：
用户动了控件 = `{<id>: <值>, "_c": "<id>"}`（按钮只有 `_c`）；收到 OPENED 后补发全部非按钮控件的当前值（含 default）+ `"_c": "*"`；
其他推送（听写进度等）`"_c": ""`。整数值发十进制文字。spac 把 `on control(id)` 编译进 data 处理函数：`seq == 0 && data("_c") != ""` 时调用它，否则调用 `on data`。
