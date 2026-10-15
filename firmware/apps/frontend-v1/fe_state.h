#ifndef SPARE_FRONTEND_STATE_H
#define SPARE_FRONTEND_STATE_H
/* Spare front-end v1 — pure, host+ARM-testable interaction state machine.
 *
 * This module owns NO LVGL, native or OS state. It is a deterministic reducer:
 * the device layer (fe_runtime.c) feeds it decoded crown events and reads back
 * a list of effects to execute against the stock launcher / our native pages.
 * Keeping it pure lets the full transition table (time/screen-off, control
 * center enter+focus+adjust+return-target, app grid + bubble focus, in-app
 * floating card, long-press AI from every page) be proven offline with no
 * firmware, exactly like ai_decide()/sg_receive() in the shipped build.
 *
 * Crown decode (see docs/fw-input-notes.md): the single click / long press /
 * (unused) double arrive as LVGL keys through onInputEvent; rotation arrives
 * separately through the raw-wheel listener as a signed detent delta. The
 * runtime converts both into FEEvent values below before calling fe_step(). */
#include <stdint.h>
#include <stdbool.h>

/* Logical pages. SCREEN_OFF is a distinct page so a click there only wakes. */
typedef enum {
  FE_PAGE_TIME = 0,     /* stock watch face (unchanged look)                 */
  FE_PAGE_SCREEN_OFF,   /* display asleep, reached by click on the time page */
  FE_PAGE_CC,           /* control center, browse mode (focus not yet taken) */
  FE_PAGE_APPS,         /* app grid (6 per screen), bubble selection         */
  FE_PAGE_IN_APP,       /* a launched app owns the screen                    */
  FE_PAGE_AI            /* stock AI assistant overlay (long press)           */
} FEPage;

/* Events after decode. Rotation carries a direction; magnitude is one detent
 * per event (the runtime emits one event per settled detent). */
typedef enum {
  FE_EV_ROTATE_CW = 0,  /* clockwise  */
  FE_EV_ROTATE_CCW,     /* counter-clockwise */
  FE_EV_CLICK,          /* single click (LVGL key 0x3a) */
  FE_EV_LONGPRESS       /* long press  (LVGL key 0x3b while screen on) */
} FEEvent;

/* Who the control center returns to when its Back item is chosen. */
typedef enum { FE_RET_TIME = 0, FE_RET_APP } FEReturn;

/* Effects the runtime must execute, in order, after a step. At most FE_MAX_FX
 * per event (bounded; no allocation). */
typedef enum {
  FE_FX_NONE = 0,
  FE_FX_SCREEN_OFF,     /* lv_rayneo_display_force_screen_off            */
  FE_FX_SCREEN_ON,      /* lv_rayneo_display_screen_on(true)             */
  FE_FX_OPEN_CC,        /* build + show the control center page          */
  FE_FX_CLOSE_CC,       /* tear the control center down                  */
  FE_FX_CC_SET_SELECT,  /* arg = 1 selection mode on (focus taken), 0 off*/
  FE_FX_CC_FOCUS,       /* arg = new focused item index                  */
  FE_FX_CC_ADJUST_BEGIN,/* arg = item index; enter value-adjust          */
  FE_FX_CC_ADJUST_STEP, /* arg = signed detent (+1 / -1)                 */
  FE_FX_CC_ADJUST_END,  /* commit the current item's value               */
  FE_FX_CC_ACTIVATE,    /* arg = item index; toggle/invoke a non-adjust item */
  FE_FX_OPEN_APPS,      /* build + show the app grid                     */
  FE_FX_CLOSE_APPS,     /* tear the app grid down                        */
  FE_FX_APPS_FOCUS,     /* arg = signed detent; runtime moves bubble + page */
  FE_FX_LAUNCH_APP,     /* arg = focused grid index; open that app       */
  FE_FX_SHOW_CARD,      /* in-app floating 3-button card, focus = Back   */
  FE_FX_HIDE_CARD,      /* dismiss the card, keep the app                */
  FE_FX_CARD_FOCUS,     /* arg = new card button index (0..2)            */
  FE_FX_EXIT_APP,       /* quit the foreground app, go home              */
  FE_FX_OPEN_AI,        /* show stock assistant overlay (long press)     */
  FE_FX_DELEGATE_APP,   /* pass a rotation detent on to the foreground app (arg=+/-1) */
  FE_FX_DELEGATE_CLICK, /* pass a single click to a self-handling app    */
  FE_FX_PASS_STOCK      /* not ours this state: run stock onInputEvent   */
} FEFxKind;

enum { FE_MAX_FX = 4 };

/* Control-center item layout. Index 0 is always Back (spec item 2). The rest
 * are device toggles/sliders; the runtime maps indices to real services and
 * reports how many exist so focus wraps correctly. Adjustable items (sliders:
 * brightness, volume) use the ADJUST_* effects; the others use ACTIVATE. */
#ifndef FE_CC_BACK
#define FE_CC_BACK 0   /* shared with fe_pages.h */
#endif

typedef struct { FEFxKind kind; int arg; } FEFx;
typedef struct { FEFx fx[FE_MAX_FX]; unsigned n; } FEFxList;

/* The whole interaction state. POD, copyable, no pointers. */
typedef struct {
  FEPage   page;
  /* control center */
  bool     cc_select;      /* focus taken (rotation moves focus)          */
  bool     cc_adjust;      /* inside a slider value edit                  */
  int      cc_focus;       /* 0..cc_items-1                               */
  int      cc_items;       /* number of CC items incl. Back (>=1)         */
  uint32_t cc_slider_mask; /* bit i set => CC item i is an adjustable slider
                              (brightness/volume); others are toggles      */
  FEReturn cc_return;      /* where Back / close goes                     */
  /* app grid */
  int      apps_focus;     /* 0..apps_count-1                             */
  int      apps_count;     /* number of installed app entries (>=1)       */
  /* in-app */
  bool     card_enabled;   /* true: in-app click shows the 3-button card;
                              false: app handles its own clicks (SG_APP_HANDLES_CLICK)
                              — the runtime sets this after each FE_FX_LAUNCH_APP  */
  bool     card;           /* card visible                               */
  int      card_focus;     /* 0 Back, 1 Control Center, 2 Exit           */
} FEState;

/* Reset to the time page. cc_items/apps_count are supplied by the runtime from
 * the live device (installed app count, available CC toggles). slider_mask
 * marks which CC items are value sliders (click = enter adjust). */
void fe_init(FEState *s, int cc_items, uint32_t slider_mask, int apps_count);

/* Advance the machine by one decoded event, filling *out with the effects to
 * run. Pure: identical (state,event) always yields identical (state,effects). */
void fe_step(FEState *s, FEEvent ev, FEFxList *out);

/* Convenience for the runtime/tests: is the foreground an app we must let the
 * app see rotation for (in-app rotate always goes to the app's own focus)? */
bool fe_rotation_is_app(const FEState *s);

/* External (non-crown) intents a self-handling app invokes through the public
 * launcher API (sg_front_open_control_center / sg_front_exit_to_time). They act
 * only when a foreground app is current. */
void fe_app_open_control_center(FEState *s, FEFxList *out); /* CC, Back -> this app */
void fe_app_exit_to_time(FEState *s, FEFxList *out);        /* quit app -> time page */

#endif
