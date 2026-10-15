/* Centring check against the REAL LVGL v9 backend (backend-lvgl/fp_display_lvgl.c).
 * Builds the pages with fe_pages.c, lets every animation settle with lv_tick +
 * lv_timer_handler, then reads LVGL's final object coordinates and asserts:
 *   app grid  : |bubble_cx - icon_cx| <= 1, |bubble_cx - label_cx| <= 1 and
 *               |bubble_cy - (icon top .. label bottom)_cy| <= 1 on all 6 cells of
 *               both pages, block fully inside the bubble;
 *   CC rings  : ring centre == tile centre (+-1) for every item;
 *   card      : ring centre == button centre == glyph centre (+-1).
 * Build + run from firmware/apps/frontend-v1 (after `make preview` built LVGL):
 *   xcrun clang -O1 -std=c11 -DLV_CONF_INCLUDE_SIMPLE -Ibackend-lvgl/host -I../../../build/lvgl -I. \
 *     tests/test_fe_lvgl_centring.c fe_pages.c backend-lvgl/fp_display_lvgl.c \
 *     ../../../build/frontend-preview/obj/liblvgl.a -o /tmp/fe_centring && /tmp/fe_centring
 * Clean-room, Spare original. */
#include "lvgl.h"
#include "../fe_pages.h"
#include "../backend-lvgl/fp_display_lvgl.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static uint8_t fb[540 * 220 * 4];
static void flush(lv_display_t *d, const lv_area_t *a, uint8_t *px) { (void)a; (void)px; lv_display_flush_ready(d); }
static void settle(void) { for (int i = 0; i < 120; i++) { lv_tick_inc(10); lv_timer_handler(); } }

static void box(fp_obj o, int *cx2, int *cy2, int *y1, int *y2) {
  lv_area_t a; lv_obj_update_layout((lv_obj_t *)o); lv_obj_get_coords((lv_obj_t *)o, &a);
  *cx2 = a.x1 + a.x2; *cy2 = a.y1 + a.y2; if (y1) *y1 = a.y1; if (y2) *y2 = a.y2;
}
static int ab(int v) { return v < 0 ? -v : v; }

int main(void) {
  lv_init();
  lv_display_t *disp = lv_display_create(540, 220);
  lv_display_set_buffers(disp, fb, NULL, sizeof fb, LV_DISPLAY_RENDER_MODE_FULL);
  lv_display_set_flush_cb(disp, flush);
  const fp_display *d = fpl_init(NULL, NULL);
  assert(d);

  static uint8_t icon[FE_ICON_BOX * FE_ICON_BOX];
  FEGridEntry ent[10];
  for (int i = 0; i < 10; i++) { ent[i].label = i % 2 ? "实时字幕" : "录音"; ent[i].icon_l8 = icon; }
  static FEAppsView g;
  assert(fe_apps_build(d, d->root(), &g, ent, 10, 0));
  settle();
  int worst = 0;
  for (int f = 0; f < 10; f++) {
    fe_apps_focus(&g, f); settle();
    int s = f % 6;
    FEGridPane *p = &g.pane[g.cur];
    int bx, by, by1, by2, ix, iy, iy1, iy2, lx, ly, ly1, ly2;
    box(g.bubble.obj, &bx, &by, &by1, &by2);
    box(p->icon[s], &ix, &iy, &iy1, &iy2);
    box(p->label[s], &lx, &ly, &ly1, &ly2);
    int dx1 = ab(bx - ix), dx2 = ab(bx - lx), dy = ab(by - (iy1 + ly2));
    if (dx1 > worst) worst = dx1;
    if (dx2 > worst) worst = dx2;
    if (dy > worst) worst = dy;
    printf("grid focus %d (page %d slot %d): bubble c=(%.1f,%.1f) icon cx=%.1f label cx=%.1f block cy=%.1f\n",
           f, g.page, s, bx / 2.0, by / 2.0, ix / 2.0, lx / 2.0, (iy1 + ly2) / 2.0);
    assert(dx1 <= 2 && dx2 <= 2 && dy <= 2);             /* centres doubled: <= 1 px */
    assert(iy1 >= by1 && ly2 <= by2);
  }
  fe_apps_free(&g);

  static FECcView cc;
  FECcValues val = { 9, 60, 82, true, false, true, false, false };
  assert(fe_cc_build(d, d->root(), &cc));
  for (int f = 0; f < FE_CC_N; f++) {
    fe_cc_update(&cc, f, true, false, &val); settle();
    int rx, ry, tx, ty;
    box(cc.ring.obj, &rx, &ry, 0, 0); box(cc.tile[f], &tx, &ty, 0, 0);
    printf("cc item %d: ring c=(%.1f,%.1f) tile c=(%.1f,%.1f)\n", f, rx / 2.0, ry / 2.0, tx / 2.0, ty / 2.0);
    assert(ab(rx - tx) <= 2 && ab(ry - ty) <= 2);
  }
  fe_cc_free(&cc);

  static FECardView cd;
  assert(fe_card_build(d, d->root(), &cd));
  for (int i = 0; i < 3; i++) {
    fe_card_focus(&cd, i); settle();
    int rx, ry, bx, by, gx, gy;
    box(cd.ring.obj, &rx, &ry, 0, 0); box(cd.btn[i], &bx, &by, 0, 0); box(cd.glyph[i], &gx, &gy, 0, 0);
    printf("card %d: ring c=(%.1f,%.1f) button c=(%.1f,%.1f) glyph c=(%.1f,%.1f)\n", i, rx / 2.0, ry / 2.0, bx / 2.0, by / 2.0, gx / 2.0, gy / 2.0);
    assert(ab(rx - bx) <= 2 && ab(ry - by) <= 2 && ab(gx - bx) <= 2 && ab(gy - by) <= 2);
  }
  fe_card_free(&cd);
  printf("lvgl centring: all cells/rings/buttons within 1 px (worst grid delta %.1f px)\n", worst / 2.0);
  return 0;
}
