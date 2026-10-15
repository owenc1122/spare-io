/* preview_main.c — host preview renderer for the Spare front-end (macOS).
 * Clean-room, Spare original. HOST ONLY (statics allowed here).
 *
 * Real LVGL v9.2 with a 540x220 XRGB8888 display whose frames are dumped as L8
 * (green channel) PGMs. The REAL front-end — fe_runtime.c + fe_state.c +
 * fe_registry.c + fe_pages.c, the same files the firmware links — runs on a
 * mock frontend_port whose display is backend-lvgl's fp_display. A scripted
 * crown sequence is injected through the port's key/encoder hooks; time
 * advances 20 ms per frame through lv_tick (50 fps). assemble.py turns the
 * dumps into PNG stills and GIF transitions.
 *
 * Host stand-ins: the stock watch face (fp_display.time_page), the stock apps
 * (labels + placeholder icons, a plain "app" page on launch) and our own apps'
 * pages. The stock click-to-wake is emulated when the runtime passes it on.
 *
 * usage: preview <regular.ttf> <bold.ttf> <raw-out-dir>
 * Build with -DSG_LANG_EN (make preview SG_LANG=en) for English strings. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lvgl.h"
#include "../fp_display_lvgl.h"
#include "../../fe_pages.h"
#include "../../sg_front.h"
#include "../../../../sdk/sg_ctx.h"
#include "host_icons.h"

/* preview-only stand-in strings (stock app names, watch face, placeholder pages) */
#ifdef SG_LANG_EN
#define PV_MENU_HINT "Click for menu"
#define PV_STOCK     { "Recorder", "Captions", "Tips", "To-do", "Prompter", "Notes" }
#define PV_STOCK_APP "Stock app"
#define PV_NAV_BODY  "Turn right in 200 m"
#define PV_DATE      "Tue, Oct 13"
#define PV_NAV "Navigate"
#define PV_MUSIC "Music"
#define PV_READER "Reader"
#define PV_POMO "Pomodoro"
#else
#define PV_MENU_HINT "单击呼出菜单"
#define PV_STOCK     { "录音", "实时字幕", "实时提示", "待办", "提词器", "全天智记" }
#define PV_STOCK_APP "原厂应用"
#define PV_NAV_BODY  "前方 200 米 右转"
#define PV_DATE      "10月13日 星期二"
#define PV_NAV "导航"
#define PV_MUSIC "音乐"
#define PV_READER "阅读"
#define PV_POMO "番茄钟"
#endif

_Static_assert(HOST_ICON_BOX == FE_ICON_BOX, "host icons must match FE_ICON_BOX");

#define W FE_SCR_W
#define H FE_SCR_H
#define FRAME_MS 20

/* ---------------- SDK stubs the runtime uses ------------------------------ */
void *sg_calloc(size_t c, size_t s) { return calloc(c, s); }
/* host stand-in for the boot-owned context slots (device: hooks/hk_ctx.c) */
static void *ctx_slot[SG_CTX__COUNT];
void *sg_ctx_get(sg_ctx_id id) { return ctx_slot[id]; }
sg_err_t sg_ctx_set(sg_ctx_id id, void *c) { ctx_slot[id] = c; return SG_OK; }
void  sg_free(void *p) { free(p); }
void  sg_log(sg_log_level l, const char *t, const char *f, ...) {
  (void)l; va_list ap; va_start(ap, f); printf("  [%s] ", t); vprintf(f, ap); printf("\n"); va_end(ap);
}

/* ---------------- LVGL host display + virtual time ------------------------ */
static uint32_t g_tick;
static uint32_t fb[W * H];
static uint32_t tick_cb(void) { return g_tick; }
static void flush_cb(lv_display_t *d, const lv_area_t *a, uint8_t *px) { (void)a; (void)px; lv_display_flush_ready(d); }

/* fonts: Noto Sans SC Regular/Bold (OFL) via tiny_ttf, one instance per size */
typedef struct { void *ttf[2]; size_t len[2]; lv_font_t *by_px[2][160]; } FontSet;
static FontSet FS;
static const void *font_get(unsigned px, fp_weight w, void *user) {
  FontSet *f = (FontSet *)user;
  int k = w == FP_W_BOLD ? 1 : 0;
  if (px < 8) px = 8;
  if (px >= 160) px = 159;
  if (!f->by_px[k][px])
    f->by_px[k][px] = lv_tiny_ttf_create_data_ex(f->ttf[k], f->len[k], (int32_t)px, LV_FONT_KERNING_NONE, 512);
  return f->by_px[k][px];
}
static void load(const char *path, void **buf, size_t *len) {
  FILE *f = fopen(path, "rb");
  if (!f) { perror(path); exit(1); }
  fseek(f, 0, SEEK_END); *len = (size_t)ftell(f); fseek(f, 0, SEEK_SET);
  *buf = malloc(*len);
  if (fread(*buf, 1, *len, f) != *len) exit(1);
  fclose(f);
}

/* ---------------- mock port ------------------------------------------------ */
static const fp_display *D;
static bool (*KEY)(fp_key, void *); static void *KEYU;
static void (*ENC)(int, void *);    static void *ENCU;
static bool hk(bool (*f)(fp_key, void *), void *u) { KEY = f; KEYU = u; return true; }
static bool he(void (*f)(int, void *), void *u) { ENC = f; ENCU = u; return true; }
static const fp_input IN = { hk, he };

static bool scr_on = true;
static bool s_on(void) { return scr_on; }
static void s_set(bool on) { scr_on = on; }
static void s_act(void) {}
static const fp_screen SCR = { s_on, s_set, s_act };

static int bri = 12, vol = 45; static bool rai = true, dnd = false;
static int  g_b(void) { return bri; } static void s_b(int v, bool f) { (void)f; bri = v; }
static int  g_v(void) { return vol; } static void s_v(int v) { vol = v; }
static bool g_r(void) { return rai; } static void s_r(bool v) { rai = v; }
static bool g_d(void) { return dnd; } static void s_d(bool v) { dnd = v; }
static bool g_bt(void) { return true; } static int g_bat(void) { return 82; } static bool g_ch(void) { return false; }
static const fp_settings SET = { g_b, s_b, g_v, s_v, g_r, s_r, g_d, s_d, g_bt, g_bat, g_ch };

/* stand-in app page (stock or ours) */
static fp_obj app_pg; static const char *fg;
static void app_page(const char *title, const char *body) {
  if (app_pg) D->destroy(app_pg);
  app_pg = D->obj(D->root());
  D->pos_size(app_pg, 0, 0, W, H); D->bg(app_pg, 0, 255);
  fp_obj t = D->label(app_pg, 26, FP_W_BOLD); D->pos_size(t, 0, 24, W, 40); D->text(t, title); D->text_gray(t, 255);
  fp_obj b = D->label(app_pg, 38, FP_W_BOLD); D->pos_size(b, 0, 84, W, 60); D->text(b, body); D->text_gray(b, 200);
  fp_obj h = D->label(app_pg, 18, FP_W_REGULAR); D->pos_size(h, 0, 166, W, 30); D->text(h, PV_MENU_HINT); D->text_gray(h, 110);
}
static void app_close(void) { if (app_pg) { D->destroy(app_pg); app_pg = 0; } fg = 0; }

static const char *const STOCK[6] = PV_STOCK;
static const uint8_t *const STOCK_ICON[6] = { host_icon_mic, host_icon_caption, host_icon_bulb,
                                               host_icon_todo, host_icon_prompt, host_icon_note };
static int  a_n(void) { return 6; }
static const char *a_lab(int i) { return STOCK[i]; }
static bool a_icon(int i, uint8_t *dst, unsigned box) {
  if (box != HOST_ICON_BOX) return false;
  memcpy(dst, STOCK_ICON[i], box * box); return true;
}
static void a_launch(int i) { app_page(STOCK[i], PV_STOCK_APP); fg = "stock"; }
static void a_home(void) { app_close(); }
static const char *a_fg(void) { return fg; }
static const fp_apps APPS = { a_n, a_lab, a_icon, a_launch, a_home, a_fg };

static void ai_open(void) { printf("  (stock AI assistant opens)\n"); }
static void ai_close(void) {} static bool ai_is(void) { return false; }
static const fp_assistant AI = { ai_open, ai_close, ai_is };

/* our own apps (icon == NULL -> runtime fills our SVG art by id) */
static sg_err_t our_start(sg_lv_obj p, void *u) { (void)p; app_page((const char *)u, PV_NAV_BODY); fg = "ours"; return SG_OK; }
static sg_err_t our_stop(void *u) { (void)u; app_close(); return SG_OK; }

/* stock watch face stand-in */
static fp_obj build_time(void) {
  fp_obj t = D->obj(D->root());
  D->pos_size(t, 0, 0, W, H); D->bg(t, 0, 255);
  fp_obj c = D->label(t, 100, FP_W_BOLD); D->pos_size(c, 0, 92 - 60, W, 120); D->text(c, "10:42"); D->text_gray(c, 255);
  fp_obj d = D->label(t, 24, FP_W_REGULAR); D->pos_size(d, 0, 165 - 18, W, 36); D->text(d, PV_DATE); D->text_gray(d, 170);
  return t;
}

/* ---------------- frame recording ----------------------------------------- */
static char g_out[512];
static char g_rec[64];
static int  g_rec_n;
static void write_pgm(const char *path) {
  FILE *f = fopen(path, "wb");
  if (!f) { perror(path); exit(1); }
  fprintf(f, "P5\n%d %d\n255\n", W, H);
  static uint8_t row[W];
  for (int y = 0; y < H; y++) {
    for (int x = 0; x < W; x++) row[x] = scr_on ? (uint8_t)((fb[y * W + x] >> 8) & 0xff) : 0;  /* green = L8 */
    fwrite(row, 1, W, f);
  }
  fclose(f);
}
static void mkdirs(const char *p) { char b[600]; snprintf(b, sizeof b, "mkdir -p '%s'", p); if (system(b)) exit(1); }
static void frame(void) {
  g_tick += FRAME_MS;
  lv_timer_handler();
  lv_refr_now(NULL);
  if (g_rec[0]) { char p[700]; snprintf(p, sizeof p, "%s/anim/%s/%04d.pgm", g_out, g_rec, g_rec_n++); write_pgm(p); }
}
static void wait_ms(int ms) { for (int t = 0; t < ms; t += FRAME_MS) frame(); }
static void snap(const char *name) { char p[700]; snprintf(p, sizeof p, "%s/stills/%s.pgm", g_out, name); write_pgm(p); printf("  still %s\n", name); }
static void rec(const char *name) {
  snprintf(g_rec, sizeof g_rec, "%s", name); g_rec_n = 0;
  char p[700]; snprintf(p, sizeof p, "%s/anim/%s", g_out, name); mkdirs(p);
}
static void rec_end(void) { printf("  anim %s (%d frames)\n", g_rec, g_rec_n); g_rec[0] = 0; }

/* crown input through the port hooks */
static void rot(int d, int hold) { ENC(d, ENCU); wait_ms(hold); }
static void click(int hold) {
  if (!KEY(FP_KEY_CLICK, KEYU) && !scr_on) scr_on = true;   /* stock click-to-wake */
  wait_ms(hold);
}
#define CW  (+1 * FE_ENCODER_SIGN)   /* raw device delta for a clockwise detent */
#define CCW (-1 * FE_ENCODER_SIGN)
#define HOP 560

int main(int argc, char **argv) {
  if (argc < 4) { fprintf(stderr, "usage: %s regular.ttf bold.ttf outdir\n", argv[0]); return 2; }
  snprintf(g_out, sizeof g_out, "%s", argv[3]);
  char sd[600]; snprintf(sd, sizeof sd, "%s/stills", g_out); mkdirs(sd);
  load(argv[1], &FS.ttf[0], &FS.len[0]);
  load(argv[2], &FS.ttf[1], &FS.len[1]);

  lv_init();
  lv_tick_set_cb(tick_cb);
  lv_display_t *disp = lv_display_create(W, H);
  lv_display_set_color_format(disp, LV_COLOR_FORMAT_XRGB8888);
  lv_display_set_buffers(disp, fb, NULL, sizeof fb, LV_DISPLAY_RENDER_MODE_DIRECT);
  lv_display_set_flush_cb(disp, flush_cb);
  lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), 0);
  lv_obj_set_style_bg_opa(lv_screen_active(), LV_OPA_COVER, 0);

  D = fpl_init(font_get, &FS);
  if (!D) return 1;
  fpl_set_time_page(build_time());
  static const frontend_port PORT = { &IN, &SCR, &SET, &APPS, &AI, 0 };
  frontend_port port = PORT; port.display = D;

  static sg_app nav   = { "com.spare.nav",      PV_NAV, 0, our_start, our_stop, (void *)PV_NAV, 0 };
  static sg_app music = { "com.spare.music",    PV_MUSIC, 0, our_start, our_stop, (void *)PV_MUSIC, 0 };
  static sg_app read  = { "com.spare.reader",   PV_READER, 0, our_start, our_stop, (void *)PV_READER, 0 };
  static sg_app pomo  = { "com.spare.pomodoro", PV_POMO, 0, our_start, our_stop, (void *)PV_POMO, 0 };
  if (!fe_runtime_init(&port) || !KEY || !ENC) { fprintf(stderr, "runtime init failed\n"); return 1; }
  sg_app_register(&nav); sg_app_register(&music); sg_app_register(&read); sg_app_register(&pomo);

  wait_ms(100); snap("a_time");

  /* --- time -> control center, select, adjust brightness, toggle DND --- */
  rec("slide_time_to_cc");
  wait_ms(200);
  rot(CCW, 700); snap("b_cc_browse");
  click(600);    snap("b_cc_focus_back");
  rot(CW, HOP);  snap("b_cc_focus_bright");
  click(500);
  rot(CW, 220); rot(CW, 220); rot(CW, 400); snap("b_cc_edit_bright");
  click(500);
  rot(CW, HOP); rot(CW, HOP); rot(CW, HOP); snap("b_cc_focus_dnd");
  click(500);    snap("b_cc_dnd_on");
  rot(CW, HOP); rot(CW, HOP); snap("b_cc_focus_battery");
  rec_end();
  rec("cc_back_to_time");
  rot(CW, HOP);                /* wraps to 返回 */
  click(800);
  rec_end();

  /* --- time -> apps, bubble focus, page slide (6 stock + 4 ours = 2 screens) --- */
  rec("slide_time_to_apps");
  wait_ms(200);
  rot(CW, 800); snap("c_apps");
  rec_end();
  rec("app_focus");
  rot(CW, HOP); snap("c_apps_focus1");
  rot(CW, HOP); rot(CW, HOP); snap("c_apps_focus3");
  rot(CW, HOP); snap("c_apps_focus4");
  rot(CW, HOP); rot(CCW, HOP); rot(CCW, HOP); rot(CCW, HOP);
  rec_end();
  rot(CW, HOP); rot(CW, HOP); rot(CW, HOP);          /* back on slot 5 */
  rec("app_page_slide");
  wait_ms(200);
  rot(CW, 800); snap("c_apps_p2");
  rot(CW, HOP); snap("c_apps_p2_focus7");
  rot(CCW, HOP);
  rot(CCW, 800);                                      /* back to page 1, slot 5 */
  rec_end();
  for (int i = 0; i < 5; i++) rot(CCW, 100);
  wait_ms(600);
  rec("apps_to_time");
  rot(CCW, 800);
  rec_end();

  /* --- in-app card on our nav app (grid index 6, page 2) --- */
  rot(CW, 800);
  for (int i = 0; i < 6; i++) rot(CW, 120);
  wait_ms(800);
  click(400); snap("f_in_app");
  rec("card_pop");
  wait_ms(200);
  click(700); snap("d_card");
  rot(CW, HOP); snap("d_card_focus_cc");
  rot(CW, HOP); snap("d_card_focus_exit");
  rot(CCW, HOP); rot(CCW, HOP);
  click(600);                                         /* 返回: dismiss */
  rec_end();
  rec("card_to_cc");
  click(600); rot(CW, HOP);
  click(800); snap("b_cc_from_app");
  click(500); click(800);                             /* select, 返回 -> back to the app */
  rec_end();
  click(600); rot(CW, HOP); rot(CW, HOP);
  click(600);                                         /* 退出 -> time */

  /* --- screen off --- */
  rec("screen_off");
  wait_ms(300);
  click(600); snap("e_off");
  click(400);
  rec_end();

  printf("done\n");
  return 0;
}
