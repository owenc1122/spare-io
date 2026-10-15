# frontend-v1 · 眼镜前端模块（净室）

`firmware/apps/frontend-v1/` 是 Spare 眼镜的全新前端交互模块：用一个表冠（侧边旋钮，可**旋转/单击/长按**）
驱动「时间页 → 控制中心 / App 页 → App 内 → AI 助手」的导航。

本模块 100% 为 Spare 原创：**不含任何厂商函数名、地址或偏移**。
它对底层固件的全部需求都收敛在一个自定义接口头 `frontend_port.h`，由净室固件团队在自家 SDK
（`firmware/sdk/sg_sdk.h`）上实现一次即可，UI 代码无需改动。

---

## 1. 统一控制律

**旋转 = 移动焦点，单击 = 确认，长按 = AI 助手。全程无双击语义。**

| 页面 | 顺时针 CW | 逆时针 CCW | 单击 | 长按 |
|---|---|---|---|---|
| 时间页（原厂表盘） | 滑入 App 页 | 滑入控制中心 | 熄屏 | AI 助手 |
| 熄屏 | 透传（保留原厂旋钮唤醒） | 透传 | 亮屏 | 亮屏 + AI |
| 控制中心·浏览 | — | 旋回=关闭回来源 | 进入选择态 | AI |
| 控制中心·选择 | 焦点下一项 | 焦点上一项 | 返回=关闭；滑块=进调节；开关=切换 | AI |
| 控制中心·调节 | +1 档 | −1 档 | 完成 | AI |
| App 页 | 下一图标（满 6 翻屏） | 上一（第 1 个上再逆转=回时间页） | 打开 App | AI |
| App 内（普通） | 委派 App（移动其焦点） | 委派 App | 呼出 3 键卡片 | AI |
| App 内（自管，`SG_APP_HANDLES_CLICK`） | 委派 App | 委派 App | 委派 App（App 自处理） | AI |
| App 内·卡片 | 下一按钮 | 上一按钮 | 返回/控制中心/退出 | AI |

- 控制中心第一项固定「返回」：回时间页；若从 App 内打开则回那个 App。
- 滑块（亮度/音量）：单击进调节、旋转改值、再单击确认。
- App 页 **3 列（横）× 2 行（竖）、每屏 6 图标**，焦点先左到右再换行，转过最后一个进入下一屏；选中高亮用弹簧「泡泡」过冲动画。
- 原厂 7 个 App 用原厂图标；我们自己的 App 用原创图标（见 §5）。

### 屏幕尺寸（给 music / nav / assistant 等模块）
启动器 App 页的可用 LVGL 逻辑屏是 **540 × 220**（静态确认：`lv_display_create(540, 220)`；帧缓冲 544×220
RGB565，单眼面板 540×280 4-bit 绿）。各 App 的 `on_start(parent, ...)` 收到的 `parent` 即覆盖此可用区，
**请按 540×220 布局**（留四边安全边距）。注意：历史文档里的「512×128」只是导航页内部的 L8 画布，不是整屏。
App 网格按 540×220 定为 3 列 × 2 行（每格约 180×110），布局数字以 SPEC.md（540×220 版）为准。

## 2. 架构

- `fe_state.{c,h}` — **纯交互归约器**。无 LVGL/OS/堆/全局，不依赖 `frontend_port`。
  输入解码后的 `FEEvent`，输出一串 `FEFx` 效果；确定性，可主机 + ARM 离线验证。
- `fe_registry.{c,h}` — 启动器 App 表。实现 SDK 共享契约 `sg_app_register`/`sg_app_unregister`
  （见 `firmware/sdk/sg_sdk.h`），并提供内部枚举 `fe_reg_count/at/find`。纯、有界、无堆。
- `fe_pages.{c,h}` — 页面构建（540×220，数字按 `SPEC.md`）：控制中心瓷砖/滑块/开关/信息栏、3×2 网格
  （泡泡 MOVE 弹簧 + 果冻、按安全区封顶过冲 `sg_move_channel_cap`、邻格随泡位置渐变、横向翻页
  用临界阻尼无过冲曲线、底部胶囊翻页点）、App 内卡片。只经 `fp_display`，无文件级可变状态；同一份源码
  进固件、也进主机 LVGL v9 预览后端。
- `fe_runtime.c` — 运行时胶水：装按键/编码器 hook、解码手势跑 `fe_step`、把 `FEFx` 执行为页面/设置/App
  生命周期、实现 `sg_front.h` 公开服务与 `sg_app_launch`。上下文单堆分配。
- `fe_spring.h` — 弹簧动效 Q10 整数表 + lv_anim path 回调（泡泡/翻页/卡片）。
- `frontend_port.h` — **唯一的厂商接缝**（见 §4）。
- `sg_front.h` — 对其它 App 模块公开的接口（见 §3）。
- `assets/icons/` — 我们自己的 App 图标 SVG + 生成器（见 §5）。

## 3. 给其它 App 模块的公开接口（`sg_front.h`）

App 注册用 SDK 契约 `sg_app_register(const sg_app*)`（`sg_sdk.h`，由本模块实现）。本模块另加：

- `#define SG_APP_HANDLES_CLICK (1u<<0)` — `sg_app.flags` 的标志位。置位的 App 自己接收单击，
  **不显示全局 3 键卡片**，须自绘「返回/控制中心/退出」，用下面两个服务实现。音乐 App 用这个。
- `sg_err_t sg_frontend_open_control_center(void (*return_cb)(void *), void *user)` — 在当前 App 之上打开
  控制中心；其「返回」调用 `return_cb`（App 重新显示自己的页面）。
- `sg_err_t sg_frontend_exit_app(void)` — 退出前台 App 回时间页。
- `void sg_front_set_app_input(void (*on_rotate)(int32_t), void (*on_click)(void))` — 自管 App 在 `on_start`
  里登记输入入口；前台期间前端把表冠旋转（带符号格数）和单击转发给它，长按仍归 AI。
- 注册时 `icon == NULL` 的 App，前端按 id 用我们自绘图标填充（`com.spare.music/nav/reader/pomodoro`）。

长按在任意页面 = 打开 AI 助手（`frontend_port` 的 `fp_assistant.open`）；助手的手机侧数据通路由
`assistant-v1` 模块负责，本模块只负责"长按→打开助手卡片"。

示例（音乐 App）：
```c
static sg_err_t music_start(sg_lv_obj parent, void *u){ /* build library/now-playing */ return SG_OK; }
static sg_err_t music_stop(void *u){ /* teardown */ return SG_OK; }
void music_module_init(void){
  sg_app a = { .id="music", .title="音乐", .on_start=music_start, .on_stop=music_stop,
               .flags=SG_APP_HANDLES_CLICK };
  sg_app_register(&a);
}
/* 在 on_start 里： sg_front_set_app_input(music_app_on_rotate, music_app_on_click); */
/* 「控制中心」按钮： sg_frontend_open_control_center(cc_returned, 0);  「退出」： sg_frontend_exit_app(); */
```

## 4. 厂商接缝 `frontend_port.h`（净室团队实现）

本模块对底层固件的**全部**需求都是这张表里的函数指针，均为能力描述、无地址/无厂商符号：

| 组 | 能力 |
|---|---|
| `fp_input` | 全局按键拦截器（单击/长按，返回是否消费）；表冠**编码器**旋转订阅（独立于按键） |
| `fp_screen` | 屏幕开/关、是否亮、刷新活跃时间 |
| `fp_settings` | 亮度 get/set（档 0..17）、音量 get/set（0..100）、抬头亮屏 get/set（读-改-写保留其它字段）、勿扰 get/set、蓝牙连接、电量、充电 |
| `fp_apps` | 原厂 App 数量/名称/图标(L8)/启动、回主页、前台 App id |
| `fp_assistant` | 打开/关闭/查询 原厂 AI 助手卡片 |
| `fp_display` | LVGL 门面：空闲判定、建对象/标签/画布、位置尺寸、圆角、灰度、画布缓冲、文字、可见、缩放、失效、销毁、弹簧动画、tick |

净室团队把这些实现为对 `sg_sdk.h`（或直接对厂商）的薄封装，再调用 `fe_runtime_init(port)` 启动。
本模块可用 mock port 做主机测试，换到真机基座时 UI 代码零改动。

> 说明：`sg_app_register`/`sg_app_launch` 属 `sg_sdk.h` 共享契约（本模块实现），不走 `frontend_port`。
> 旋转的物理方向（CW/CCW 对应 +/−）由基座在 `fp_input.hook_encoder` 里按设备设置归一化后给出。

## 5. 自绘 App 图标

原厂 7 个 App 用原厂图标（基座经 `fp_apps.stock_icon_l8` 提供）。我们自己的 App（导航/音乐/阅读/番茄钟）
用**原创矢量图标**，风格模仿原厂（单色绿描边、圆角端点、小四角星点缀），作为仓库素材入库：

- `assets/icons/*.svg` — 原创 SVG（本仓库美术）。
- `assets/icons/gen_icons.py` — 把 SVG 子集栅格化为打包 L8 数组，生成 `sg_app_icons.h`，供 `SG_ICON_L8` 注册。
  依赖 Pillow；重跑：`python3 assets/icons/gen_icons.py --box 30`。
- 不复用任何厂商/相近位图。

## 6. 验证

- `tests/test_fe_state.c`：主机单元测试，`-fsanitize=address,undefined`，覆盖全部转移
  （时间/熄屏、控制中心选择/焦点环绕/滑块/开关/返回目标、App 网格焦点/翻页回退/钳位/启动、
  App 内卡片、自管 App 的 click 委派 + 公开 CC/退出意图、长按 AI、确定性）。
- `tests/test_fe_registry.c`：主机测试，注册/替换/注销/枚举/容量/HANDLES_CLICK。
- `tests/test_fe_pages.c`：mock `fp_display`，验证 SPEC 几何、对象生命周期无泄漏、30 种跳转泡泡始终在安全区
  内且精确收敛、翻页双面板同步移动 + 胶囊点、焦点中途输入先完成翻页、控制中心与卡片。
- `tests/test_fe_runtime.c`：mock `frontend_port` 端到端：音乐契约（bit0、输入转发、控制中心 return_cb、
  退出）、按 id 填图标、原厂 App 卡片 + 退出、亮度/抬头亮屏落到 port、长按 AI、熄屏/亮屏。
- 视觉/动效规格与帧缓冲渲染随私有轨交付（本模块按 SPEC 的布局数字与弹簧表实现，`fe_spring.h` 即其整数表）。

复现（主机）：
```sh
cd firmware/apps/frontend-v1
xcrun clang -std=c11 -Wall -Wextra -Wno-comment -fsanitize=address,undefined -I . \
  tests/test_fe_state.c fe_state.c -o /tmp/fe && /tmp/fe
xcrun clang -std=c11 -Wall -Wextra -Wno-comment -fsanitize=address,undefined -I . \
  tests/test_fe_registry.c fe_registry.c -o /tmp/reg && /tmp/reg
```

## 7. 尚未完成

- 主机 LVGL v9 预览后端（`fp_display` 的真实 LVGL 实现 + `make preview` 出 PNG/GIF）由另一 Agent 实现；
  视觉以其渲染结果目视验收。
- 尚未实现的小动效：卡片弹出的 POP 缩放、按钮/图标 ×1.05~1.06 放大、信息栏 160 ms 淡入上浮（门面暂无
  缩放/透明度动画原语，现为直接切换）。
- 上机验收：真机 LVGL 页面/输入/原厂助手卡片联动。

## 8. LVGL 后端 + 主机预览（backend-lvgl/）

- `backend-lvgl/fp_display_lvgl.{c,h}`：`fp_display` 门面的真实 LVGL v9.2 实现，主机与真机同一份源码。
  无文件级可变静态量：上下文挂在 top layer 下我们自己的根容器上（magic 标记），门面表为 const。
  灰度 g 一律写成 `0x00gg00`；图标用 A8 图像 + `image_recolor` 着色。字体经 `fpl_init(font_fn, user)` 的取字体回调。
- `make preview`（`firmware/apps/frontend-v1/`）：克隆 LVGL v9.2.2 到 `build/lvgl`，540×220 XRGB8888 显示驱动逐帧导出，
  脚本化表冠输入驱动 `fe_state` + `fe_pages.c`（与固件同一份），按 lv_tick 每帧 20 ms（50 fps），输出
  `build/frontend-preview/*.png`（每页/状态）与 `*.gif`（转场）。主机中文字体 = Noto Sans SC TTF（OFL，tiny_ttf），
  `NOTO_SC_TTF=/path/NotoSansSC-Bold.ttf` 可覆盖。时间页与前台 App 为主机替身；页级滑动/熄屏淡出由预览驱动经门面完成。
- `make device-check`：`fe_pages.c` + 后端以 thumbv8m.main / hard-float / `-Werror` 对 LVGL 头编译，并断言无 .data/.bss。
  注意：真机须用与基座 LVGL 相同的 lv_conf（`lv_anim_t`/`lv_image_dsc_t` 布局必须一致）。

### facade requests（LVGL 后端 → fe_pages/frontend_port 维护者）
1. **缩放/变换**：`scale(o, q8)`（v9 `transform_scale`，以对象中心为原点）。SPEC 需要：卡片弹出 0.55→1.0（POP）/收起 →0.88、
   熄屏 1.0→0.96、图标/按钮选中 ×1.06、控制中心选择态 ×1.05。当前预览中这些缩放均缺失（卡片直接出现）。
2. **时间页句柄**：`fp_obj time_page(void)`（或 `fp_ui` 组）返回原厂表盘对象，供时间页↔控制中心/App 页滑动和熄屏淡出；
   页级转场（PAGE 滑动、熄屏 280 ms 淡出后再 `screen->set(false)`）目前在预览驱动里实现，建议收进 fe_pages/fe_runtime。
3. **字重**：`label(parent, px)` 加 weight（SPEC 区分 Semibold / Medium）；主机现统一用 Noto Sans SC Bold。
4. **文本对齐**：`text` 只有居中；控制中心信息栏按 SPEC 应左对齐 x=392。建议 `text_align(o, left|center|right)`。
5. **子对象裁剪**：滑块填充应被轨道圆角裁切（顶边平直），建议 `clip_children(o, bool)`（v9 `clip_corner`）。
6. **矢量字形**：控制中心/卡片字形目前用单字文本（亮/音/抬/扰/蓝、<、::、X）；门面已支持 `canvas` + L8，
   建议把 SPEC 字形（返回/太阳/喇叭/抬头/月亮/蓝牙/电池/控制中心/退出）烘焙成 L8 资源走 `canvas_buf`。

## 8. 门面需求（facade requests，给 backend-lvgl）

已全部写进 `frontend_port.h` 的 `fp_display`（头文件先提交，后端可并行实现）。fe_pages/fe_runtime 已全部使用，后端缺项时自动退回旧行为：

| # | 需求 | 接口 |
|---|---|---|
| 1 | 缩放/透明度动画（卡片弹出缩放+背景压暗、图标聚焦 ×1.06、灭屏淡出+0.96、信息栏淡入） | `scale`、`anim_scale`、`anim_opa`、`after`；缓动 `fp_path`（含 `FP_PATH_SPRING_MOVE/POP/PAGE_CD`），后端可直接用 `fe_path_q10()` 求值 |
| 2 | 时间页句柄，使时间页↔控制中心/App 页的滑动与灭屏都在 fe_pages/fe_runtime 内完成 | `time_page`、`translate_x`、`anim_translate_x`；前端只改其平移/透明度/缩放，回到时间页时恢复 0/255/256 |
| 3 | 字重 | `label(parent, px, FP_W_REGULAR/FP_W_BOLD)` |
| 4 | 左对齐标签（信息栏 x=392） | `text_align(o, FP_ALIGN_LEFT)` |
| 5 | 滑块平顶填充 | `clip_corner(track, true)`：填充为直角矩形，被圆角轨道裁剪 |
| 6 | 真实图形图标替换单字占位（已实现：太阳/喇叭/抬头亮屏/月亮/蓝牙/电池/返回/控制中心/退出） | `image` + `image_a8(o, a8, w, h)` + `image_gray(o, g)`；字形由 `assets/glyphs/*.svg` 经 `gen_icons.py --glyphs` 生成 `sg_glyphs.h`（A8） |

### 后端回复：App 网格泡泡右偏（2026-10-13）
- 结论：**不在 LVGL 后端这一侧**。`fp_display_lvgl.c` 的 `pos_size` 只做 `lv_obj_set_pos(x,y)` + `lv_obj_set_size(w,h)`（左上角语义），
  不加 align、不设 transform pivot 以外的偏移；`scale` 的 pivot = 对象中心（`lv_pct(50)`），不改 x/y。
- 实测（3c0b997 预览，`c_apps*.l8.png` 第 52/152 行亮边）：第 1 列泡外沿 x=16..163（中心 89.5，SPEC 90），
  第 2 列 196..343（中心 269.5，SPEC 270），与格中心一致，图标/标签居中。
- 若真机上偏右，优先查：基座 `fp_display.obj` 是否没清掉默认主题样式（父对象 pad/border 会把子对象整体右下移，
  而泡与图标不在同一父对象时就会相对错位）；或 fe_bubble_frame 果冻拉伸改了 w 却没以中心重算 x。
- 复测（27ad74f 方形泡 88×88，1c98cbf 预览）：泡外沿 x=46..133 / 226..313，中心 89.5 / 269.5，仍居中；后端未改坐标逻辑。

## 9. App 页焦点泡（owner 2026-10-13 改为紧凑圆角方块）

覆盖 `prototypes/frontend-preview/out/SPEC.md` (c) 中「高亮泡 148×88 r24」一行（SPEC 文件本身未改）：

| 项 | 值 |
|---|---|
| 焦点泡 | **92×92 圆角方块，r=23**，中心 = 格中心 (x, y)；x = 90/270/450，y = 52/152 |
| 图标 | 44 px，中心 (x, y−13) |
| 标签 | 18 px 粗体，框 96×22，中心 (x, y+24) |
| 图标+标签块 | 高 70（y−35 … y+35），在泡内垂直居中；4 字标签（如「实时字幕」）两侧约 8 px 余量 |
| 动效 | 中心走 MOVE 弹簧（按安全区 x 4..536 / y 3..204 封顶过冲，`sg_move_channel_cap`）；果冻拉伸/压扁对称作用于中心，宽高取偶数，形变时仍居中 |
| 聚焦图标 | 随泡位置 k 放大到 ×1.06（`scale`），邻格 ×1.0 |

验证：`tests/test_fe_pages.c`（mock）与 **`tests/test_fe_lvgl_centring.c`（真实 LVGL v9 后端读回 `lv_obj_get_coords`）**：
两页全部格子泡中心与图标中心、标签中心、图标+标签块中心的偏差均为 **0.0 px**（阈值 ≤1 px），块完全在泡内；
控制中心 7 个焦点环与瓷砖、卡片 3 个焦点环与按钮及字形同样 0.0 px。构建命令见该测试文件头。
