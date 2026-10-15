/* Host test for fe_pages.c through a recording mock fp_display (no LVGL).
 * Checks object lifecycle (no leaks, subtree delete), SPEC geometry, bubble
 * staying inside the safe area during every spring jump, page slide + dots,
 * critically damped page curve, and card/CC focus. -fsanitize=address,undefined. */
#include "../fe_pages.h"
#include "../fe_spring.h"
#include "../fe_strings.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../assets/glyphs/sg_glyphs.h"
#define SAFE_RIGHT 536   /* fe_pages.c SAFE_X1 */

/* ---------------- mock display ---------------- */
#define MAXO 512
typedef struct { int used, parent, x, y, w, h, r, kind, visible, scale, tx, align, clip, weight; uint8_t bg, bgopa, bw, bgray, tg, opa; const char *txt; const uint8_t *a8; } MObj;
static MObj O[MAXO];
static int live;
static struct { void *var; void (*exec)(void *, int32_t); uint32_t ms; } A[16];

static int idx(fp_obj o) { return (int)(intptr_t)o; }
static fp_obj mk(fp_obj parent, int kind) {
  for (int i = 1; i < MAXO; i++) if (!O[i].used) {
    memset(&O[i], 0, sizeof O[i]); O[i].used = 1; O[i].parent = idx(parent); O[i].kind = kind; O[i].visible = 1; O[i].opa = 255; O[i].scale = 256;
    live++; return (fp_obj)(intptr_t)i;
  }
  assert(!"mock full"); return 0;
}
static bool m_idle(void) { return true; }
static fp_obj m_root(void) { return (fp_obj)(intptr_t)0; }
static fp_obj m_obj(fp_obj p) { return mk(p, 0); }
static fp_obj m_label(fp_obj p, unsigned px, fp_weight w) { (void)px; fp_obj o = mk(p, 1); O[idx(o)].weight = (int)w; return o; }
static fp_obj m_canvas(fp_obj p) { return mk(p, 2); }
static void m_pos(fp_obj o, int x, int y, int w, int h) { MObj *m = &O[idx(o)]; assert(m->used); m->x = x; m->y = y; m->w = w; m->h = h; }
static void m_rad(fp_obj o, int r) { assert(O[idx(o)].used); O[idx(o)].r = r; }
static void m_bg(fp_obj o, uint8_t g, uint8_t a) { assert(O[idx(o)].used); O[idx(o)].bg = g; O[idx(o)].bgopa = a; }
static void m_border(fp_obj o, int w, uint8_t g) { assert(O[idx(o)].used); O[idx(o)].bw = (uint8_t)w; O[idx(o)].bgray = g; }
static void m_tg(fp_obj o, uint8_t g) { assert(O[idx(o)].used && O[idx(o)].kind == 1); O[idx(o)].tg = g; }
static int scale_tweens, opa_tweens, tx_tweens, afters;
static void m_scale(fp_obj o, int q) { assert(O[idx(o)].used); O[idx(o)].scale = q; }
static void m_ascale(fp_obj o, int a, int b, uint32_t ms, fp_path p) { assert(O[idx(o)].used && ms); (void)a; (void)p; O[idx(o)].scale = b; scale_tweens++; }
static void m_aopa(fp_obj o, uint8_t a, uint8_t b, uint32_t ms, fp_path p) { assert(O[idx(o)].used && ms); (void)a; (void)p; O[idx(o)].opa = b; opa_tweens++; }
static void m_after(uint32_t ms, void (*cb)(void *), void *u) { (void)ms; afters++; if (cb) cb(u); }
static int TP;  /* time page object index */
static fp_obj m_tp(void) { return (fp_obj)(intptr_t)TP; }
static void m_tx(fp_obj o, int px) { assert(O[idx(o)].used); O[idx(o)].tx = px; }
static void m_atx(fp_obj o, int a, int b, uint32_t ms, fp_path p) { assert(O[idx(o)].used && ms); (void)a; (void)p; O[idx(o)].tx = b; tx_tweens++; }
static void m_align(fp_obj o, fp_align a) { assert(O[idx(o)].kind == 1); O[idx(o)].align = (int)a; }
static void m_clip(fp_obj o, bool on) { assert(O[idx(o)].used); O[idx(o)].clip = on; }
static fp_obj m_image(fp_obj p) { return mk(p, 3); }
static void m_ia8(fp_obj o, const uint8_t *a, unsigned w, unsigned h) { assert(O[idx(o)].kind == 3 && a && w == h); O[idx(o)].a8 = a; }
static void m_igray(fp_obj o, uint8_t g) { assert(O[idx(o)].kind == 3); O[idx(o)].tg = g; }
static void m_text(fp_obj o, const char *s) { assert(O[idx(o)].used && O[idx(o)].kind == 1 && s); O[idx(o)].txt = s; }
static void m_cbuf(fp_obj o, const uint8_t *b, unsigned w, unsigned h) { assert(O[idx(o)].used && O[idx(o)].kind == 2); (void)b; (void)w; (void)h; }
static void m_opa(fp_obj o, uint8_t a) { assert(O[idx(o)].used); O[idx(o)].opa = a; }
static void m_vis(fp_obj o, bool v) { assert(O[idx(o)].used); O[idx(o)].visible = v; }
static void del_rec(int i) {
  for (int j = 1; j < MAXO; j++) if (O[j].used && O[j].parent == i) del_rec(j);
  O[i].used = 0; live--;
}
static void m_destroy(fp_obj o) { int i = idx(o); assert(i > 0 && O[i].used); del_rec(i); }
static void m_anim(void *var, void (*exec)(void *, int32_t), uint32_t ms) {
  for (int i = 0; i < 16; i++) if (A[i].var == var) { A[i].exec = exec; A[i].ms = ms; exec(var, 0); return; }
  for (int i = 0; i < 16; i++) if (!A[i].var) { A[i].var = var; A[i].exec = exec; A[i].ms = ms; exec(var, 0); return; }
  assert(!"anim full");
}
static void m_stop(void *var) { for (int i = 0; i < 16; i++) if (A[i].var == var) A[i].var = 0; }
static uint32_t m_tick(void) { return 0; }
static const fp_display D = { .ui_idle = m_idle, .root = m_root, .obj = m_obj, .label = m_label, .canvas = m_canvas,
  .pos_size = m_pos, .radius = m_rad, .bg = m_bg, .border = m_border, .text_gray = m_tg, .text = m_text,
  .canvas_buf = m_cbuf, .opa = m_opa, .visible = m_vis, .destroy = m_destroy, .anim = m_anim, .anim_stop = m_stop,
  .tick_ms = m_tick, .scale = m_scale, .anim_scale = m_ascale, .anim_opa = m_aopa, .after = m_after,
  .time_page = m_tp, .translate_x = m_tx, .anim_translate_x = m_atx, .text_align = m_align,
  .clip_corner = m_clip, .image = m_image, .image_a8 = m_ia8, .image_gray = m_igray };
/* a backend without any of the 2026-10-13 additions must still work */
static const fp_display D_OLD = { .ui_idle = m_idle, .root = m_root, .obj = m_obj, .label = m_label, .canvas = m_canvas,
  .pos_size = m_pos, .radius = m_rad, .bg = m_bg, .border = m_border, .text_gray = m_tg, .text = m_text,
  .canvas_buf = m_cbuf, .opa = m_opa, .visible = m_vis, .destroy = m_destroy, .anim = m_anim, .anim_stop = m_stop,
  .tick_ms = m_tick };

static int i_abs2(int a) { return a < 0 ? -a : a; }
/* absolute box of an object (sum of parent offsets + translate) */
static void abs_box(int i, int *x, int *y) {
  *x = 0; *y = 0;
  while (i > 0) { *x += O[i].x + O[i].tx; *y += O[i].y; i = O[i].parent; }
}
static void centre(fp_obj o, int *cx2, int *cy2) {           /* centre * 2 (exact) */
  int x, y; abs_box(idx(o), &x, &y);
  *cx2 = 2 * x + O[idx(o)].w; *cy2 = 2 * y + O[idx(o)].h;
}
/* bubble centre == icon centre (x) == label centre (x) == icon+label block centre (y), +-1 px */
static void check_grid_centring(FEAppsView *g, int slot) {
  FEGridPane *p = &g->pane[g->cur];
  int bx, by, ix, iy, lx, ly, ax, ay, bx0, by0;
  centre(g->bubble.obj, &bx, &by); centre(p->icon[slot], &ix, &iy); centre(p->label[slot], &lx, &ly);
  abs_box(idx(p->icon[slot]), &ax, &ay);
  abs_box(idx(p->label[slot]), &bx0, &by0);
  int block_top = ay, block_bot = by0 + O[idx(p->label[slot])].h;
  assert(i_abs2(bx - ix) <= 2 && i_abs2(bx - lx) <= 2);
  assert(i_abs2(by - (block_top + block_bot)) <= 2);
  /* the whole block sits inside the bubble */
  int bxa, bya; abs_box(idx(g->bubble.obj), &bxa, &bya);
  assert(block_top >= bya && block_bot <= bya + O[idx(g->bubble.obj)].h);
}

/* run every pending anim to completion, sampling each frame with `check` */
static void run_anims(void (*check)(void *var, uint32_t e)) {
  for (int guard = 0; guard < 8; guard++) {
    int any = 0;
    for (int i = 0; i < 16; i++) if (A[i].var) {
      void *var = A[i].var; void (*ex)(void *, int32_t) = A[i].exec; uint32_t ms = A[i].ms; any = 1;
      for (uint32_t e = 0; e <= ms; e += 10) { ex(var, (int32_t)e); if (check) check(var, e); if (A[i].var != var) break; }
      if (A[i].var == var) { ex(var, (int32_t)ms); A[i].var = 0; }
    }
    if (!any) return;
  }
}

static FEAppsView G;
static void bubble_in_safe(void *var, uint32_t e) {
  (void)e;
  if (var != &G.bubble) return;
  FERect r = G.bubble.cur;
  assert(r.x - r.w / 2 >= 4 && r.x + r.w / 2 <= 536);   /* SPEC: outer edge x 6..534 measured */
  assert(r.y - r.h / 2 >= 3 && r.y + r.h / 2 <= 205);   /* above the page dots */
}

int main(void) {
  /* geometry per SPEC */
  FERect c0 = fe_grid_cell_rect(0), c5 = fe_grid_cell_rect(5);
  assert(c0.x == 90 && c0.y == 52 && c0.w == 92 && c0.h == 92 && c0.r == 23);   /* square bubble */
  assert(c5.x == 450 && c5.y == 152);
  assert(fe_cc_tile_rect(FE_CC_BRIGHT).h == 188 && fe_card_btn_rect(1).x == 270);
  /* CC is left of the time page: Back is the right-most tile at the right edge,
   * every other tile is left of it, no two tiles (with focus ring) overlap, and
   * the info panel (x 4..148) is clear of all tiles. */
  {
    FERect b = fe_cc_tile_rect(FE_CC_BACK);
    assert(b.x + b.w / 2 + 5 <= SAFE_RIGHT && b.x > FE_SCR_W - 60);
    for (int i = 0; i < FE_CC_N; i++) {
      FERect a = fe_cc_tile_rect(i);
      if (i != FE_CC_BACK) assert(a.x + a.w / 2 < b.x - b.w / 2);
      assert(a.x - a.w / 2 - 5 > 148);
      for (int j = i + 1; j < FE_CC_N; j++) {
        FERect c = fe_cc_tile_rect(j);
        bool sep = abs(a.x - c.x) * 2 >= a.w + c.w + 10 || abs(a.y - c.y) * 2 >= a.h + c.h + 10;
        assert(sep);
      }
    }
  }
  /* page curve: monotonic, no overshoot, ends at 1024 */
  int32_t prev = -1;
  for (uint32_t e = 0; e <= SG_PAGE_MS + 40; e += 5) { int32_t p = fe_page_curve_q10(e); assert(p >= prev && p <= 1024); prev = p; }
  assert(fe_page_curve_q10(SG_PAGE_MS) == 1024);
  /* follow weight */
  assert(fe_follow_k256(0, 0, 180, 100) == 256 && fe_follow_k256(180, 0, 180, 100) == 0);

  /* ---- app grid: 10 entries -> 2 screens ---- */
  static uint8_t icon[FE_ICON_BOX * FE_ICON_BOX];
  FEGridEntry ent[10];
  for (int i = 0; i < 10; i++) { ent[i].label = "应用"; ent[i].icon_l8 = (i % 3) ? icon : 0; }
  assert(fe_apps_build(&D, m_root(), &G, ent, 10, 0));
  int base = live;
  assert(G.pages == 2 && G.page == 0);
  /* every same-screen jump keeps the bubble inside the safe area */
  for (int from = 0; from < 6; from++) for (int to = 0; to < 6; to++) {
    if (from == to) continue;
    fe_apps_focus(&G, from); run_anims(0);
    fe_apps_focus(&G, to); run_anims(bubble_in_safe);
    FERect r = G.bubble.cur, t = fe_grid_cell_rect(to);
    assert(r.x == t.x && r.y == t.y && r.w == t.w && r.h == t.h);   /* settles exactly */
  }
  assert(live == base);                                   /* no growth from moves */
  for (int s = 0; s < 6; s++) { fe_apps_focus(&G, s); run_anims(0); check_grid_centring(&G, s); }
  /* focused icon is bright, others dim (follow k) */
  fe_apps_focus(&G, 4); run_anims(0);
  assert(O[idx(G.pane[G.cur].icon[4])].opa == 255 && O[idx(G.pane[G.cur].icon[0])].opa == 150);
  /* page slide 5 -> 6: incoming pane enters from the right, old one leaves */
  fe_apps_focus(&G, 5); run_anims(0);
  fe_apps_focus(&G, 6);
  assert(G.slide_dir == 1 && G.pane[G.cur ^ 1].root);
  fe_apps_slide_frame(&G, SG_PAGE_MS / 2);
  int xo = O[idx(G.pane[G.cur].root)].x, xn = O[idx(G.pane[G.cur ^ 1].root)].x;
  assert(xo < 0 && xn > 0 && xn - xo == FE_SCR_W);       /* panes move together */
  run_anims(0);
  assert(G.slide_dir == 0 && G.page == 1 && !G.pane[G.cur ^ 1].root);
  assert(O[idx(G.pane[G.cur].root)].x == 0);
  assert(O[idx(G.dot[1])].w == 16 && O[idx(G.dot[0])].w == 6);   /* capsule on page 2 */
  FERect b = G.bubble.cur, s0 = fe_grid_cell_rect(0);
  assert(b.x == s0.x && b.y == s0.y);                     /* focus = first of new page */
  /* reverse slide 6 -> 5 lands on the last slot of page 1 */
  fe_apps_focus(&G, 5); run_anims(0);
  assert(G.page == 0 && G.bubble.cur.x == fe_grid_cell_rect(5).x);
  /* input mid-slide finishes the slide first */
  fe_apps_focus(&G, 6); fe_apps_slide_frame(&G, 100); fe_apps_focus(&G, 7); run_anims(0);
  assert(G.page == 1 && G.slide_dir == 0 && G.bubble.cur.x == fe_grid_cell_rect(1).x);
  for (int s = 6; s < 10; s++) { fe_apps_focus(&G, s); run_anims(0); check_grid_centring(&G, s % 6); }
  /* focused icon scaled x1.06, neighbours 1.0 */
  assert(O[idx(G.pane[G.cur].icon[3])].scale == 271 && O[idx(G.pane[G.cur].icon[0])].scale == 256);
  assert(O[idx(G.pane[G.cur].label[0])].weight == FP_W_BOLD);
  fe_apps_free(&G);
  assert(live == 0);

  /* ---- page transitions + screen-off fade on the stock watch face ---- */
  TP = idx(mk(0, 0)); int tp_live = live;
  fp_obj pg = mk(0, 0);
  fe_page_enter(&D, pg, -1);                              /* CC from the left */
  assert(O[idx(pg)].tx == 0 && O[TP].tx == 540 && O[TP].opa == 102);
  static int left_done; left_done = 0;
  fe_page_leave(&D, pg, -1, (void (*)(void *))(void *)0, 0);
  assert(O[TP].tx == 0 && O[TP].opa == 255 && O[idx(pg)].tx == -540);
  m_destroy(pg); (void)left_done;
  fe_screen_off_fade(&D, 0, 0);
  assert(O[TP].opa == 0 && O[TP].scale == 246);
  fe_time_restore(&D);
  assert(O[TP].opa == 255 && O[TP].scale == 256 && O[TP].tx == 0);
  m_destroy((fp_obj)(intptr_t)TP); TP = 0; assert(live == tp_live - 1);

  /* ---- control center ---- */
  FECcView cc; memset(&cc, 0, sizeof cc);
  FECcValues val = { 9, 60, 82, true, false, true, false, false };
  assert(fe_cc_build(&D, m_root(), &cc));
  fe_cc_update(&cc, 0, false, false, &val);
  assert(!O[idx(cc.ring.obj)].visible && !strcmp(O[idx(cc.info_title)].txt, FE_S_CC));
  for (int f = 0; f < FE_CC_N; f++) { fe_cc_update(&cc, f, true, false, &val); run_anims(0);
    FERect t = fe_cc_tile_rect(f); assert(cc.ring.cur.x == t.x && cc.ring.cur.w == t.w + 10); }
  fe_cc_update(&cc, FE_CC_BRIGHT, true, true, &val); run_anims(0);
  assert(!strcmp(O[idx(cc.info_value)].txt, "53%") && O[idx(cc.ring.obj)].bw == 3);
  assert(O[idx(cc.tile[FE_CC_RAISE])].bg == 190 && O[idx(cc.tile[FE_CC_DND])].bg == 46);
  assert(!strcmp(O[idx(cc.sub[FE_CC_BATTERY])].txt, "82%"));
  /* real glyph images, tinted; info panel left-aligned; slider flat-top fill */
  for (int i = 0; i < FE_CC_N; i++) assert(O[idx(cc.glyph[i])].kind == 3 && O[idx(cc.glyph[i])].a8);
  assert(!memcmp(O[idx(cc.glyph[FE_CC_BACK])].a8, sg_glyph_forward_28, sizeof sg_glyph_forward_28));   /* right-pointing chevron */
  assert(O[idx(cc.glyph[FE_CC_RAISE])].tg == 10 && O[idx(cc.glyph[FE_CC_DND])].tg == 220);
  assert(O[idx(cc.info_title)].align == FP_ALIGN_LEFT && O[idx(cc.info_value)].align == FP_ALIGN_LEFT);
  assert(O[idx(cc.tile[FE_CC_BRIGHT])].clip && O[idx(cc.fill[FE_CC_BRIGHT])].r == 0);
  assert(O[idx(cc.tile[FE_CC_BRIGHT])].scale == 269);      /* selection x1.05 */
  /* every focus ring is centred on its tile */
  for (int f = 0; f < FE_CC_N; f++) {
    fe_cc_update(&cc, f, true, false, &val); run_anims(0);
    int rx, ry, tx2, ty2; centre(cc.ring.obj, &rx, &ry); centre(cc.tile[f], &tx2, &ty2);
    assert(i_abs2(rx - tx2) <= 2 && i_abs2(ry - ty2) <= 2);
  }
  assert(O[idx(cc.tile[FE_CC_BRIGHT])].scale == 256);      /* back to 1.0 */
  fe_cc_free(&cc);
  assert(live == 0);

  /* ---- card ---- */
  FECardView cd; memset(&cd, 0, sizeof cd);
  assert(fe_card_build(&D, m_root(), &cd));
  assert(O[idx(cd.btn[0])].bg == 205 && O[idx(cd.btn[1])].bg == 62);   /* Back default */
  fe_card_focus(&cd, 2); run_anims(0);
  assert(cd.ring.cur.x == 385 && O[idx(cd.btn[2])].bg == 205 && O[idx(cd.glyph[2])].tg == 12);
  assert(O[idx(cd.pop)].scale == 256 && O[idx(cd.pop)].opa == 255 && O[idx(cd.scrim)].opa == 178);  /* popped in */
  assert(O[idx(cd.btn[2])].scale == 271 && O[idx(cd.btn[0])].scale == 256);
  for (int i = 0; i < 3; i++) {                           /* ring + glyph centred on each button */
    fe_card_focus(&cd, i); run_anims(0);
    int rx, ry, bx2, by2, gx, gy; centre(cd.ring.obj, &rx, &ry); centre(cd.btn[i], &bx2, &by2); centre(cd.glyph[i], &gx, &gy);
    assert(i_abs2(rx - bx2) <= 2 && i_abs2(ry - by2) <= 2 && i_abs2(gx - bx2) <= 2 && i_abs2(gy - by2) <= 2);
  }
  fe_card_close(&cd, 0, 0);
  assert(O[idx(cd.pop)].opa == 0 && O[idx(cd.scrim)].opa == 0);
  fe_card_free(&cd);
  assert(live == 0);

  /* ---- an old backend without the new facade entries still works ---- */
  { FEAppsView g2; memset(&g2, 0, sizeof g2); FECcView c2; memset(&c2, 0, sizeof c2); FECardView k2; memset(&k2, 0, sizeof k2);
    assert(fe_apps_build(&D_OLD, m_root(), &g2, ent, 10, 0)); fe_apps_focus(&g2, 1); run_anims(0); fe_apps_free(&g2);
    assert(fe_cc_build(&D_OLD, m_root(), &c2)); fe_cc_update(&c2, 1, true, true, &val); run_anims(0); fe_cc_free(&c2);
    assert(fe_card_build(&D_OLD, m_root(), &k2)); fe_card_close(&k2, 0, 0); fe_card_free(&k2);
    assert(live == 0); }
  assert(scale_tweens && opa_tweens && tx_tweens && afters);
  printf("fe_pages: geometry, lifecycle, safe-area springs, page slide, CC, card passed\n");
  return 0;
}
