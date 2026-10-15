#ifndef FP_DISPLAY_LVGL_H
#define FP_DISPLAY_LVGL_H
/* fp_display_lvgl — the fp_display facade (frontend_port.h §6) implemented on
 * real LVGL v9.2. Clean-room, Spare original; no vendor names or addresses.
 *
 * One source for both backends:
 *   - host  : macOS preview renderer (backend-lvgl/host/), LVGL built from build/lvgl
 *   - device: compiled for thumbv8m against the base's LVGL v9 headers
 *
 * No file-scope mutable state (device modules have no .bss/.data): the only
 * state is a heap context hung on a paused LVGL timer we own (user_data,
 * tagged with a magic); per-canvas descriptors ride on the canvas's DELETE
 * event user data. The facade table itself is const (.rodata).
 *
 * Colours: the facade speaks L8 gray; the panel is monochrome green, so every
 * gray g is written as RGB 0x00gg00 (stock text is 0x00ff00). */
#include <stdbool.h>
#include <stdint.h>
#include "../frontend_port.h"

/* Font getter behind the port: returns an lv_font_t* (as void*) for a pixel
 * size + weight. Host: Noto Sans SC Regular/Bold via tiny_ttf. Device: the
 * base's menu font. */
typedef const void *(*fpl_font_fn)(unsigned px, fp_weight weight, void *user);

/* Create the root container on the active display and attach the context.
 * Returns the const facade table, or NULL on OOM. Idempotent. */
const fp_display *fpl_init(fpl_font_fn font, void *font_user);
/* The stock watch face object handed out by fp_display.time_page() (host: a
 * stand-in; device: the base's watch-face root). NULL = unavailable. */
void fpl_set_time_page(fp_obj time_page);
/* Delete the root container (and every page under it) + free the context. */
void fpl_deinit(void);

#endif
