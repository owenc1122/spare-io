#ifndef SG_FRONTEND_PORT_H
#define SG_FRONTEND_PORT_H
/*
 * frontend_port.h — the ONLY seam between the Spare front-end module and the
 * underlying RayNeo StrixOS firmware.
 *
 * Clean-room: this header is Spare's own design and names NO vendor functions,
 * addresses or offsets. It lists, as a table of function pointers, every vendor
 * capability the front-end needs. The clean-room firmware team implements one
 * translation unit that fills this table (typically thin wrappers over
 * firmware/sdk/sg_sdk.h) and hands it to fe_runtime_init(). The front-end
 * module can thus be built and host-tested with a mock port, and ported onto the
 * real base without touching any UI code.
 *
 * Threading: unless noted, every call is made on, and must run on, the UI
 * (LVGL) thread. The port's hook-install functions deliver events on that
 * thread too.
 */
#include <stdint.h>
#include <stdbool.h>

/* Opaque LVGL object handle; the port owns the concrete LVGL ABI. */
typedef void *fp_obj;

/* Decoded crown gestures the front-end consumes (see fe_state.h FEEvent). */
typedef enum { FP_KEY_CLICK = 0, FP_KEY_LONG = 1 } fp_key;

/* --- 1. Input: button interceptor + encoder (crown rotation) -------------- */
typedef struct {
  /* Install a global button interceptor. `on_key` returns true to CONSUME the
   * key (vendor/app does not see it), false to let it pass through. The front-end
   * uses this for single click / long press. */
  bool (*hook_keys)(bool (*on_key)(fp_key key, void *user), void *user);
  /* Subscribe to the crown ENCODER (separate device from the buttons). delta is
   * the RAW signed detent count exactly as the device encoder reports it. The
   * front-end converts it to its logical direction with FE_ENCODER_SIGN below
   * (in one place: fe_runtime.c on_detent), so ports never flip it. */
  bool (*hook_encoder)(void (*on_detent)(int delta, void *user), void *user);
} fp_input;

/* Raw encoder delta -> logical rotation (logical +1 = clockwise as the wearer
 * sees the crown). Verified on the real glasses (2026-10-13): the device
 * reports a clockwise detent as -1, so the sign is -1. With it applied, from
 * the time page counter-clockwise opens the Control Center, clockwise opens
 * the app list, and inside lists/grids/CC clockwise = next item. */
#ifndef FE_ENCODER_SIGN
#define FE_ENCODER_SIGN (-1)
#endif

/* --- 2. Screen power + activity ------------------------------------------- */
typedef struct {
  bool (*is_on)(void);
  void (*set)(bool on);        /* on=false => force screen off                */
  void (*report_activity)(void); /* reset the auto-sleep timer                */
} fp_screen;

/* --- 3. Control-center backends ------------------------------------------- */
typedef struct {
  /* brightness: device level 0..17 (see control-center research). is_final=false
   * while the user is still turning the crown (no persist/report spam). */
  int  (*brightness_get)(void);
  void (*brightness_set)(int level_0_17, bool is_final);
  /* media volume: user units 0..100 (port maps to the native 0..15 stream). */
  int  (*volume_get)(void);
  void (*volume_set)(int pct_0_100);
  /* raise-to-wake (head-up). The port must read-modify-write the wake config so
   * the companion degree/crown fields are preserved. */
  bool (*raise_get)(void);
  void (*raise_set)(bool on);
  /* do-not-disturb / focus mode. */
  bool (*dnd_get)(void);
  void (*dnd_set)(bool on);
  /* read-only status for the control-center tiles. */
  bool (*bt_connected)(void);
  int  (*battery_pct)(void);
  bool (*charging)(void);
} fp_settings;

/* --- 4. Stock app icons + stock app launch -------------------------------- */
/* The 7 built-in RayNeo apps keep their stock icons (frames of the vendor
 * launcher icon animation) and are launched by their stock index. The port
 * hides the vendor resource/launch details behind these calls; the front-end
 * never references vendor resource keys or ids directly. */
typedef struct {
  int  (*stock_count)(void);                 /* number of stock apps          */
  const char *(*stock_label)(int stock_idx); /* localized name                */
  /* render stock app `stock_idx`'s icon into an L8 box (box*box bytes); returns
   * false if unavailable (front-end then draws a placeholder). */
  bool (*stock_icon_l8)(int stock_idx, uint8_t *dst, unsigned box);
  void (*stock_launch)(int stock_idx);       /* open the stock app            */
  void (*back_home)(void);                   /* return to the launcher/home   */
  const char *(*foreground)(void);           /* current top app id, NULL=home */
} fp_apps;

/* --- 5. AI assistant (long press) ----------------------------------------- */
/* Opening the stock assistant card is a vendor capability; the companion data
 * path (phone-driven ASR/NLP) is handled by the assistant module, not here.
 * The front-end only asks the port to open/close the assistant presentation. */
typedef struct {
  void (*open)(void);   /* show the stock assistant card (long press)         */
  void (*close)(void);
  bool (*is_open)(void);
} fp_assistant;

/* Easing paths for the facade's property animations. The backend maps each to
 * an lv_anim path; fe_path_q10() (fe_pages.h) evaluates them identically so a
 * backend can simply call it from its path callback. */
typedef enum {
  FP_PATH_LINEAR = 0,
  FP_PATH_EASE_OUT,      /* cubic ease-out (info-panel fade)                */
  FP_PATH_EASE_OUT_QUAD, /* screen-off fade                                 */
  FP_PATH_SMOOTHSTEP,    /* card close                                      */
  FP_PATH_SPRING_MOVE,   /* fe_spring MOVE (overshooting, settles)          */
  FP_PATH_SPRING_POP,    /* fe_spring POP  (card scale-in)                  */
  FP_PATH_PAGE_CD        /* critically damped, no overshoot                 */
} fp_path;

/* Label font weight and text alignment (facade request #3/#4). */
typedef enum { FP_W_REGULAR = 0, FP_W_BOLD = 1 } fp_weight;
typedef enum { FP_ALIGN_CENTER = 0, FP_ALIGN_LEFT = 1, FP_ALIGN_RIGHT = 2 } fp_align;

/* --- 6. LVGL object + animation facade ------------------------------------ */
/* Enough of LVGL to build the pages without re-exposing the vendor LVGL ABI.
 * Coordinates are pixels in the 540x220 launcher screen. Colours are L8 gray
 * (0..255); the panel shows them as monochrome green. */
typedef struct {
  bool   (*ui_idle)(void);                    /* safe to mutate retained buffers */
  fp_obj (*root)(void);                       /* full-screen overlay parent      */
  fp_obj (*obj)(fp_obj parent);               /* plain rect, no style, no scroll */
  fp_obj (*label)(fp_obj parent, unsigned font_px, fp_weight weight); /* centred by default */
  fp_obj (*canvas)(fp_obj parent);
  void   (*pos_size)(fp_obj o, int x, int y, int w, int h);
  void   (*radius)(fp_obj o, int r);
  void   (*bg)(fp_obj o, uint8_t gray, uint8_t opa);       /* fill            */
  void   (*border)(fp_obj o, int width, uint8_t gray);     /* outline, 0=off  */
  void   (*text_gray)(fp_obj o, uint8_t gray);             /* label colour    */
  void   (*text)(fp_obj o, const char *utf8_static);       /* centred, 1 line */
  void   (*canvas_buf)(fp_obj o, const uint8_t *l8, unsigned w, unsigned h);
  void   (*opa)(fp_obj o, uint8_t opa);                    /* whole object    */
  void   (*visible)(fp_obj o, bool on);
  void   (*destroy)(fp_obj o);                             /* + children      */
  /* one-shot animation: exec(var, e) is called with e = elapsed ms 0..ms
   * (linear); the caller applies its own spring (fe_spring.h). anim_stop
   * cancels a running anim for `var`. */
  void   (*anim)(void *var, void (*exec)(void *, int32_t), uint32_t ms);
  void   (*anim_stop)(void *var);
  uint32_t (*tick_ms)(void);
  /* --- added 2026-10-13 (facade request #1) --------------------------------
   * scale: 256 == 1.0, pivot = object centre, children scale with the object.
   * anim_scale / anim_opa: property tweens from -> to over ms along `path`;
   * a new tween of the same property on the same object replaces the old one;
   * anim_stop(obj) cancels them too.
   * after: one-shot callback on the UI thread after `ms` (used to finish a
   * fade before powering the panel off or deleting a closing card). */
  void   (*scale)(fp_obj o, int q8);
  void   (*anim_scale)(fp_obj o, int from_q8, int to_q8, uint32_t ms, fp_path path);
  void   (*anim_opa)(fp_obj o, uint8_t from, uint8_t to, uint32_t ms, fp_path path);
  void   (*after)(uint32_t ms, void (*cb)(void *user), void *user);
  /* --- added 2026-10-13 (facade requests #2-#6) -------------------------- */
  /* #2 the stock watch face as an object the front-end may translate/fade
   *    (slides time <-> CC/apps, screen-off fade). NULL if unavailable. The
   *    front-end never deletes or restyles it; it only sets translate/opa/scale
   *    and always restores 0 / 255 / 256 when it returns to the time page. */
  fp_obj (*time_page)(void);
  void   (*translate_x)(fp_obj o, int px);                 /* render offset  */
  void   (*anim_translate_x)(fp_obj o, int from_px, int to_px, uint32_t ms, fp_path path);
  /* #4 text alignment inside the label box */
  void   (*text_align)(fp_obj o, fp_align a);
  /* #5 clip children to this object's rounded corners (slider: flat-top fill
   *    inside a rounded track) */
  void   (*clip_corner)(fp_obj o, bool on);
  /* #6 image primitive: an A8 alpha glyph tinted with a gray level (L8 panel).
   *    a8 points at w*h static bytes. */
  fp_obj (*image)(fp_obj parent);
  void   (*image_a8)(fp_obj o, const uint8_t *a8, unsigned w, unsigned h);
  void   (*image_gray)(fp_obj o, uint8_t gray);
} fp_display;

/* The whole port, filled by the base and handed to fe_runtime_init(). */
typedef struct {
  const fp_input     *input;
  const fp_screen    *screen;
  const fp_settings  *settings;
  const fp_apps      *apps;
  const fp_assistant *assistant;
  const fp_display   *display;
} frontend_port;

/* Bring the front-end up on a filled port. Installs hooks, seeds the stock apps
 * into the registry, and shows the time page. Returns false on bad port. */
bool fe_runtime_init(const frontend_port *port);

#endif
