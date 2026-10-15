/* fe_pages — front-end pages at 540x220 via fp_display. See fe_pages.h.
 * Numbers: prototypes/frontend-preview/out/SPEC.md (3x2 grid, 540x220) with the
 * owner's 2026-10-13 change: compact square focus bubble (92x92 r23) centred on
 * the cell, icon+label block vertically centred inside it.
 * Clean-room, Spare original. No libc, no file-scope mutable state. Every
 * optional facade entry is NULL-checked so older backends still render. */
#include "fe_pages.h"
#include "fe_spring.h"
#include "fe_strings.h"
#include "assets/glyphs/sg_glyphs.h"

/* ============================ layout ===================================== */
static const int16_t GRID_CX[FE_GRID_COLS] = { 90, 270, 450 };
static const int16_t GRID_CY[FE_GRID_ROWS] = { 52, 152 };
#define GRID_BUB_W 92
#define GRID_BUB_H 92
#define GRID_BUB_R 23
/* icon (44) + 4 gap + label box (22) = 70 px block, centred on the cell:
 * block top = cy-35, icon centre = cy-13, label centre = cy+24, bottom = cy+35 */
#define GRID_ICON_CY (-13)
#define GRID_LABEL_CY 24
#define GRID_LABEL_H 22
#define GRID_LABEL_W 96
#define GRID_DOT_Y 211
#define GRID_DOT_PITCH 14
#define GRID_FOLLOW_RX 180
#define GRID_FOLLOW_RY 100

/* bubble safe area: stays clear of the screen edge and the page dots (y>=208) */
#define SAFE_X0 4
#define SAFE_X1 536
#define SAFE_Y0 3
#define SAFE_Y1 204

/* control center: cx, cy, w, h, r. The CC lives to the LEFT of the time page
 * (CCW from time), so the layout is mirrored: Back sits on the RIGHT edge with
 * a right-pointing chevron toward the time page, the sliders and toggle grid
 * fan out leftwards from it (focus order walks away from Back), and the info
 * panel takes the left edge. */
static const int16_t CC_GEO[FE_CC_N][5] = {
  { 498, 110, 56,  56, 28 },   /* 返回     */
  { 422, 110, 64, 188, 22 },   /* 亮度     */
  { 346, 110, 64, 188, 22 },   /* 音量     */
  { 270,  70, 64,  64, 20 },   /* 抬头亮屏 */
  { 198,  70, 64,  64, 20 },   /* 勿扰模式 */
  { 270, 150, 64,  64, 20 },   /* 蓝牙     */
  { 198, 150, 64,  64, 20 },   /* 电池     */
};
static const char *const CC_TITLE[FE_CC_N] = FE_S_CC_TITLES;
#define CC_INFO_X 4
#define CC_INFO_W 144
#define CC_RING_GROW 5
#define CC_SEL_SCALE 269         /* x1.05 (256 == 1.0) */

/* card */
#define CARD_CX 270
#define CARD_CY 110
#define CARD_W 420
#define CARD_H 156
#define CARD_R 34
static const int16_t CARD_BTN_X[3] = { 155, 270, 385 };
#define CARD_BTN_Y 94
#define CARD_BTN_D 70
#define CARD_RING_D 80
#define CARD_LABEL_Y 160
#define CARD_FOLLOW_RX 115
#define CARD_DIM 178             /* backdrop: app at ~30% */
static const char *const CARD_LABEL[3] = FE_S_CARD;
static const char *const CARD_FALLBACK[3] = { "<", "::", "X" };

/* L8 grays (panel shows them as monochrome green) */
#define G_TILE 46
#define G_ON 190
#define G_FILL 195
#define G_FILL_SEL 235
#define G_RING 240

/* critically damped page slide (zeta = 1, no overshoot), Q10 over SG_PAGE_MS */
static const int16_t PAGE_CD[65] = {
  0, 9, 34, 69, 112, 161, 212, 265, 318, 370, 421, 469, 516, 559, 600, 639,
  674, 707, 737, 765, 791, 814, 835, 854, 872, 888, 902, 915, 927, 937, 947,
  955, 963, 969, 976, 981, 986, 990, 994, 998, 1001, 1004, 1006, 1008, 1010,
  1012, 1013, 1015, 1016, 1017, 1018, 1019, 1020, 1020, 1021, 1021, 1022,
  1022, 1023, 1023, 1023, 1023, 1024, 1024, 1024
};

/* ============================ helpers ==================================== */
static int i_min(int a, int b) { return a < b ? a : b; }
static int i_max(int a, int b) { return a > b ? a : b; }
static int i_abs(int a) { return a < 0 ? -a : a; }
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static uint32_t isqrt32(uint32_t n) {
  uint32_t r = 0, b = 1u << 30;
  while (b > n) b >>= 2;
  while (b) { if (n >= r + b) { n -= r + b; r = (r >> 1) + b; } else r >>= 1; b >>= 2; }
  return r;
}

static void place_rect(const fp_display *d, fp_obj o, FERect r) {
  d->pos_size(o, r.x - r.w / 2, r.y - r.h / 2, r.w, r.h);
  d->radius(o, r.r);
}
static void put_box(const fp_display *d, fp_obj o, int cx, int cy, int w, int h) {
  d->pos_size(o, cx - w / 2, cy - h / 2, w, h);
}
static FERect grow(FERect r, int px) {
  r.w = (int16_t)(r.w + 2 * px); r.h = (int16_t)(r.h + 2 * px); r.r = (int16_t)(r.r + px);
  return r;
}
static char *fmt_pct(char *buf, int n) {        /* "NN%" (n clamped 0..100) */
  char t[4]; int k = 0, i = 0;
  n = clampi(n, 0, 100);
  do { t[k++] = (char)('0' + n % 10); n /= 10; } while (n && k < 3);
  while (k) buf[i++] = t[--k];
  buf[i++] = '%'; buf[i] = 0;
  return buf;
}
static void set_scale(const fp_display *d, fp_obj o, int q8) { if (d->scale) d->scale(o, q8); }
static void left(const fp_display *d, fp_obj o) { if (d->text_align) d->text_align(o, FP_ALIGN_LEFT); }

/* glyph: real A8 image when the backend has one, else a text fallback */
static fp_obj glyph_make(const fp_display *d, fp_obj parent, const uint8_t *a8, unsigned px,
                         const char *fallback, bool *is_img) {
  fp_obj o = 0;
  *is_img = false;
  if (d->image && d->image_a8 && (o = d->image(parent))) { d->image_a8(o, a8, px, px); *is_img = true; return o; }
  if ((o = d->label(parent, px, FP_W_BOLD))) d->text(o, fallback);
  return o;
}
static void glyph_gray(const fp_display *d, fp_obj o, bool is_img, uint8_t g) {
  if (is_img) { if (d->image_gray) d->image_gray(o, g); }
  else d->text_gray(o, g);
}

/* ============================ geometry =================================== */
FERect fe_grid_cell_rect(int slot) {
  FERect r;
  slot = clampi(slot, 0, FE_GRID_PER_PAGE - 1);
  r.x = GRID_CX[slot % FE_GRID_COLS]; r.y = GRID_CY[slot / FE_GRID_COLS];
  r.w = GRID_BUB_W; r.h = GRID_BUB_H; r.r = GRID_BUB_R;
  return r;
}
FERect fe_cc_tile_rect(int item) {
  FERect r = { 0, 0, 0, 0, 0 };
  if (item < 0 || item >= FE_CC_N) return r;
  r.x = CC_GEO[item][0]; r.y = CC_GEO[item][1];
  r.w = CC_GEO[item][2]; r.h = CC_GEO[item][3]; r.r = CC_GEO[item][4];
  return r;
}
FERect fe_card_btn_rect(int idx) {
  FERect r;
  idx = clampi(idx, 0, 2);
  r.x = CARD_BTN_X[idx]; r.y = CARD_BTN_Y; r.w = CARD_BTN_D; r.h = CARD_BTN_D; r.r = CARD_BTN_D / 2;
  return r;
}

int32_t fe_page_curve_q10(uint32_t e) { return sg_spring_q10(PAGE_CD, e, SG_PAGE_MS); }

int32_t fe_path_q10(fp_path path, uint32_t e, uint32_t ms) {
  if (!ms || e >= ms) return 1024;
  int32_t t = (int32_t)((uint64_t)e * 1024 / ms), u = 1024 - t;
  switch (path) {
  case FP_PATH_EASE_OUT:      return 1024 - (int32_t)((int64_t)u * u / 1024 * u / 1024);
  case FP_PATH_EASE_OUT_QUAD: return 1024 - u * u / 1024;
  case FP_PATH_SMOOTHSTEP:    return (int32_t)((int64_t)t * t / 1024 * (3072 - 2 * t) / 1024);
  case FP_PATH_SPRING_MOVE:   return sg_spring_q10(SG_SPRING_MOVE, e, ms);
  case FP_PATH_SPRING_POP:    return sg_spring_q10(SG_SPRING_POP, e, ms);
  case FP_PATH_PAGE_CD:       return sg_spring_q10(PAGE_CD, e, ms);
  case FP_PATH_LINEAR: default: return t;
  }
}

int fe_follow_k256(int dx, int dy, int rx, int ry) {
  int32_t a = (int32_t)dx * 256 / (rx ? rx : 1), b = (int32_t)dy * 256 / (ry ? ry : 1);
  int32_t h = (int32_t)isqrt32((uint32_t)(a * a + b * b));
  return clampi(256 - h, 0, 256);
}

/* overshoot cap for one axis: room between the target's outer edge and the
 * safe-area edge in the direction of travel, limited to SG_OVR_MAX_PX */
static int32_t axis_cap(int32_t from_c, int32_t to_c, int32_t half, int32_t lo, int32_t hi) {
  int32_t room = to_c >= from_c ? hi - (to_c + half) : (to_c - half) - lo;
  return clampi(room, 0, SG_OVR_MAX_PX);
}

/* Centre (x,y) follows the capped MOVE spring; w/h get the jelly stretch along
 * the motion and squash across it, applied symmetrically about that centre, so
 * the bubble stays re-centred while it deforms. */
FERect fe_bubble_at(const FERect *a, const FERect *b, uint32_t e) {
  FERect r;
  r.x = (int16_t)sg_move_channel_cap(a->x, b->x, e, axis_cap(a->x, b->x, b->w / 2, SAFE_X0, SAFE_X1));
  r.y = (int16_t)sg_move_channel_cap(a->y, b->y, e, axis_cap(a->y, b->y, b->h / 2, SAFE_Y0, SAFE_Y1));
  int32_t w = sg_move_channel(a->w, b->w, e), h = sg_move_channel(a->h, b->h, e);
  int32_t q = sg_spring_q10(SG_SPRING_PULSE, e, SG_PULSE_MS);
  int dx = i_abs(b->x - a->x), dy = i_abs(b->y - a->y), sum = dx + dy;
  if (sum > 0) {
    int32_t ux = (int32_t)dx * 1024 / sum, uy = (int32_t)dy * 1024 / sum;
    int32_t st = SG_JELLY_STRETCH_Q10 * q / 1024, sq = SG_JELLY_SQUASH_Q10 * q / 1024;
    w = w * (1024 + (st * ux - sq * uy) / 1024) / 1024;
    h = h * (1024 + (st * uy - sq * ux) / 1024) / 1024;
  }
  w = i_max((int)w, 8); h = i_max((int)h, 8);
  w &= ~1; h &= ~1;                                /* even sizes: exact centring */
  r.w = (int16_t)w; r.h = (int16_t)h;
  r.r = (int16_t)i_min((int)sg_move_channel(a->r, b->r, e), i_min((int)w, (int)h) / 2);
  return r;
}

/* ============================ bubble ===================================== */
void fe_bubble_frame(FEBubble *b, uint32_t e) {
  if (!b || !b->obj || !b->disp) return;
  b->cur = fe_bubble_at(&b->from, &b->to, e > SG_MOVE_MS ? SG_MOVE_MS : e);
  place_rect(b->disp, b->obj, b->cur);
  if (b->on_frame) b->on_frame(b->owner);
}
static void bubble_exec(void *var, int32_t e) { fe_bubble_frame((FEBubble *)var, (uint32_t)(e < 0 ? 0 : e)); }

static void bubble_snap(FEBubble *b, FERect to) {
  if (b->disp && b->disp->anim_stop) b->disp->anim_stop(b);
  b->from = b->to = b->cur = to;
  if (b->obj) place_rect(b->disp, b->obj, to);
  if (b->on_frame) b->on_frame(b->owner);
}
/* retarget from the live position (no queueing), MOVE spring + jelly */
static void bubble_go(FEBubble *b, FERect to) {
  const fp_display *d = b->disp;
  if (!d->anim) { bubble_snap(b, to); return; }
  if (d->anim_stop) d->anim_stop(b);
  b->from = b->cur;
  b->to = to;
  d->anim(b, bubble_exec, SG_MOVE_MS);
}

/* ============================ page transitions ========================== */
void fe_time_restore(const fp_display *d) {
  fp_obj tp = d->time_page ? d->time_page() : 0;
  if (!tp) return;
  if (d->anim_stop) d->anim_stop(tp);
  if (d->translate_x) d->translate_x(tp, 0);
  d->opa(tp, 255);
  set_scale(d, tp, 256);
}

void fe_page_enter(const fp_display *d, fp_obj page_root, int dir) {
  fp_obj tp = d->time_page ? d->time_page() : 0;
  if (!d->anim_translate_x) return;                    /* instant */
  d->anim_translate_x(page_root, dir * FE_SCR_W, 0, SG_PAGE_MS, FP_PATH_PAGE_CD);
  if (tp) {
    d->anim_translate_x(tp, 0, -dir * FE_SCR_W, SG_PAGE_MS, FP_PATH_PAGE_CD);
    if (d->anim_opa) d->anim_opa(tp, 255, 102, SG_PAGE_MS, FP_PATH_PAGE_CD);   /* 1 - 0.6p */
  }
}

void fe_page_leave(const fp_display *d, fp_obj page_root, int dir,
                   void (*done)(void *user), void *user) {
  fp_obj tp = d->time_page ? d->time_page() : 0;
  if (!d->anim_translate_x || !d->after) { fe_time_restore(d); if (done) done(user); return; }
  d->anim_translate_x(page_root, 0, dir * FE_SCR_W, SG_PAGE_MS, FP_PATH_PAGE_CD);
  if (tp) {
    d->anim_translate_x(tp, -dir * FE_SCR_W, 0, SG_PAGE_MS, FP_PATH_PAGE_CD);
    if (d->anim_opa) d->anim_opa(tp, 102, 255, SG_PAGE_MS, FP_PATH_PAGE_CD);
  }
  d->after(SG_PAGE_MS, done, user);
}

void fe_screen_off_fade(const fp_display *d, void (*done)(void *user), void *user) {
  fp_obj tp = d->time_page ? d->time_page() : 0;
  if (!tp || !d->anim_opa || !d->after) { if (done) done(user); return; }
  d->anim_opa(tp, 255, 0, FE_SCREEN_OFF_MS, FP_PATH_EASE_OUT_QUAD);
  if (d->anim_scale) d->anim_scale(tp, 256, 246, FE_SCREEN_OFF_MS, FP_PATH_EASE_OUT_QUAD); /* 0.96 */
  d->after(FE_SCREEN_OFF_MS, done, user);
}

/* ============================ control center ============================ */
static const uint8_t *cc_glyph_a8(int i) {
  switch (i) {
  case FE_CC_BACK:    return sg_glyph_forward_28;   /* points right, toward the time page */
  case FE_CC_BRIGHT:  return sg_glyph_sun_28;
  case FE_CC_VOLUME:  return sg_glyph_speaker_28;
  case FE_CC_RAISE:   return sg_glyph_raise_34;
  case FE_CC_DND:     return sg_glyph_moon_34;
  case FE_CC_BT:      return sg_glyph_bluetooth_34;
  default:            return sg_glyph_battery_24;
  }
}
static unsigned cc_glyph_px(int i) {
  return i == FE_CC_BACK ? 28 : (FE_CC_SLIDER_MASK & (1u << i)) ? 28 : i == FE_CC_BATTERY ? 24 : 34;
}
static const char *const CC_FALLBACK[FE_CC_N] = FE_S_CC_FALLBACK;

bool fe_cc_build(const fp_display *d, fp_obj parent, FECcView *v) {
  v->disp = d; v->glyph_img = 0; v->last_focus = -1; v->adj_item = -1; v->last_select = false;
  if (!(v->root = d->obj(parent))) return false;
  d->pos_size(v->root, 0, 0, FE_SCR_W, FE_SCR_H);
  d->bg(v->root, 0, 255);
  for (int i = 0; i < FE_CC_N; i++) {
    FERect r = fe_cc_tile_rect(i);
    bool slider = (FE_CC_SLIDER_MASK & (1u << i)) != 0, img;
    if (!(v->tile[i] = d->obj(v->root))) return false;
    place_rect(d, v->tile[i], r);
    d->bg(v->tile[i], i == FE_CC_BACK ? 52 : G_TILE, 255);
    if (slider) {
      if (d->clip_corner) d->clip_corner(v->tile[i], true);   /* flat-top fill */
      if (!(v->fill[i] = d->obj(v->tile[i]))) return false;
      d->bg(v->fill[i], G_FILL, 255);
      d->radius(v->fill[i], d->clip_corner ? 0 : r.r);
    }
    unsigned px = cc_glyph_px(i);
    if (!(v->glyph[i] = glyph_make(d, v->tile[i], cc_glyph_a8(i), px, CC_FALLBACK[i], &img))) return false;
    if (img) v->glyph_img |= (uint8_t)(1u << i);
    int gy = slider ? r.h - 24 : i == FE_CC_BATTERY ? r.h / 2 + 15 : r.h / 2;
    put_box(d, v->glyph[i], r.w / 2, gy, (int)px, (int)px);
    if (i == FE_CC_BATTERY) {                     /* percentage above the battery */
      if (!(v->sub[i] = d->label(v->tile[i], 22, FP_W_BOLD))) return false;
      put_box(d, v->sub[i], r.w / 2, r.h / 2 - 9, r.w, 26);
      d->text_gray(v->sub[i], 240);
      glyph_gray(d, v->glyph[i], img, 170);
    }
  }
  if (!(v->info = d->obj(v->root))) return false;
  d->pos_size(v->info, CC_INFO_X, 0, CC_INFO_W, FE_SCR_H);
  d->bg(v->info, 0, 0);
  if (!(v->info_title = d->label(v->info, 20, FP_W_REGULAR)) || !(v->info_value = d->label(v->info, 38, FP_W_BOLD)) ||
      !(v->info_hint = d->label(v->info, 18, FP_W_REGULAR)) || !(v->info_hint2 = d->label(v->info, 18, FP_W_REGULAR))) return false;
  d->pos_size(v->info_title, 0, 66 - 13, CC_INFO_W, 26);
  d->pos_size(v->info_value, 0, 108 - 24, CC_INFO_W, 48);
  d->pos_size(v->info_hint,  0, 150 - 12, CC_INFO_W, 24);
  d->pos_size(v->info_hint2, 0, 176 - 12, CC_INFO_W, 24);
  left(d, v->info_title); left(d, v->info_value); left(d, v->info_hint); left(d, v->info_hint2);
  d->text_gray(v->info_title, 160);
  d->text_gray(v->info_value, 255);
  d->text_gray(v->info_hint, 120);
  d->text_gray(v->info_hint2, 230);
  v->ring.disp = d; v->ring.on_frame = 0; v->ring.owner = 0;
  if (!(v->ring.obj = d->obj(v->root))) return false;
  d->bg(v->ring.obj, 0, 0);
  d->border(v->ring.obj, 2, G_RING);
  bubble_snap(&v->ring, grow(fe_cc_tile_rect(FE_CC_BACK), CC_RING_GROW));
  d->visible(v->ring.obj, false);
  return true;
}

void fe_cc_update(FECcView *v, int focus, bool select, bool adjust, const FECcValues *val) {
  const fp_display *d = v->disp;
  uint8_t bk = (uint8_t)(v->bank ^ 1u);
  int adj = (adjust && select) ? focus : -1;
  for (int i = 0; i < FE_CC_N; i++) {
    FERect r = fe_cc_tile_rect(i);
    bool img = (v->glyph_img >> i) & 1u;
    bool on = (i == FE_CC_RAISE && val->raise) || (i == FE_CC_DND && val->dnd) || (i == FE_CC_BT && val->bt);
    if (!(FE_CC_SLIDER_MASK & (1u << i)) && i != FE_CC_BACK) d->bg(v->tile[i], on ? G_ON : G_TILE, 255);
    if (i == FE_CC_BATTERY) d->text(v->sub[i], fmt_pct(v->batt_txt[bk], val->battery_pct));
    else if (i == FE_CC_BACK) glyph_gray(d, v->glyph[i], img, 235);
    else if (!(FE_CC_SLIDER_MASK & (1u << i))) glyph_gray(d, v->glyph[i], img, on ? 10 : 220);
    if (v->fill[i]) {
      int pct = i == FE_CC_BRIGHT ? (val->brightness_0_17 * 100 + 8) / 17 : val->volume_0_100;
      pct = clampi(pct, 0, 100);
      int fh = r.h * pct / 100;
      d->visible(v->fill[i], fh > 0);
      d->pos_size(v->fill[i], 0, r.h - fh, r.w, fh);
      d->bg(v->fill[i], adj == i ? G_FILL_SEL : G_FILL, 255);
      glyph_gray(d, v->glyph[i], img, fh >= 36 ? 0 : 220);   /* dark once the fill covers it */
    }
  }
  /* selection: tile x1.05 with the MOVE spring (resize fallback) */
  if (adj != v->adj_item) {
    for (int i = 0; i < FE_CC_N; i++) {
      bool was = i == v->adj_item, now = i == adj;
      if (was == now) continue;
      if (d->anim_scale) d->anim_scale(v->tile[i], now ? 256 : CC_SEL_SCALE, now ? CC_SEL_SCALE : 256, SG_MOVE_MS, FP_PATH_SPRING_MOVE);
      else { FERect r = fe_cc_tile_rect(i); if (now) { r.w = (int16_t)(r.w * 105 / 100); r.h = (int16_t)(r.h * 105 / 100); } place_rect(d, v->tile[i], r); }
    }
    v->adj_item = (int8_t)adj;
  }
  /* info panel */
  int f = select ? focus : -1;
  const char *title = f >= 0 ? CC_TITLE[f] : FE_S_CC;
  const char *value = "", *hint = FE_S_HINT_SELECT, *hint2 = "";
  if (f == FE_CC_BACK)        { value = val->return_to_app ? FE_S_TO_APP : FE_S_TO_CLOCK; hint = FE_S_HINT_BACK; }
  else if (f == FE_CC_BRIGHT || f == FE_CC_VOLUME) {
    int pct = f == FE_CC_BRIGHT ? (val->brightness_0_17 * 100 + 8) / 17 : val->volume_0_100;
    value = fmt_pct(v->value_txt[bk], pct);
    if (adjust) { hint = FE_S_HINT_ROTATE; hint2 = FE_S_HINT_DONE; } else hint = FE_S_HINT_ADJUST;
  }
  else if (f == FE_CC_RAISE)  { value = val->raise ? FE_S_ON : FE_S_OFF; hint = FE_S_HINT_TOGGLE; }
  else if (f == FE_CC_DND)    { value = val->dnd ? FE_S_ON : FE_S_OFF; hint = FE_S_HINT_TOGGLE; }
  else if (f == FE_CC_BT)     { value = val->bt ? FE_S_CONNECTED : FE_S_DISCONNECTED; hint = FE_S_HINT_INFO; }
  else if (f == FE_CC_BATTERY){ value = fmt_pct(v->value_txt[bk], val->battery_pct); hint = val->charging ? FE_S_CHARGING : FE_S_HINT_INFO; }
  d->text(v->info_title, title);
  d->text(v->info_value, value);
  d->text(v->info_hint, hint);
  d->text(v->info_hint2, hint2);
  d->text_gray(v->info_hint, adjust ? 230 : 120);
  if ((f != v->last_focus || select != v->last_select) && d->anim_opa)
    d->anim_opa(v->info, 0, 255, FE_INFO_FADE_MS, FP_PATH_EASE_OUT);   /* fade in on change */
  v->last_focus = (int8_t)f; v->last_select = select;
  v->bank = bk;
  /* focus ring follows with the MOVE spring */
  d->visible(v->ring.obj, select);
  if (select) {
    FERect t = fe_cc_tile_rect(focus);
    if (adjust) { t.w = (int16_t)(t.w * 105 / 100); t.h = (int16_t)(t.h * 105 / 100); }
    d->border(v->ring.obj, adjust ? 3 : 2, G_RING);
    bubble_go(&v->ring, grow(t, CC_RING_GROW));
  }
}

void fe_cc_free(FECcView *v) {
  const fp_display *d = v->disp;
  if (!d) return;
  if (d->anim_stop) {
    d->anim_stop(&v->ring);
    if (v->info) d->anim_stop(v->info);
    for (int i = 0; i < FE_CC_N; i++) if (v->tile[i]) d->anim_stop(v->tile[i]);
  }
  if (v->root) d->destroy(v->root);
  v->root = 0; v->ring.obj = 0; v->info = 0;
  for (int i = 0; i < FE_CC_N; i++) v->tile[i] = 0;
}

/* ============================ app grid =================================== */
static void grid_shade(void *owner) {
  FEAppsView *v = (FEAppsView *)owner;
  if (v->slide_dir) return;                       /* panes in motion */
  const fp_display *d = v->disp;
  FEGridPane *p = &v->pane[v->cur];
  for (int s = 0; s < FE_GRID_PER_PAGE; s++) {
    int idx = p->page * FE_GRID_PER_PAGE + s;
    if (idx >= v->count || !p->icon[s]) continue;
    FERect c = fe_grid_cell_rect(s);
    int k = fe_follow_k256(v->bubble.cur.x - c.x, v->bubble.cur.y - c.y, GRID_FOLLOW_RX, GRID_FOLLOW_RY);
    d->opa(p->icon[s], (uint8_t)(150 + 105 * k / 256));
    set_scale(d, p->icon[s], 256 + 15 * k / 256);   /* focused icon x1.06 */
    d->text_gray(p->label[s], (uint8_t)(140 + 115 * k / 256));
  }
}

static void grid_dots(FEAppsView *v, int32_t pos_q10) {   /* pos in pages, Q10 */
  const fp_display *d = v->disp;
  int total = (v->pages - 1) * GRID_DOT_PITCH;
  for (int i = 0; i < FE_GRID_MAX_PAGES; i++) {
    if (!v->dot[i]) continue;
    bool show = v->pages > 1 && i < v->pages;
    d->visible(v->dot[i], show);
    if (!show) continue;
    int32_t dist = i_abs((int)(pos_q10 - (int32_t)i * 1024));
    int32_t c = clampi(1024 - (int)dist, 0, 1024);
    int w = 6 + (int)((16 - 6) * c / 1024);
    int cx = FE_SCR_W / 2 - total / 2 + i * GRID_DOT_PITCH;
    d->pos_size(v->dot[i], cx - w / 2, GRID_DOT_Y - 3, w, 6);
    d->radius(v->dot[i], 3);
    d->bg(v->dot[i], (uint8_t)(70 + 160 * c / 1024), 255);
  }
}

/* One screen of cells at x. If bubble_slot >= 0 the focus bubble is created
 * first inside this pane (draws beneath the icons, slides with the pane). */
static bool pane_build(FEAppsView *v, FEGridPane *p, int page, int x, int bubble_slot) {
  const fp_display *d = v->disp;
  p->page = page;
  for (int s = 0; s < FE_GRID_PER_PAGE; s++) p->icon[s] = p->label[s] = 0;
  if (!(p->root = d->obj(v->root))) return false;
  d->pos_size(p->root, x, 0, FE_SCR_W, FE_SCR_H);
  d->bg(p->root, 0, 0);
  if (bubble_slot >= 0) {
    if (d->anim_stop) d->anim_stop(&v->bubble);
    v->bubble.disp = d; v->bubble.on_frame = grid_shade; v->bubble.owner = v;
    if (!(v->bubble.obj = d->obj(p->root))) return false;
    d->bg(v->bubble.obj, 34, 255);
    d->border(v->bubble.obj, 2, 235);
    FERect r = fe_grid_cell_rect(bubble_slot);
    v->bubble.from = v->bubble.to = v->bubble.cur = r;
    place_rect(d, v->bubble.obj, r);
  }
  for (int s = 0; s < FE_GRID_PER_PAGE; s++) {
    int idx = page * FE_GRID_PER_PAGE + s, cx = GRID_CX[s % FE_GRID_COLS], cy = GRID_CY[s / FE_GRID_COLS];
    if (idx >= v->count) continue;
    const FEGridEntry *e = &v->entries[idx];
    if (!(p->icon[s] = d->canvas(p->root)) || !(p->label[s] = d->label(p->root, 18, FP_W_BOLD))) return false;
    put_box(d, p->icon[s], cx, cy + GRID_ICON_CY, FE_ICON_BOX, FE_ICON_BOX);
    d->bg(p->icon[s], 0, 0);
    if (e->icon_l8) d->canvas_buf(p->icon[s], e->icon_l8, FE_ICON_BOX, FE_ICON_BOX);
    else          { d->radius(p->icon[s], 12); d->border(p->icon[s], 2, 150); }  /* placeholder */
    put_box(d, p->label[s], cx, cy + GRID_LABEL_CY, GRID_LABEL_W, GRID_LABEL_H);
    d->text(p->label[s], e->label ? e->label : "");
    d->opa(p->icon[s], 150);
    d->text_gray(p->label[s], 140);
  }
  return true;
}

bool fe_apps_build(const fp_display *d, fp_obj parent, FEAppsView *v,
                   const FEGridEntry *e, int count, int focus) {
  v->disp = d; v->entries = e; v->count = count > 0 ? count : 0;
  v->pages = clampi((v->count + FE_GRID_PER_PAGE - 1) / FE_GRID_PER_PAGE, 1, FE_GRID_MAX_PAGES);
  v->focus = clampi(focus, 0, v->count ? v->count - 1 : 0);
  v->page = v->focus / FE_GRID_PER_PAGE;
  v->cur = 0; v->slide_dir = 0;
  v->pane[1].root = 0;
  for (int i = 0; i < FE_GRID_MAX_PAGES; i++) v->dot[i] = 0;
  if (!(v->root = d->obj(parent))) return false;
  d->pos_size(v->root, 0, 0, FE_SCR_W, FE_SCR_H);
  d->bg(v->root, 0, 255);
  if (!pane_build(v, &v->pane[0], v->page, 0, v->focus % FE_GRID_PER_PAGE)) return false;
  for (int i = 0; i < v->pages && i < FE_GRID_MAX_PAGES; i++)
    if (!(v->dot[i] = d->obj(v->root))) return false;
  grid_dots(v, (int32_t)v->page * 1024);
  grid_shade(v);
  return true;
}

/* finish an in-flight slide immediately (input arrived mid-slide) */
static void slide_finish(FEAppsView *v) {
  const fp_display *d = v->disp;
  if (!v->slide_dir) return;
  if (d->anim_stop) d->anim_stop(v);
  int old = v->cur;
  v->cur ^= 1;
  if (v->pane[old].root) d->destroy(v->pane[old].root);
  v->pane[old].root = 0;
  d->pos_size(v->pane[v->cur].root, 0, 0, FE_SCR_W, FE_SCR_H);
  v->slide_dir = 0;
  grid_dots(v, (int32_t)v->page * 1024);
  grid_shade(v);
}

void fe_apps_slide_frame(FEAppsView *v, uint32_t e) {
  if (!v->slide_dir) return;
  const fp_display *d = v->disp;
  if (e >= SG_PAGE_MS) { slide_finish(v); return; }
  int32_t p = fe_page_curve_q10(e);
  int dir = v->slide_dir, old = v->cur, inc = v->cur ^ 1;
  d->pos_size(v->pane[old].root, (int)(-dir * FE_SCR_W * p / 1024), 0, FE_SCR_W, FE_SCR_H);
  d->pos_size(v->pane[inc].root, (int)(dir * FE_SCR_W - dir * FE_SCR_W * p / 1024), 0, FE_SCR_W, FE_SCR_H);
  int32_t from = (int32_t)(v->page - dir) * 1024;
  grid_dots(v, from + dir * p);
}
static void slide_exec(void *var, int32_t e) { fe_apps_slide_frame((FEAppsView *)var, (uint32_t)(e < 0 ? 0 : e)); }

void fe_apps_focus(FEAppsView *v, int focus) {
  const fp_display *d = v->disp;
  if (!d || focus < 0 || focus >= v->count || focus == v->focus) return;
  slide_finish(v);
  int page = focus / FE_GRID_PER_PAGE;
  v->focus = focus;
  if (page == v->page) {                        /* same screen: bubble spring */
    bubble_go(&v->bubble, fe_grid_cell_rect(focus % FE_GRID_PER_PAGE));
    return;
  }
  /* new screen slides in horizontally; the bubble rides with it (no bounce) */
  int dir = page > v->page ? 1 : -1, inc = v->cur ^ 1;
  v->page = page;
  if (d->anim_stop) d->anim_stop(&v->bubble);
  v->bubble.obj = 0;                             /* old one goes with the old pane */
  if (!pane_build(v, &v->pane[inc], page, dir * FE_SCR_W, focus % FE_GRID_PER_PAGE)) {
    if (v->pane[inc].root) d->destroy(v->pane[inc].root);
    v->pane[inc].root = 0;
    return;
  }
  v->slide_dir = dir;
  if (d->anim) d->anim(v, slide_exec, SG_PAGE_MS);
  else         slide_finish(v);
}

void fe_apps_free(FEAppsView *v) {
  const fp_display *d = v->disp;
  if (!d) return;
  if (d->anim_stop) { d->anim_stop(v); d->anim_stop(&v->bubble); }
  if (v->root) d->destroy(v->root);
  v->root = 0; v->bubble.obj = 0; v->pane[0].root = v->pane[1].root = 0; v->slide_dir = 0;
}

/* ============================ card ======================================= */
static void card_shade(void *owner) {
  FECardView *v = (FECardView *)owner;
  const fp_display *d = v->disp;
  for (int i = 0; i < 3; i++) {
    int k = fe_follow_k256(v->ring.cur.x - CARD_BTN_X[i], 0, CARD_FOLLOW_RX, 60);
    d->bg(v->btn[i], (uint8_t)(62 + 143 * k / 256), 255);
    set_scale(d, v->btn[i], 256 + 15 * k / 256);     /* x1.06 */
    /* glyph: smoothstep((k-0.35)/0.4) between 230 and 12 */
    int t = clampi((k * 1000 / 256 - 350) * 1000 / 400, 0, 1000);
    int s = t * t / 1000 * (3000 - 2 * t) / 1000;
    glyph_gray(d, v->glyph[i], v->glyph_img, (uint8_t)(230 - (230 - 12) * s / 1000));
    d->text_gray(v->label[i], (uint8_t)(150 + 105 * k / 256));
  }
}

static const uint8_t *card_glyph(int i) { return i == 0 ? sg_glyph_back_36 : i == 1 ? sg_glyph_grid_36 : sg_glyph_close_36; }

bool fe_card_build(const fp_display *d, fp_obj parent, FECardView *v) {
  v->disp = d; v->glyph_img = false;
  if (!(v->root = d->obj(parent))) return false;
  d->pos_size(v->root, 0, 0, FE_SCR_W, FE_SCR_H);
  d->bg(v->root, 0, 0);
  if (!(v->scrim = d->obj(v->root)) || !(v->pop = d->obj(v->root))) return false;
  d->pos_size(v->scrim, 0, 0, FE_SCR_W, FE_SCR_H);
  d->bg(v->scrim, 0, CARD_DIM);
  d->pos_size(v->pop, 0, 0, FE_SCR_W, FE_SCR_H);   /* centre == card centre */
  d->bg(v->pop, 0, 0);
  if (!(v->card = d->obj(v->pop))) return false;
  put_box(d, v->card, CARD_CX, CARD_CY, CARD_W, CARD_H);
  d->radius(v->card, CARD_R);
  d->bg(v->card, 30, 255);
  d->border(v->card, 1, 80);
  v->ring.disp = d; v->ring.on_frame = card_shade; v->ring.owner = v;
  if (!(v->ring.obj = d->obj(v->pop))) return false;
  d->bg(v->ring.obj, 0, 0);
  d->border(v->ring.obj, 2, G_RING);
  for (int i = 0; i < 3; i++) {
    FERect r = fe_card_btn_rect(i);
    bool img;
    if (!(v->btn[i] = d->obj(v->pop)) || !(v->label[i] = d->label(v->pop, 18, FP_W_BOLD))) return false;
    place_rect(d, v->btn[i], r);
    if (!(v->glyph[i] = glyph_make(d, v->btn[i], card_glyph(i), 36, CARD_FALLBACK[i], &img))) return false;
    v->glyph_img = img;
    put_box(d, v->glyph[i], r.w / 2, r.h / 2, 36, 36);
    put_box(d, v->label[i], r.x, CARD_LABEL_Y, 130, 26);
    d->text(v->label[i], CARD_LABEL[i]);
  }
  v->focus = 0;
  FERect ring = fe_card_btn_rect(0);
  ring.w = ring.h = CARD_RING_D; ring.r = CARD_RING_D / 2;
  bubble_snap(&v->ring, ring);
  /* pop in: scale 0.55 -> 1 (POP spring), fade 120 ms; backdrop dims 120 ms */
  if (d->anim_scale) d->anim_scale(v->pop, 141, 256, SG_POP_MS, FP_PATH_SPRING_POP);
  if (d->anim_opa) {
    d->anim_opa(v->pop, 0, 255, FE_CARD_IN_FADE_MS, FP_PATH_LINEAR);
    d->anim_opa(v->scrim, 0, CARD_DIM, FE_CARD_IN_FADE_MS, FP_PATH_LINEAR);
  }
  return true;
}

void fe_card_focus(FECardView *v, int focus) {
  if (focus < 0 || focus > 2) return;
  v->focus = focus;
  FERect ring = fe_card_btn_rect(focus);
  ring.w = ring.h = CARD_RING_D; ring.r = CARD_RING_D / 2;
  bubble_go(&v->ring, ring);
}

void fe_card_close(FECardView *v, void (*done)(void *user), void *user) {
  const fp_display *d = v->disp;
  if (!d || !v->root || !d->anim_opa || !d->after) { if (done) done(user); return; }
  if (d->anim_stop) d->anim_stop(&v->ring);
  if (d->anim_scale) d->anim_scale(v->pop, 256, 225, FE_CARD_OUT_MS, FP_PATH_SMOOTHSTEP);  /* 0.88 */
  d->anim_opa(v->pop, 255, 0, FE_CARD_OUT_MS, FP_PATH_SMOOTHSTEP);
  d->anim_opa(v->scrim, CARD_DIM, 0, FE_CARD_OUT_MS, FP_PATH_SMOOTHSTEP);
  d->after(FE_CARD_OUT_MS, done, user);
}

void fe_card_free(FECardView *v) {
  const fp_display *d = v->disp;
  if (!d) return;
  if (d->anim_stop) {
    d->anim_stop(&v->ring);
    if (v->pop) d->anim_stop(v->pop);
    if (v->scrim) d->anim_stop(v->scrim);
  }
  if (v->root) d->destroy(v->root);
  v->root = 0; v->ring.obj = 0; v->pop = 0; v->scrim = 0;
}
