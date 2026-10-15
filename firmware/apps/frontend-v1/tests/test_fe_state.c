/* Host unit test for the Spare front-end v1 reducer. Covers every transition
 * in docs/frontend-v1.md: time/screen-off, control center enter+focus+wrap+
 * adjust+activate+return-target (time vs app), app grid focus+launch+back,
 * in-app card (back/CC/exit) + rotation delegation, and long-press AI from
 * every page. Built with -Werror -fsanitize=address,undefined by build.py. */
#include "fe_state.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static FEFxList L;
static FEFx step(FEState *s, FEEvent ev) { fe_step(s, ev, &L); return L.n ? L.fx[0] : (FEFx){FE_FX_NONE, 0}; }
static int has(FEFxKind k) { for (unsigned i = 0; i < L.n; i++) if (L.fx[i].kind == k) return 1; return 0; }
static int argof(FEFxKind k) { for (unsigned i = 0; i < L.n; i++) if (L.fx[i].kind == k) return L.fx[i].arg; return -999; }

/* CC items: 0 Back, 1 brightness(slider), 2 volume(slider), 3 raise-to-wake,
 * 4 DND, 5 Bluetooth. mask marks sliders 1 and 2. */
enum { CC_ITEMS = 6, APPS = 7 };
static const uint32_t SLIDERS = (1u << 1) | (1u << 2);

static void reset(FEState *s) { fe_init(s, CC_ITEMS, SLIDERS, APPS); }

static void test_time_screen(void) {
  FEState s; reset(&s);
  step(&s, FE_EV_CLICK);
  assert(s.page == FE_PAGE_SCREEN_OFF && has(FE_FX_SCREEN_OFF));
  step(&s, FE_EV_ROTATE_CW); /* rotation while off must not change page */
  assert(s.page == FE_PAGE_SCREEN_OFF && has(FE_FX_PASS_STOCK));
  step(&s, FE_EV_CLICK);
  assert(s.page == FE_PAGE_TIME && has(FE_FX_SCREEN_ON));
}

static void test_cc_open_close_and_return_time(void) {
  FEState s; reset(&s);
  step(&s, FE_EV_ROTATE_CCW);
  assert(s.page == FE_PAGE_CC && !s.cc_select && s.cc_return == FE_RET_TIME && has(FE_FX_OPEN_CC));
  /* rotate back (CW) before entering select closes CC back to time */
  step(&s, FE_EV_ROTATE_CW);
  assert(s.page == FE_PAGE_TIME && has(FE_FX_CLOSE_CC));
  /* re-open, click to take focus, rotate to move, back item closes to time */
  step(&s, FE_EV_ROTATE_CCW);
  step(&s, FE_EV_CLICK);
  assert(s.cc_select && s.cc_focus == FE_CC_BACK && has(FE_FX_CC_SET_SELECT) && has(FE_FX_CC_FOCUS));
  step(&s, FE_EV_ROTATE_CW); assert(s.cc_focus == 1 && argof(FE_FX_CC_FOCUS) == 1);
  step(&s, FE_EV_ROTATE_CCW); assert(s.cc_focus == 0);
  /* wrap backwards from Back -> last item */
  step(&s, FE_EV_ROTATE_CCW); assert(s.cc_focus == CC_ITEMS - 1);
  /* wrap forward -> Back */
  step(&s, FE_EV_ROTATE_CW); assert(s.cc_focus == 0);
  step(&s, FE_EV_CLICK); /* Back */
  assert(s.page == FE_PAGE_TIME && has(FE_FX_CLOSE_CC) && !s.cc_select);
}

static void test_cc_slider_and_toggle(void) {
  FEState s; reset(&s);
  step(&s, FE_EV_ROTATE_CCW); step(&s, FE_EV_CLICK); /* select mode */
  step(&s, FE_EV_ROTATE_CW);  /* focus brightness (slider, idx 1) */
  assert(s.cc_focus == 1);
  step(&s, FE_EV_CLICK);      /* enter adjust */
  assert(s.cc_adjust && has(FE_FX_CC_ADJUST_BEGIN) && argof(FE_FX_CC_ADJUST_BEGIN) == 1);
  step(&s, FE_EV_ROTATE_CW);  assert(has(FE_FX_CC_ADJUST_STEP) && argof(FE_FX_CC_ADJUST_STEP) == +1 && s.cc_adjust);
  step(&s, FE_EV_ROTATE_CCW); assert(argof(FE_FX_CC_ADJUST_STEP) == -1);
  step(&s, FE_EV_CLICK);      /* commit */
  assert(!s.cc_adjust && has(FE_FX_CC_ADJUST_END));
  /* move to a non-slider toggle (idx 3 raise-to-wake) -> activate */
  step(&s, FE_EV_ROTATE_CW); step(&s, FE_EV_ROTATE_CW); assert(s.cc_focus == 3);
  step(&s, FE_EV_CLICK);
  assert(has(FE_FX_CC_ACTIVATE) && argof(FE_FX_CC_ACTIVATE) == 3 && !s.cc_adjust);
}

static void test_apps_grid(void) {
  FEState s; reset(&s);
  step(&s, FE_EV_ROTATE_CW);
  assert(s.page == FE_PAGE_APPS && s.apps_focus == 0 && has(FE_FX_OPEN_APPS));
  step(&s, FE_EV_ROTATE_CW); assert(s.apps_focus == 1 && argof(FE_FX_APPS_FOCUS) == +1);
  /* rotate CCW back to the first icon then once more -> leave to time */
  step(&s, FE_EV_ROTATE_CCW); assert(s.apps_focus == 0);
  step(&s, FE_EV_ROTATE_CCW); assert(s.page == FE_PAGE_TIME && has(FE_FX_CLOSE_APPS));
  /* clamp at the far end */
  step(&s, FE_EV_ROTATE_CW);
  for (int i = 0; i < APPS + 3; i++) step(&s, FE_EV_ROTATE_CW);
  assert(s.apps_focus == APPS - 1);
  step(&s, FE_EV_CLICK);
  assert(s.page == FE_PAGE_IN_APP && !s.card && has(FE_FX_LAUNCH_APP) && argof(FE_FX_LAUNCH_APP) == APPS - 1);
}

static void enter_app(FEState *s) { reset(s); step(s, FE_EV_ROTATE_CW); step(s, FE_EV_CLICK); }

static void test_in_app_card_back_and_delegate(void) {
  FEState s; enter_app(&s);
  /* rotation while app owns the screen is delegated, not a focus move */
  step(&s, FE_EV_ROTATE_CW);
  assert(s.page == FE_PAGE_IN_APP && !s.card && has(FE_FX_DELEGATE_APP) && fe_rotation_is_app(&s));
  step(&s, FE_EV_CLICK);
  assert(s.card && s.card_focus == 0 && has(FE_FX_SHOW_CARD) && !fe_rotation_is_app(&s));
  /* Back (default focus) dismisses the card, keeps the app */
  step(&s, FE_EV_CLICK);
  assert(!s.card && s.page == FE_PAGE_IN_APP && has(FE_FX_HIDE_CARD));
}

static void test_in_app_card_cc_then_return_app(void) {
  FEState s; enter_app(&s);
  step(&s, FE_EV_CLICK);        /* card */
  step(&s, FE_EV_ROTATE_CW);    /* focus Control Center (idx 1) */
  assert(s.card_focus == 1 && argof(FE_FX_CARD_FOCUS) == 1);
  step(&s, FE_EV_CLICK);        /* open CC from app */
  assert(s.page == FE_PAGE_CC && s.cc_return == FE_RET_APP && !s.card && has(FE_FX_HIDE_CARD) && has(FE_FX_OPEN_CC));
  /* take focus, choose Back -> returns to the app, not the time page */
  step(&s, FE_EV_CLICK);        /* select */
  step(&s, FE_EV_CLICK);        /* Back */
  assert(s.page == FE_PAGE_IN_APP && has(FE_FX_CLOSE_CC) && !s.card);
}

static void test_in_app_card_exit(void) {
  FEState s; enter_app(&s);
  step(&s, FE_EV_CLICK);        /* card */
  step(&s, FE_EV_ROTATE_CCW);   /* wrap back to Exit (idx 2) */
  assert(s.card_focus == 2);
  step(&s, FE_EV_CLICK);
  assert(s.page == FE_PAGE_TIME && has(FE_FX_EXIT_APP) && has(FE_FX_HIDE_CARD));
}

static void test_self_handling_app(void) {
  /* App with SG_APP_HANDLES_CLICK: runtime clears card_enabled after launch.
   * Click is delegated to the app (no card); rotation still delegated. */
  FEState s; enter_app(&s);
  s.card_enabled = false;                 /* runtime sets this post-launch */
  step(&s, FE_EV_ROTATE_CW);
  assert(has(FE_FX_DELEGATE_APP) && !s.card);
  step(&s, FE_EV_CLICK);
  assert(has(FE_FX_DELEGATE_CLICK) && !s.card && s.page == FE_PAGE_IN_APP);
  /* the app opens the control center via the public API; Back returns to app */
  fe_app_open_control_center(&s, &L);
  assert(s.page == FE_PAGE_CC && s.cc_return == FE_RET_APP && has(FE_FX_OPEN_CC));
  step(&s, FE_EV_CLICK);                   /* select */
  step(&s, FE_EV_CLICK);                   /* Back */
  assert(s.page == FE_PAGE_IN_APP && has(FE_FX_CLOSE_CC));
  /* app exits to the time page via the public API */
  fe_app_exit_to_time(&s, &L);
  assert(s.page == FE_PAGE_TIME && has(FE_FX_EXIT_APP));
  /* long press still opens AI for a self-handling app */
  enter_app(&s); s.card_enabled = false; step(&s, FE_EV_LONGPRESS);
  assert(has(FE_FX_OPEN_AI));
}

static void test_longpress_everywhere(void) {
  FEState s;
  /* TIME */ reset(&s); step(&s, FE_EV_LONGPRESS);
  assert(has(FE_FX_OPEN_AI) && s.page == FE_PAGE_TIME);
  /* SCREEN_OFF -> wake + AI */ reset(&s); step(&s, FE_EV_CLICK); step(&s, FE_EV_LONGPRESS);
  assert(has(FE_FX_SCREEN_ON) && has(FE_FX_OPEN_AI) && s.page == FE_PAGE_TIME);
  /* CC browse */ reset(&s); step(&s, FE_EV_ROTATE_CCW); step(&s, FE_EV_LONGPRESS);
  assert(has(FE_FX_OPEN_AI) && s.page == FE_PAGE_CC);
  /* CC adjust */ reset(&s); step(&s, FE_EV_ROTATE_CCW); step(&s, FE_EV_CLICK); step(&s, FE_EV_ROTATE_CW); step(&s, FE_EV_CLICK); step(&s, FE_EV_LONGPRESS);
  assert(has(FE_FX_OPEN_AI));
  /* APPS */ reset(&s); step(&s, FE_EV_ROTATE_CW); step(&s, FE_EV_LONGPRESS);
  assert(has(FE_FX_OPEN_AI) && s.page == FE_PAGE_APPS);
  /* IN_APP */ enter_app(&s); step(&s, FE_EV_LONGPRESS);
  assert(has(FE_FX_OPEN_AI) && s.page == FE_PAGE_IN_APP);
  /* IN_APP card open */ enter_app(&s); step(&s, FE_EV_CLICK); step(&s, FE_EV_LONGPRESS);
  assert(has(FE_FX_OPEN_AI) && s.card);
}

/* Determinism: identical (state,event) yields identical effects. */
static void test_deterministic(void) {
  FEState a, b; memset(&a, 0, sizeof a); memset(&b, 0, sizeof b); reset(&a); reset(&b);
  const FEEvent seq[] = {FE_EV_ROTATE_CW, FE_EV_CLICK, FE_EV_CLICK, FE_EV_ROTATE_CCW,
                         FE_EV_ROTATE_CW, FE_EV_LONGPRESS, FE_EV_CLICK};
  for (unsigned i = 0; i < sizeof seq / sizeof *seq; i++) {
    FEFxList la, lb; fe_step(&a, seq[i], &la); fe_step(&b, seq[i], &lb);
    assert(la.n == lb.n && !memcmp(la.fx, lb.fx, la.n * sizeof *la.fx) && !memcmp(&a, &b, sizeof a));
  }
}

int main(void) {
  test_time_screen();
  test_cc_open_close_and_return_time();
  test_cc_slider_and_toggle();
  test_apps_grid();
  test_in_app_card_back_and_delegate();
  test_in_app_card_cc_then_return_app();
  test_in_app_card_exit();
  test_self_handling_app();
  test_longpress_everywhere();
  test_deterministic();
  printf("fe_state: all transition tests passed\n");
  return 0;
}
