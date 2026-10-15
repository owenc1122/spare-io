/* End-to-end host test of fe_runtime.c + fe_pages.c + fe_state.c + fe_registry.c
 * through a mock frontend_port (no LVGL, no vendor). Verifies the integration
 * contract other modules rely on (music-v1/music_port.h):
 *   - SG_APP_HANDLES_CLICK == bit 0; such an app gets rotation/click forwarded,
 *     no global card;
 *   - sg_frontend_open_control_center(return_cb,user): Back calls return_cb;
 *   - sg_frontend_exit_app(): back to the time page;
 *   - icon == NULL for "com.spare.music" is filled with our SVG art;
 *   - long press opens the assistant from every page; screen off/on. */
#include "../frontend_port.h"
#include "../fe_pages.h"
#include "../sg_front.h"
#include "../../../sdk/sg_ctx.h"
#include "../assets/icons/sg_app_icons.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(SG_APP_HANDLES_CLICK == 1u, "music_port.h expects bit 0");

/* ---- SDK stubs used by the runtime ---- */
void *sg_calloc(size_t c, size_t s) { return calloc(c, s); }
void  sg_free(void *p) { free(p); }
void  sg_log(sg_log_level l, const char *t, const char *f, ...) { (void)l; (void)t; (void)f; }
/* host stand-in for the boot-owned context slots (device: hooks/hk_ctx.c) */
static void *ctx_slot[SG_CTX__COUNT];
void *sg_ctx_get(sg_ctx_id id) { return ctx_slot[id]; }
sg_err_t sg_ctx_set(sg_ctx_id id, void *c) { ctx_slot[id] = c; return SG_OK; }

/* ---- mock display (object table, anims run to completion immediately) ---- */
#define MAXO 1024
static struct { int used, parent, kind, vis; const uint8_t *buf; const char *txt; } O[MAXO];
static int live;
static fp_obj mk(fp_obj p, int k) { for (int i = 1; i < MAXO; i++) if (!O[i].used) { memset(&O[i], 0, sizeof O[i]); O[i].used = 1; O[i].parent = (int)(intptr_t)p; O[i].kind = k; O[i].vis = 1; live++; return (fp_obj)(intptr_t)i; } abort(); }
static void del_rec(int i) { for (int j = 1; j < MAXO; j++) if (O[j].used && O[j].parent == i) del_rec(j); O[i].used = 0; live--; }
#define U(o) assert(O[(int)(intptr_t)(o)].used)
static bool d_idle(void) { return true; }
static fp_obj d_root(void) { return 0; }
static fp_obj d_obj(fp_obj p) { return mk(p, 0); }
static fp_obj d_label(fp_obj p, unsigned px, fp_weight w) { (void)px; (void)w; return mk(p, 1); }
static fp_obj d_image(fp_obj p) { return mk(p, 3); }
static void d_ia8(fp_obj o, const uint8_t *a, unsigned w, unsigned h) { U(o); (void)a; (void)w; (void)h; }
static void d_after(uint32_t ms, void (*cb)(void *), void *u) { (void)ms; if (cb) cb(u); }
static void d_aopa(fp_obj o, uint8_t a, uint8_t b, uint32_t ms, fp_path p) { U(o); (void)a; (void)b; (void)ms; (void)p; }
static void d_atx(fp_obj o, int a, int b, uint32_t ms, fp_path p) { U(o); (void)a; (void)b; (void)ms; (void)p; }
static fp_obj d_canvas(fp_obj p) { return mk(p, 2); }
static void d_pos(fp_obj o, int x, int y, int w, int h) { U(o); (void)x; (void)y; (void)w; (void)h; }
static void d_rad(fp_obj o, int r) { U(o); (void)r; }
static void d_bg(fp_obj o, uint8_t g, uint8_t a) { U(o); (void)g; (void)a; }
static void d_border(fp_obj o, int w, uint8_t g) { U(o); (void)w; (void)g; }
static void d_tg(fp_obj o, uint8_t g) { U(o); (void)g; }
static void d_text(fp_obj o, const char *s) { U(o); O[(int)(intptr_t)o].txt = s; }
static void d_cbuf(fp_obj o, const uint8_t *b, unsigned w, unsigned h) { U(o); O[(int)(intptr_t)o].buf = b; (void)w; (void)h; }
static void d_opa(fp_obj o, uint8_t a) { U(o); (void)a; }
static void d_vis(fp_obj o, bool v) { U(o); O[(int)(intptr_t)o].vis = v; }
static void d_destroy(fp_obj o) { U(o); del_rec((int)(intptr_t)o); }
static void d_anim(void *var, void (*ex)(void *, int32_t), uint32_t ms) { ex(var, 0); ex(var, (int32_t)ms); }
static void d_stop(void *var) { (void)var; }
static uint32_t now_ms;
static uint32_t d_tick(void) { return now_ms; }
static const fp_display DISP = { .ui_idle = d_idle, .root = d_root, .obj = d_obj, .label = d_label, .canvas = d_canvas,
  .pos_size = d_pos, .radius = d_rad, .bg = d_bg, .border = d_border, .text_gray = d_tg, .text = d_text,
  .canvas_buf = d_cbuf, .opa = d_opa, .visible = d_vis, .destroy = d_destroy, .anim = d_anim, .anim_stop = d_stop,
  .tick_ms = d_tick, .anim_opa = d_aopa, .after = d_after, .anim_translate_x = d_atx,
  .image = d_image, .image_a8 = d_ia8 };

/* ---- mock input: capture the hooks ---- */
static bool (*KEY)(fp_key, void *); static void *KEYU;
static void (*ENC)(int, void *);    static void *ENCU;
static bool hk(bool (*f)(fp_key, void *), void *u) { KEY = f; KEYU = u; return true; }
static bool he(void (*f)(int, void *), void *u) { ENC = f; ENCU = u; return true; }
static const fp_input IN = { hk, he };
static bool click(void) { return KEY(FP_KEY_CLICK, KEYU); }
static bool lpress(void) { return KEY(FP_KEY_LONG, KEYU); }
/* rot(+1) = one logical clockwise detent; the mock feeds the RAW device delta. */
static void raw(int d) { now_ms += 200; ENC(d, ENCU); }
static void rot(int d) { raw(d * FE_ENCODER_SIGN); }

/* ---- mock screen/settings/apps/assistant ---- */
static bool scr = true; static int acts;
static bool s_on(void) { return scr; }
static void s_set(bool on) { scr = on; }
static void s_act(void) { acts++; }
static const fp_screen SCR = { s_on, s_set, s_act };
static int bri = 9, vol = 50; static bool rai = true, dnd = false;
static int  g_b(void) { return bri; } static void s_b(int v, bool f) { (void)f; bri = v; }
static int  g_v(void) { return vol; } static void s_v(int v) { vol = v; }
static bool g_r(void) { return rai; } static void s_r(bool v) { rai = v; }
static bool g_d(void) { return dnd; } static void s_d(bool v) { dnd = v; }
static bool g_bt(void) { return true; } static int g_bat(void) { return 82; } static bool g_ch(void) { return false; }
static const fp_settings SET = { g_b, s_b, g_v, s_v, g_r, s_r, g_d, s_d, g_bt, g_bat, g_ch };
static const char *const STOCK[6] = { "录音", "实时字幕", "实时提示", "待办", "提词器", "全天智记" };
static int launched = -1, homes; static const char *fg;
static int  a_n(void) { return 6; }
static const char *a_lab(int i) { return STOCK[i]; }
static bool a_icon(int i, uint8_t *dst, unsigned box) { memset(dst, i * 10, box * box); return true; }
static void a_launch(int i) { launched = i; fg = "stock"; }
static void a_home(void) { homes++; fg = 0; }
static const char *a_fg(void) { return fg; }
static const fp_apps APPS = { a_n, a_lab, a_icon, a_launch, a_home, a_fg };
static int ai_opens;
static void ai_open(void) { ai_opens++; } static void ai_close(void) {} static bool ai_is(void) { return false; }
static const fp_assistant AI = { ai_open, ai_close, ai_is };
static const frontend_port PORT = { &IN, &SCR, &SET, &APPS, &AI, &DISP };

/* ---- a music-like self-handling app ---- */
static int m_start, m_stop, m_rot, m_click, m_cc_back;
static void m_on_rot(int32_t d, void *u) { assert(u == &m_rot); m_rot += d; }
static void m_on_click(void *u) { assert(u == &m_rot); m_click++; }
static sg_err_t mus_start(sg_lv_obj p, void *u) { (void)p; (void)u; m_start++; sg_front_set_app_input(m_on_rot, m_on_click, &m_rot); return SG_OK; }
static sg_err_t mus_stop(void *u) { (void)u; m_stop++; return SG_OK; }
static void cc_returned(void *u) { (void)u; m_cc_back++; }
static sg_err_t nav_start(sg_lv_obj p, void *u) { (void)p; (void)u; return SG_OK; }
static sg_err_t nav_stop(void *u) { (void)u; return SG_OK; }

int main(void) {
  sg_app music = { "com.spare.music", "音乐", 0, mus_start, mus_stop, 0, SG_APP_HANDLES_CLICK };
  sg_app nav   = { "com.spare.nav",   "导航", 0, nav_start, nav_stop, 0, 0 };
  assert(sg_app_register(&nav) == SG_ERR_AGAIN);   /* no front-end context yet */
  assert(fe_runtime_init(&PORT) && KEY && ENC);
  assert(sg_app_register(&nav) == SG_OK && sg_app_register(&music) == SG_OK);

  /* time: click = screen off (consumed); off: click passes to stock wake */
  assert(click() && !scr);
  assert(!click());                 /* stock wakes the panel */
  scr = true;                       /* (stock did it) */

  /* Device sign (owner-verified): raw +1 is a counter-clockwise detent, so a raw
   * +1 from the time page opens the Control Center and raw -1 the app list. */
  assert(FE_ENCODER_SIGN == -1);
  int base = live;
  raw(+1); assert(live > base);     /* CC (browse) */
  raw(-1); assert(live == base);    /* rotate back closes it */

  /* CW -> app grid: 6 stock + nav + music = 8 entries, 2 screens */
  rot(+1);
  assert(live > base);
  /* music is grid index 7 (page 2); its icon comes from our SVG art */
  int found = 0;
  for (int i = 1; i < MAXO; i++) if (O[i].used && O[i].kind == 2 && O[i].buf && !memcmp(O[i].buf, sg_icon_nav, sizeof sg_icon_nav)) found = 1;
  assert(!found);                   /* page 1 shows stock apps only */
  for (int i = 0; i < 6; i++) rot(+1);          /* focus 6 = nav (page 2) */
  for (int i = 1; i < MAXO; i++) if (O[i].used && O[i].kind == 2 && O[i].buf && !memcmp(O[i].buf, sg_icon_music, sizeof sg_icon_music)) found = 1;
  assert(found);                    /* icon == NULL filled by id */
  rot(+1);                          /* focus 7 = music */
  assert(click() && m_start == 1);  /* launched, grid torn down */
  assert(live == base);

  /* HANDLES_CLICK: no card; click + rotation forwarded */
  assert(click() && m_click == 1);
  rot(+1); rot(-1); rot(-1);
  assert(m_rot == -1);
  int before = live;
  /* music opens the control center; Back calls its return_cb */
  assert(sg_frontend_open_control_center(cc_returned, 0) == SG_OK && live > before);
  assert(click());                  /* take focus (Back) */
  assert(click() && m_cc_back == 1 && live == before);
  assert(click() && m_click == 2);  /* still forwarding after CC round trip */
  /* long press from inside the app -> assistant */
  assert(lpress() && ai_opens == 1);
  /* music exits itself */
  assert(sg_frontend_exit_app() == SG_OK && m_stop == 1 && homes == 1);

  /* stock app keeps the 3-button card; Exit -> home */
  rot(+1); assert(click() && launched == 0);
  assert(click());                  /* card */
  assert(live > base);
  rot(-1);                          /* wrap to 退出 */
  assert(click() && homes == 2 && live == base);

  /* CCW -> control center; brightness slider adjust persists through port */
  rot(-1); assert(click());          /* select */
  rot(+1); assert(click());          /* adjust brightness */
  rot(+1); rot(+1); assert(bri == 11);
  assert(click());                   /* done */
  rot(+1); rot(+1); assert(click() && rai == false);   /* raise-to-wake toggled */
  /* long press works from the control center too */
  assert(lpress() && ai_opens == 2);
  printf("fe_runtime: music contract, grid icons by id, card/CC/exit, settings, AI passed\n");
  return 0;
}
