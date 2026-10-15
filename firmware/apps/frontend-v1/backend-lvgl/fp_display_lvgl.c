/* fp_display_lvgl.c — fp_display facade on LVGL v9.2. See fp_display_lvgl.h.
 * Clean-room, Spare original. No file-scope mutable statics. */
#include "fp_display_lvgl.h"
#include "lvgl.h"
#include "src/lvgl_private.h"   /* lv_refr_get_disp_refreshing() */
#include "../fe_pages.h"         /* fe_path_q10(): the facade's easing paths */

#define FPL_MAGIC 0x5350464cu   /* 'SPFL' */

/* Object kinds are tagged with LVGL user flags (no class-object or obj
 * user_data access: neither is reachable on the device base). */
#define FPL_FLAG_LABEL LV_OBJ_FLAG_USER_1
#define FPL_FLAG_IMAGE LV_OBJ_FLAG_USER_2
/* Image widget constructor. The device base only exports the canvas (an image
 * subclass); the device build passes -DFPL_IMAGE_CREATE=lv_canvas_create. */
#ifndef FPL_IMAGE_CREATE
#define FPL_IMAGE_CREATE lv_image_create
#endif

typedef struct {
  uint32_t    magic;
  lv_obj_t   *root;
  lv_timer_t *anchor;      /* paused timer, user_data = this ctx (lookup key) */
  fpl_font_fn font;
  void       *font_user;
  lv_obj_t   *time_page;
} fpl_ctx;

/* one-shot after(): heap record freed when the timer fires */
typedef struct { void (*cb)(void *); void *user; } fpl_after;

/* Per-canvas image descriptor, freed with the object. */
typedef struct { lv_image_dsc_t dsc; } fpl_canvas;

static inline lv_color_t green(uint8_t g) { return lv_color_make(0, g, 0); }

/* The context hangs off a paused LVGL timer we own: walk the timer list for
 * our anchor callback and accept its user_data only on the magic. */
static void anchor_cb(lv_timer_t *t) { (void)t; }
static fpl_ctx *ctx_find(void) {
  for (lv_timer_t *t = lv_timer_get_next(NULL); t; t = lv_timer_get_next(t)) {
    if (t->timer_cb != anchor_cb) continue;
    fpl_ctx *x = (fpl_ctx *)lv_timer_get_user_data(t);
    if (x && x->magic == FPL_MAGIC && x->anchor == t) return x;
  }
  return NULL;
}

static void plain(lv_obj_t *o) {
  lv_obj_remove_style_all(o);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICK_FOCUSABLE);
}

/* ---------------- facade entries ------------------------------------------ */
static bool d_ui_idle(void) { return lv_refr_get_disp_refreshing() == NULL; }

static fp_obj d_root(void) {
  fpl_ctx *x = ctx_find();
  return x ? x->root : NULL;
}

static fp_obj d_obj(fp_obj parent) {
  lv_obj_t *o = lv_obj_create((lv_obj_t *)parent);
  if (o) plain(o);
  return o;
}

static fp_obj d_label(fp_obj parent, unsigned font_px, fp_weight weight) {
  lv_obj_t *o = lv_label_create((lv_obj_t *)parent);
  if (!o) return NULL;
  plain(o);
  lv_obj_add_flag(o, FPL_FLAG_LABEL);
  fpl_ctx *x = ctx_find();
  const lv_font_t *f = (x && x->font) ? (const lv_font_t *)x->font(font_px, weight, x->font_user) : NULL;
  if (f) lv_obj_set_style_text_font(o, f, 0);
  lv_obj_set_style_text_color(o, green(255), 0);
  lv_obj_set_style_text_align(o, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(o, LV_LABEL_LONG_CLIP);
  lv_label_set_text_static(o, "");
  return o;
}

/* The per-canvas descriptor is the user data of our DELETE event callback. */
static void canvas_free_cb(lv_event_t *e) {
  fpl_canvas *c = (fpl_canvas *)lv_event_get_user_data(e);
  if (c) lv_free(c);
}
static fpl_canvas *canvas_of(lv_obj_t *o) {
  uint32_t n = lv_obj_get_event_count(o);
  for (uint32_t i = 0; i < n; i++) {
    lv_event_dsc_t *d = lv_obj_get_event_dsc(o, i);
    if (d && lv_event_dsc_get_cb(d) == canvas_free_cb) return (fpl_canvas *)lv_event_dsc_get_user_data(d);
  }
  return NULL;
}

static fp_obj d_canvas(fp_obj parent) {
  fpl_canvas *c = (fpl_canvas *)lv_malloc_zeroed(sizeof(*c));
  if (!c) return NULL;
  lv_obj_t *o = FPL_IMAGE_CREATE((lv_obj_t *)parent);
  if (!o) { lv_free(c); return NULL; }
  plain(o);
  lv_obj_add_flag(o, FPL_FLAG_IMAGE);
  lv_image_set_inner_align(o, LV_IMAGE_ALIGN_CENTER);
  /* A8 images take their colour from image_recolor (green, full) */
  lv_obj_set_style_image_recolor(o, green(255), 0);
  lv_obj_set_style_image_recolor_opa(o, LV_OPA_COVER, 0);
  lv_obj_add_event_cb(o, canvas_free_cb, LV_EVENT_DELETE, c);
  return o;
}

static void d_pos_size(fp_obj o, int x, int y, int w, int h) {
  lv_obj_t *ob = (lv_obj_t *)o;
  lv_obj_set_pos(ob, x, y);
  lv_obj_set_size(ob, w, h);
  if (lv_obj_has_flag(ob, FPL_FLAG_LABEL)) {   /* vertical centring of one line */
    const lv_font_t *f = lv_obj_get_style_text_font(ob, 0);
    int lh = f ? (int)lv_font_get_line_height(f) : h;
    int pad = (h - lh) / 2;
    lv_obj_set_style_pad_top(ob, pad > 0 ? pad : 0, 0);
  }
}

static void d_radius(fp_obj o, int r) {
  lv_obj_set_style_radius((lv_obj_t *)o, r < 0 ? 0 : r, 0);
}

static void d_bg(fp_obj o, uint8_t gray, uint8_t opa) {
  lv_obj_set_style_bg_color((lv_obj_t *)o, green(gray), 0);
  lv_obj_set_style_bg_opa((lv_obj_t *)o, opa, 0);
}

static void d_border(fp_obj o, int width, uint8_t gray) {
  lv_obj_t *ob = (lv_obj_t *)o;
  lv_obj_set_style_border_width(ob, width > 0 ? width : 0, 0);
  lv_obj_set_style_border_color(ob, green(gray), 0);
  lv_obj_set_style_border_opa(ob, width > 0 ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
}

static void d_text_gray(fp_obj o, uint8_t gray) {
  lv_obj_t *ob = (lv_obj_t *)o;
  if (lv_obj_has_flag(ob, FPL_FLAG_IMAGE)) lv_obj_set_style_image_recolor(ob, green(gray), 0);
  else                                         lv_obj_set_style_text_color(ob, green(gray), 0);
}

static void d_text(fp_obj o, const char *utf8_static) {
  lv_label_set_text_static((lv_obj_t *)o, utf8_static ? utf8_static : "");
}

/* 1x1 fully transparent A8 image: "no icon" without an unknown-src warning. */
static const uint8_t FPL_EMPTY_PX[1] = { 0 };
static const lv_image_dsc_t FPL_EMPTY = {
  .header = { .magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_A8, .w = 1, .h = 1, .stride = 1 },
  .data_size = 1, .data = FPL_EMPTY_PX,
};

static void d_canvas_buf(fp_obj o, const uint8_t *l8, unsigned w, unsigned h) {
  lv_obj_t *ob = (lv_obj_t *)o;
  if (!l8 || !w || !h) { lv_image_set_src(ob, &FPL_EMPTY); return; }
  fpl_canvas *c = canvas_of(ob);
  if (!c) return;
  if (c->dsc.data) lv_image_cache_drop(&c->dsc);   /* same dsc, new pixels */
  lv_memzero(&c->dsc, sizeof(c->dsc));
  c->dsc.header.magic  = LV_IMAGE_HEADER_MAGIC;
  c->dsc.header.cf     = LV_COLOR_FORMAT_A8;  /* coverage; colour via recolor */
  c->dsc.header.w      = (uint16_t)w;
  c->dsc.header.h      = (uint16_t)h;
  c->dsc.header.stride = (uint16_t)w;
  c->dsc.data_size     = (uint32_t)(w * h);
  c->dsc.data          = l8;
  lv_image_set_src(ob, &c->dsc);
}

static void d_opa(fp_obj o, uint8_t opa) { lv_obj_set_style_opa((lv_obj_t *)o, opa, 0); }

static void d_visible(fp_obj o, bool on) {
  if (on) lv_obj_remove_flag((lv_obj_t *)o, LV_OBJ_FLAG_HIDDEN);
  else    lv_obj_add_flag((lv_obj_t *)o, LV_OBJ_FLAG_HIDDEN);
}

static void d_destroy(fp_obj o) { if (o) lv_obj_delete((lv_obj_t *)o); }

static void d_anim(void *var, void (*exec)(void *, int32_t), uint32_t ms) {
  lv_anim_delete(var, NULL);
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, var);
  lv_anim_set_exec_cb(&a, exec);
  lv_anim_set_values(&a, 0, (int32_t)ms);
  lv_anim_set_duration(&a, ms ? ms : 1);
  lv_anim_set_path_cb(&a, lv_anim_path_linear);
  lv_anim_start(&a);
}

static void d_anim_stop(void *var) { lv_anim_delete(var, NULL); }

static uint32_t d_tick_ms(void) { return lv_tick_get(); }

/* ---------------- property tweens (request #1/#2) -------------------------- */
/* The fp_path id rides in the anim's user_data; fe_path_q10 is the single
 * definition of every curve (same numbers as the host tests / SPEC). */
static int32_t path_cb(const lv_anim_t *a) {
  fp_path p = (fp_path)(uintptr_t)a->user_data;
  uint32_t t = (uint32_t)(a->act_time < 0 ? 0 : a->act_time);
  int32_t q = fe_path_q10(p, t, a->duration ? a->duration : 1);
  return a->start_value + (int32_t)(((int64_t)(a->end_value - a->start_value) * q) >> 10);
}
static void set_scale(lv_obj_t *o, int32_t q8) {
  lv_obj_set_style_transform_pivot_x(o, lv_pct(50), 0);
  lv_obj_set_style_transform_pivot_y(o, lv_pct(50), 0);
  lv_obj_set_style_transform_scale(o, q8 < 1 ? 1 : q8, 0);
}
static void exec_scale(void *o, int32_t v) { set_scale((lv_obj_t *)o, v); }
static void exec_opa(void *o, int32_t v) {
  lv_obj_set_style_opa((lv_obj_t *)o, (lv_opa_t)(v < 0 ? 0 : v > 255 ? 255 : v), 0);
}
static void exec_tx(void *o, int32_t v) { lv_obj_set_style_translate_x((lv_obj_t *)o, v, 0); }

static void tween(lv_obj_t *o, lv_anim_exec_xcb_t exec, int32_t from, int32_t to,
                  uint32_t ms, fp_path path) {
  lv_anim_delete(o, exec);                  /* same property on same obj: replace */
  exec(o, from);
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, o);
  lv_anim_set_exec_cb(&a, exec);
  lv_anim_set_values(&a, from, to);
  lv_anim_set_duration(&a, ms ? ms : 1);
  lv_anim_set_path_cb(&a, path_cb);
  lv_anim_set_user_data(&a, (void *)(uintptr_t)path);
  lv_anim_start(&a);
}

static void d_scale(fp_obj o, int q8) { set_scale((lv_obj_t *)o, q8); }
static void d_anim_scale(fp_obj o, int from_q8, int to_q8, uint32_t ms, fp_path path) {
  tween((lv_obj_t *)o, exec_scale, from_q8, to_q8, ms, path);
}
static void d_anim_opa(fp_obj o, uint8_t from, uint8_t to, uint32_t ms, fp_path path) {
  tween((lv_obj_t *)o, exec_opa, from, to, ms, path);
}
static void d_translate_x(fp_obj o, int px) { lv_obj_set_style_translate_x((lv_obj_t *)o, px, 0); }
static void d_anim_translate_x(fp_obj o, int from_px, int to_px, uint32_t ms, fp_path path) {
  tween((lv_obj_t *)o, exec_tx, from_px, to_px, ms, path);
}

static void after_cb(lv_timer_t *t) {
  fpl_after *r = (fpl_after *)lv_timer_get_user_data(t);
  lv_timer_delete(t);
  if (r) { void (*cb)(void *) = r->cb; void *u = r->user; lv_free(r); if (cb) cb(u); }
}
static void d_after(uint32_t ms, void (*cb)(void *user), void *user) {
  fpl_after *r = (fpl_after *)lv_malloc(sizeof(*r));
  if (!r) return;
  r->cb = cb; r->user = user;
  lv_timer_t *t = lv_timer_create(after_cb, ms ? ms : 1, r);
  if (!t) { lv_free(r); return; }
}

static fp_obj d_time_page(void) { fpl_ctx *x = ctx_find(); return x ? x->time_page : NULL; }

static void d_text_align(fp_obj o, fp_align a) {
  lv_obj_set_style_text_align((lv_obj_t *)o, a == FP_ALIGN_LEFT ? LV_TEXT_ALIGN_LEFT :
                              a == FP_ALIGN_RIGHT ? LV_TEXT_ALIGN_RIGHT : LV_TEXT_ALIGN_CENTER, 0);
}
static void d_clip_corner(fp_obj o, bool on) { lv_obj_set_style_clip_corner((lv_obj_t *)o, on, 0); }

static const fp_display FPL_DISPLAY = {
  .ui_idle = d_ui_idle, .root = d_root, .obj = d_obj, .label = d_label,
  .canvas = d_canvas, .pos_size = d_pos_size, .radius = d_radius, .bg = d_bg,
  .border = d_border, .text_gray = d_text_gray, .text = d_text,
  .canvas_buf = d_canvas_buf, .opa = d_opa, .visible = d_visible,
  .destroy = d_destroy, .anim = d_anim, .anim_stop = d_anim_stop,
  .tick_ms = d_tick_ms,
  .scale = d_scale, .anim_scale = d_anim_scale, .anim_opa = d_anim_opa, .after = d_after,
  .time_page = d_time_page, .translate_x = d_translate_x, .anim_translate_x = d_anim_translate_x,
  .text_align = d_text_align, .clip_corner = d_clip_corner,
  .image = d_canvas, .image_a8 = d_canvas_buf, .image_gray = d_text_gray,
};

/* ---------------- lifecycle ------------------------------------------------ */
static void root_free_cb(lv_event_t *e) {
  fpl_ctx *x = (fpl_ctx *)lv_event_get_user_data(e);
  if (x && x->magic == FPL_MAGIC) {
    x->magic = 0;
    if (x->anchor) lv_timer_delete(x->anchor);
    lv_free(x);
  }
}

const fp_display *fpl_init(fpl_font_fn font, void *font_user) {
  fpl_ctx *x = ctx_find();
  if (x) { x->font = font; x->font_user = font_user; return &FPL_DISPLAY; }
  x = (fpl_ctx *)lv_malloc_zeroed(sizeof(*x));
  if (!x) return NULL;
  lv_obj_t *root = lv_obj_create(lv_layer_top());
  if (!root) { lv_free(x); return NULL; }
  x->anchor = lv_timer_create(anchor_cb, 0x7fffffff, x);
  if (!x->anchor) { lv_obj_delete(root); lv_free(x); return NULL; }
  lv_timer_pause(x->anchor);
  plain(root);
  lv_obj_set_pos(root, 0, 0);
  lv_obj_set_size(root, 540, 220);
  x->magic = FPL_MAGIC; x->root = root; x->font = font; x->font_user = font_user;
  lv_obj_add_event_cb(root, root_free_cb, LV_EVENT_DELETE, x);
  return &FPL_DISPLAY;
}

void fpl_set_time_page(fp_obj time_page) {
  fpl_ctx *x = ctx_find();
  if (x) x->time_page = (lv_obj_t *)time_page;
}

void fpl_deinit(void) {
  fpl_ctx *x = ctx_find();
  if (x) lv_obj_delete(x->root);
}
