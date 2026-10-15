/* Spare front-end v1 — pure interaction reducer. See fe_state.h.
 * No LVGL/native/OS/heap use: this file compiles for both the host test and
 * the ARM candidate from the identical source. */
#include "fe_state.h"

static void push(FEFxList *o, FEFxKind k, int arg) {
  if (o->n < FE_MAX_FX) { o->fx[o->n].kind = k; o->fx[o->n].arg = arg; o->n++; }
}

void fe_init(FEState *s, int cc_items, uint32_t slider_mask, int apps_count) {
  s->page = FE_PAGE_TIME;
  s->cc_select = false;
  s->cc_adjust = false;
  s->cc_focus = FE_CC_BACK;
  s->cc_items = cc_items < 1 ? 1 : cc_items;
  s->cc_slider_mask = slider_mask;
  s->cc_return = FE_RET_TIME;
  s->apps_focus = 0;
  s->apps_count = apps_count < 1 ? 1 : apps_count;
  s->card_enabled = true;
  s->card = false;
  s->card_focus = 0;
}

bool fe_rotation_is_app(const FEState *s) {
  return s->page == FE_PAGE_IN_APP && !s->card;
}

static bool cc_is_slider(const FEState *s, int i) {
  return i >= 0 && i < 32 && (s->cc_slider_mask & (1u << i)) != 0;
}

/* Open the control center from TIME or from inside an app (via the card). */
static void open_cc(FEState *s, FEReturn ret, FEFxList *o) {
  s->page = FE_PAGE_CC;
  s->cc_select = false;
  s->cc_adjust = false;
  s->cc_focus = FE_CC_BACK;
  s->cc_return = ret;
  push(o, FE_FX_OPEN_CC, 0);
}

static void close_cc(FEState *s, FEFxList *o) {
  push(o, FE_FX_CLOSE_CC, 0);
  if (s->cc_return == FE_RET_APP) {
    s->page = FE_PAGE_IN_APP;   /* app still underneath; card already hidden */
    s->card = false;
  } else {
    s->page = FE_PAGE_TIME;
  }
  s->cc_select = false;
  s->cc_adjust = false;
}

void fe_step(FEState *s, FEEvent ev, FEFxList *out) {
  out->n = 0;

  /* Long press is the one gesture that behaves identically on every page:
   * open the stock AI assistant overlay. It overlays the current page (the
   * AI layer handles its own keys until it closes), so page is preserved,
   * except from screen-off where we first wake into the time page. */
  if (ev == FE_EV_LONGPRESS) {
    if (s->page == FE_PAGE_SCREEN_OFF) { s->page = FE_PAGE_TIME; push(out, FE_FX_SCREEN_ON, 0); }
    push(out, FE_FX_OPEN_AI, 0);
    return;
  }

  switch (s->page) {

  case FE_PAGE_TIME:
    if (ev == FE_EV_ROTATE_CCW)      open_cc(s, FE_RET_TIME, out);
    else if (ev == FE_EV_ROTATE_CW){ s->page = FE_PAGE_APPS; s->apps_focus = 0; push(out, FE_FX_OPEN_APPS, 0); }
    else if (ev == FE_EV_CLICK)   { s->page = FE_PAGE_SCREEN_OFF; push(out, FE_FX_SCREEN_OFF, 0); }
    break;

  case FE_PAGE_SCREEN_OFF:
    if (ev == FE_EV_CLICK) { s->page = FE_PAGE_TIME; push(out, FE_FX_SCREEN_ON, 0); }
    else                   push(out, FE_FX_PASS_STOCK, 0); /* rotation: keep stock crown-wake */
    break;

  case FE_PAGE_CC:
    if (!s->cc_select) {
      /* browse mode: must click to take focus; rotate-back closes */
      if (ev == FE_EV_ROTATE_CW)       close_cc(s, out); /* reverse of the CCW that opened it */
      else if (ev == FE_EV_CLICK)    { s->cc_select = true; s->cc_focus = FE_CC_BACK;
                                       push(out, FE_FX_CC_SET_SELECT, 1); push(out, FE_FX_CC_FOCUS, s->cc_focus); }
      /* ROTATE_CCW while already at CC: nothing */
    } else if (s->cc_adjust) {
      if (ev == FE_EV_ROTATE_CW)       push(out, FE_FX_CC_ADJUST_STEP, +1);
      else if (ev == FE_EV_ROTATE_CCW) push(out, FE_FX_CC_ADJUST_STEP, -1);
      else if (ev == FE_EV_CLICK)    { s->cc_adjust = false; push(out, FE_FX_CC_ADJUST_END, s->cc_focus); }
    } else {
      if (ev == FE_EV_ROTATE_CW)     { s->cc_focus = (s->cc_focus + 1) % s->cc_items; push(out, FE_FX_CC_FOCUS, s->cc_focus); }
      else if (ev == FE_EV_ROTATE_CCW){ s->cc_focus = (s->cc_focus - 1 + s->cc_items) % s->cc_items; push(out, FE_FX_CC_FOCUS, s->cc_focus); }
      else if (ev == FE_EV_CLICK) {
        if (s->cc_focus == FE_CC_BACK)        close_cc(s, out);
        else if (cc_is_slider(s, s->cc_focus)){ s->cc_adjust = true; push(out, FE_FX_CC_ADJUST_BEGIN, s->cc_focus); }
        else                                   push(out, FE_FX_CC_ACTIVATE, s->cc_focus);
      }
    }
    break;

  case FE_PAGE_APPS:
    if (ev == FE_EV_ROTATE_CW) {
      if (s->apps_focus < s->apps_count - 1) { s->apps_focus++; push(out, FE_FX_APPS_FOCUS, +1); }
    } else if (ev == FE_EV_ROTATE_CCW) {
      if (s->apps_focus > 0)                 { s->apps_focus--; push(out, FE_FX_APPS_FOCUS, -1); }
      else                                   { s->page = FE_PAGE_TIME; push(out, FE_FX_CLOSE_APPS, 0); }
    } else if (ev == FE_EV_CLICK) {
      s->page = FE_PAGE_IN_APP; s->card = false; s->card_focus = 0;
      push(out, FE_FX_LAUNCH_APP, s->apps_focus);
    }
    break;

  case FE_PAGE_IN_APP:
    if (!s->card) {
      if (ev == FE_EV_CLICK) {
        if (s->card_enabled) { s->card = true; s->card_focus = 0; push(out, FE_FX_SHOW_CARD, 0); }
        else                 push(out, FE_FX_DELEGATE_CLICK, 0); /* app handles its own click */
      } else {
        push(out, FE_FX_DELEGATE_APP, ev == FE_EV_ROTATE_CW ? +1 : -1); /* in-app rotate -> app focus */
      }
    } else {
      if (ev == FE_EV_ROTATE_CW)       { s->card_focus = (s->card_focus + 1) % 3; push(out, FE_FX_CARD_FOCUS, s->card_focus); }
      else if (ev == FE_EV_ROTATE_CCW) { s->card_focus = (s->card_focus + 2) % 3; push(out, FE_FX_CARD_FOCUS, s->card_focus); }
      else if (ev == FE_EV_CLICK) {
        if (s->card_focus == 0)      { s->card = false; push(out, FE_FX_HIDE_CARD, 0); }          /* Back */
        else if (s->card_focus == 1) { s->card = false; push(out, FE_FX_HIDE_CARD, 0); open_cc(s, FE_RET_APP, out); } /* Control Center */
        else                         { s->card = false; push(out, FE_FX_HIDE_CARD, 0); push(out, FE_FX_EXIT_APP, 0); s->page = FE_PAGE_TIME; } /* Exit */
      }
    }
    break;

  case FE_PAGE_AI:
  default:
    push(out, FE_FX_PASS_STOCK, 0);
    break;
  }
}

void fe_app_open_control_center(FEState *s, FEFxList *out) {
  out->n = 0;
  if (s->page != FE_PAGE_IN_APP) return;
  s->card = false;
  open_cc(s, FE_RET_APP, out);   /* CC's Back returns to this app */
}

void fe_app_exit_to_time(FEState *s, FEFxList *out) {
  out->n = 0;
  if (s->page != FE_PAGE_IN_APP) return;
  s->card = false;
  s->page = FE_PAGE_TIME;
  push(out, FE_FX_EXIT_APP, 0);
}
