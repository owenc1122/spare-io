#ifndef FE_PAGES_H
#define FE_PAGES_H
/* fe_pages — builds the front-end pages at the real launcher screen size
 * 540x220 through the fp_display seam (frontend_port.h). The SAME source
 * compiles into the firmware image and into the host LVGL v9 preview backend.
 * Layout numbers follow prototypes/frontend-preview/out/SPEC.md (3x2 grid at
 * 540x220). Clean-room, Spare original; no vendor names/addresses; no
 * file-scope mutable state (everything lives in the caller-allocated views).
 *
 * The time page is NOT built here: the stock watch face stays untouched.
 *
 * Threading: UI thread only. Animations are driven through fp_display.anim();
 * each view keeps a back-pointer to the display table for its anim callbacks. */
#include <stdint.h>
#include <stdbool.h>
#include "frontend_port.h"

#define FE_SCR_W 540
#define FE_SCR_H 220
#define FE_GRID_COLS 3
#define FE_GRID_ROWS 2
#define FE_GRID_PER_PAGE (FE_GRID_COLS * FE_GRID_ROWS)
#define FE_ICON_BOX 44
#define FE_GRID_MAX_PAGES 6

/* Control-center items (index order == focus order). */
#ifndef FE_CC_BACK
#define FE_CC_BACK 0   /* shared with fe_state.h */
#endif
enum { FE_CC_BRIGHT = 1, FE_CC_VOLUME, FE_CC_RAISE, FE_CC_DND,
       FE_CC_BT, FE_CC_BATTERY, FE_CC_N };
#define FE_CC_SLIDER_MASK ((1u << FE_CC_BRIGHT) | (1u << FE_CC_VOLUME))

typedef struct { int16_t x, y, w, h, r; } FERect;   /* centre + size + radius */

/* Spring-driven highlight (focus bubble / ring). `on_frame(owner)` lets the
 * owning page re-shade neighbours from the bubble's live position. */
typedef struct {
  const fp_display *disp;
  fp_obj  obj;
  FERect  from, to, cur;
  void  (*on_frame)(void *owner);
  void   *owner;
} FEBubble;

/* ---------------- control center ---------------- */
typedef struct {
  const fp_display *disp;
  fp_obj root;
  fp_obj tile[FE_CC_N], fill[FE_CC_N], glyph[FE_CC_N], sub[FE_CC_N];
  fp_obj info, info_title, info_value, info_hint, info_hint2;  /* info = fade group */
  uint8_t glyph_img;               /* bit i: glyph[i] is an image (else text label) */
  int8_t  last_focus, adj_item;    /* for info fade / tile x1.05 transitions        */
  bool    last_select;
  FEBubble ring;
  char   value_txt[2][16], batt_txt[2][8];
  uint8_t bank;
} FECcView;

typedef struct {
  int  brightness_0_17, volume_0_100, battery_pct;
  bool raise, dnd, bt, charging;
  bool return_to_app;            /* Back returns to an app, not the watch face */
} FECcValues;

/* ---------------- app grid ---------------- */
typedef struct { const char *label; const uint8_t *icon_l8; } FEGridEntry;

typedef struct {                 /* one screen of 6 cells (slides as a unit) */
  fp_obj root;
  fp_obj icon[FE_GRID_PER_PAGE], label[FE_GRID_PER_PAGE];
  int    page;
} FEGridPane;

typedef struct {
  const fp_display *disp;
  fp_obj root;
  FEGridPane pane[2];            /* [cur] showing, [cur^1] incoming during slide */
  int    cur;
  fp_obj dot[FE_GRID_MAX_PAGES];
  FEBubble bubble;               /* lives in the active pane */
  const FEGridEntry *entries;    /* caller-owned, stable while the page lives */
  int    count, pages, page, focus;
  int    slide_dir;              /* 0 idle, +1 to next page, -1 to previous */
} FEAppsView;

/* ---------------- in-app card ---------------- */
typedef struct {
  const fp_display *disp;
  fp_obj root, scrim, pop, card;   /* pop = card + buttons + ring (scales as one) */
  fp_obj btn[3], glyph[3], label[3];
  bool   glyph_img;
  FEBubble ring;
  int    focus;
} FECardView;

/* --- pure geometry (host-testable) --- */
FERect fe_grid_cell_rect(int slot);          /* bubble rect for slot 0..5      */
FERect fe_cc_tile_rect(int item);
FERect fe_card_btn_rect(int idx);
FERect fe_bubble_at(const FERect *from, const FERect *to, uint32_t e_ms);
int32_t fe_page_curve_q10(uint32_t e_ms);    /* critically damped, 0..1024    */
/* Evaluate any facade easing path: Q10 progress (spring paths overshoot
 * above 1024 and settle at exactly 1024 when e_ms >= ms). For backends. */
int32_t fe_path_q10(fp_path path, uint32_t e_ms, uint32_t ms);
/* follow weight 0..256 of a cell relative to the bubble centre (SPEC k)     */
int fe_follow_k256(int dx, int dy, int rx, int ry);

/* --- control center --- */
bool fe_cc_build(const fp_display *d, fp_obj parent, FECcView *v);
void fe_cc_update(FECcView *v, int focus, bool select, bool adjust, const FECcValues *val);
void fe_cc_free(FECcView *v);

/* --- app grid: 3x2, row-major focus, horizontal page slide --- */
bool fe_apps_build(const fp_display *d, fp_obj parent, FEAppsView *v,
                   const FEGridEntry *e, int count, int focus);
void fe_apps_focus(FEAppsView *v, int focus);   /* bubble move or page slide */
void fe_apps_free(FEAppsView *v);

/* --- in-app card (返回 / 控制中心 / 退出) --- */
bool fe_card_build(const fp_display *d, fp_obj parent, FECardView *v);
void fe_card_focus(FECardView *v, int focus);
void fe_card_free(FECardView *v);
/* Animated close (180 ms smoothstep, scale 1.0 -> 0.88, fade, backdrop back to
 * 100%); done(user) runs afterwards so the caller frees the card. Instant if
 * the backend lacks tweens. */
void fe_card_close(FECardView *v, void (*done)(void *user), void *user);

/* --- page transitions (time page is the stock watch face via d->time_page) ---
 * Enter: the new full-screen page slides in from dir (-1 = from the left, used
 * by the control center on CCW; +1 = from the right, the app grid on CW) with
 * the critically damped PAGE curve while the watch face slides out the other
 * way and dims to 40%. Leave: the reverse; `done(user)` runs after the slide so
 * the caller can free the page. Without anim support both are instant. */
void fe_page_enter(const fp_display *d, fp_obj page_root, int dir);
void fe_page_leave(const fp_display *d, fp_obj page_root, int dir,
                   void (*done)(void *user), void *user);
/* Screen-off: watch face fades to 0 and shrinks to 0.96 over 280 ms, then
 * done(user) (caller powers the panel off). fe_time_restore() resets the watch
 * face's translate/opa/scale (call on wake and after every leave). */
void fe_screen_off_fade(const fp_display *d, void (*done)(void *user), void *user);
void fe_time_restore(const fp_display *d);
#define FE_SCREEN_OFF_MS 280u
#define FE_INFO_FADE_MS 160u
#define FE_CARD_IN_FADE_MS 120u
#define FE_CARD_OUT_MS 180u

/* Apply one animation frame directly (used by the anim exec and by preview
 * tools that render deterministic frames). */
void fe_bubble_frame(FEBubble *b, uint32_t e_ms);
void fe_apps_slide_frame(FEAppsView *v, uint32_t e_ms);

#endif
