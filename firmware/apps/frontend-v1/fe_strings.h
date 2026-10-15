/* fe_strings.h — user-visible launcher strings. Spare original.
 *
 * Chinese is the default UI language. Build with -DSG_LANG_EN for English
 * (the host preview does this via `make preview SG_LANG=en`). Only literals
 * live here, so both variants share every layout number in fe_pages.c. */
#ifndef FE_STRINGS_H
#define FE_STRINGS_H

#ifdef SG_LANG_EN
/* control center tiles (titles, then one-glyph fallbacks when an icon is missing) */
#define FE_S_CC_TITLES   { "Back", "Brightness", "Volume", "Raise to Wake", "Do Not Disturb", "Bluetooth", "Battery" }
#define FE_S_CC_FALLBACK { ">", "B", "V", "R", "D", "BT", "%" }
#define FE_S_CC          "Control Center"
#define FE_S_TO_APP      "To app"
#define FE_S_TO_CLOCK    "Clock"
#define FE_S_ON          "On"
#define FE_S_OFF         "Off"
#define FE_S_CONNECTED   "Connected"
#define FE_S_DISCONNECTED "Not connected"
#define FE_S_CHARGING    "Charging"
#define FE_S_HINT_SELECT "Click to select"
#define FE_S_HINT_BACK   "Click to go back"
#define FE_S_HINT_ROTATE "Rotate to adjust"
#define FE_S_HINT_DONE   "Click when done"
#define FE_S_HINT_ADJUST "Click to adjust"
#define FE_S_HINT_TOGGLE "Click to toggle"
#define FE_S_HINT_INFO   "Display only"
/* in-app card */
#define FE_S_CARD        { "Back", "Control", "Exit" }
#else
#define FE_S_CC_TITLES   { "返回", "亮度", "音量", "抬头亮屏", "勿扰模式", "蓝牙", "电量" }
#define FE_S_CC_FALLBACK { ">", "亮", "音", "抬", "扰", "蓝", "电" }
#define FE_S_CC          "控制中心"
#define FE_S_TO_APP      "回到应用"
#define FE_S_TO_CLOCK    "表盘"
#define FE_S_ON          "已开启"
#define FE_S_OFF         "已关闭"
#define FE_S_CONNECTED   "已连接"
#define FE_S_DISCONNECTED "未连接"
#define FE_S_CHARGING    "充电中"
#define FE_S_HINT_SELECT "单击开始选择"
#define FE_S_HINT_BACK   "单击返回"
#define FE_S_HINT_ROTATE "旋转调节"
#define FE_S_HINT_DONE   "单击完成"
#define FE_S_HINT_ADJUST "单击调节"
#define FE_S_HINT_TOGGLE "单击切换"
#define FE_S_HINT_INFO   "仅显示"
#define FE_S_CARD        { "返回", "控制中心", "退出" }
#endif

#endif /* FE_STRINGS_H */
