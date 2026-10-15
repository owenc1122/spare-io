/*
 * fe_runtime.c — Spare front-end runtime glue. Clean-room, Spare original.
 *
 * Binds the pure state machine (fe_state), the app registry (fe_registry) and
 * the page builders (fe_pages) to the firmware through the frontend_port seam:
 *   - installs the button + encoder hooks,
 *   - decodes gestures into FEEvent and runs fe_step(),
 *   - executes the resulting FEFx effects (pages, settings, app lifecycle),
 *   - implements the public services in sg_front.h and the SDK's sg_app_launch.
 *
 * Grid order: the port's stock apps first (stock icons, launched by the port),
 * then every app registered through sg_app_register() (our SVG icon by id when
 * the app ships icon == NULL).
 *
 * Needs no vendor header. ALL module state (including the app table) lives in
 * one heap-allocated context (the module image has no writable statics). The
 * hooks receive it as `user`; the public SDK/sg_front entry points, which carry
 * no context argument, fetch it from the SG_CTX_FRONTEND slot (sg_ctx.h).
 */
#include "frontend_port.h"
#include "fe_state.h"
#include "fe_registry.h"
#include "fe_pages.h"
#include "fe_spring.h"
#include "sg_front.h"
#include "assets/icons/sg_app_icons.h"
#include "../../sdk/sg_sdk.h"
#include "../../sdk/sg_ctx.h"

#define FE_TAG "frontend"
#define FE_DETENT_MS 80u
#define FE_MAX_GRID (FE_GRID_PER_PAGE * FE_GRID_MAX_PAGES)

_Static_assert(SG_APP_ICON_BOX == FE_ICON_BOX, "icon art must match the grid box");

typedef struct {
  const frontend_port *P;
  FEReg     reg;                              /* registered apps (sg_app_register) */
  FEState   S;
  /* pages (at most one of cc/apps alive; card overlays an app) */
  FECcView   cc;   bool cc_on;
  FEAppsView apps; bool apps_on;
  FECardView card; bool card_on;
  /* pages animating out (freed by their after() callback, or early) */
  FECcView   cc_old;   bool cc_old_on;
  FEAppsView apps_old; bool apps_old_on;
  FECardView card_old; bool card_old_on;
  bool       off_pending;                    /* screen-off fade running */
  /* grid model */
  FEGridEntry grid[FE_MAX_GRID];
  int8_t      grid_reg[FE_MAX_GRID];          /* -1 stock, else registry index */
  int8_t      grid_stock[FE_MAX_GRID];
  int         grid_n;
  uint8_t     stock_icon[FE_GRID_PER_PAGE * 2][FE_ICON_BOX * FE_ICON_BOX];
  /* foreground app */
  int        fg_reg;                          /* registry index, -1 none/stock */
  void     (*app_on_rotate)(int32_t, void *);
  void     (*app_on_click)(void *);
  void      *app_input_user;
  void     (*cc_return_cb)(void *);
  void      *cc_return_ctx;
  uint32_t   last_detent;
} FERt;

/* Public entry points (no user argument) find the context here. */
static FERt *fe_rt(void) { return (FERt *)sg_ctx_get(SG_CTX_FRONTEND); }

/* ----------------------------- icons ------------------------------------- */
static int streq(const char *a, const char *b) {
  if (!a || !b) return 0;
  for (unsigned i = 0; i < 128; i++) { if (a[i] != b[i]) return 0; if (!a[i]) return 1; }
  return 0;
}
const uint8_t *fe_icon_for_id(const char *id) {
  if (streq(id, "com.spare.music"))    return sg_icon_music;
  if (streq(id, "com.spare.nav") || streq(id, "nav")) return sg_icon_nav;
  if (streq(id, "com.spare.reader"))   return sg_icon_reader;
  if (streq(id, "com.spare.pomodoro")) return sg_icon_pomodoro;
  return 0;
}

static void grid_model(FERt *rt) {
  const fp_apps *ap = rt->P->apps;
  int n = 0, stock = ap->stock_count ? ap->stock_count() : 0;
  for (int i = 0; i < stock && n < FE_MAX_GRID; i++, n++) {
    rt->grid[n].label = ap->stock_label(i);
    rt->grid[n].icon_l8 = 0;
    if (i < (int)(sizeof rt->stock_icon / sizeof rt->stock_icon[0]) &&
        ap->stock_icon_l8 && ap->stock_icon_l8(i, rt->stock_icon[i], FE_ICON_BOX))
      rt->grid[n].icon_l8 = rt->stock_icon[i];
    rt->grid_reg[n] = -1; rt->grid_stock[n] = (int8_t)i;
  }
  for (int i = 0; i < fe_reg_count(&rt->reg) && n < FE_MAX_GRID; i++, n++) {
    const sg_app *a = fe_reg_at(&rt->reg, i);
    rt->grid[n].label = a->title;
    rt->grid[n].icon_l8 = a->icon ? (const uint8_t *)a->icon : fe_icon_for_id(a->id);
    rt->grid_reg[n] = (int8_t)i; rt->grid_stock[n] = -1;
  }
  rt->grid_n = n;
  rt->S.apps_count = n > 0 ? n : 1;
}

/* ----------------------------- control-center values --------------------- */
static FECcValues cc_values(FERt *rt) {
  const fp_settings *s = rt->P->settings;
  FECcValues v;
  v.brightness_0_17 = s->brightness_get(); v.volume_0_100 = s->volume_get();
  v.battery_pct = s->battery_pct(); v.raise = s->raise_get(); v.dnd = s->dnd_get();
  v.bt = s->bt_connected(); v.charging = s->charging();
  v.return_to_app = rt->S.cc_return == FE_RET_APP;
  return v;
}
static void cc_repaint(FERt *rt) {
  if (!rt->cc_on) return;
  FECcValues v = cc_values(rt);
  fe_cc_update(&rt->cc, rt->S.cc_focus, rt->S.cc_select, rt->S.cc_adjust, &v);
}

static void reap_old(FERt *rt) {
  if (rt->cc_old_on)   { fe_cc_free(&rt->cc_old); rt->cc_old_on = false; }
  if (rt->apps_old_on) { fe_apps_free(&rt->apps_old); rt->apps_old_on = false; }
}
static void reap_cb(void *user) { reap_old((FERt *)user); }
static void card_reap_cb(void *user) {
  FERt *rt = (FERt *)user;
  if (rt->card_old_on) { fe_card_free(&rt->card_old); rt->card_old_on = false; }
}
/* abrupt teardown (launch, AI, screen off): no slide, watch face reset */
static void pages_clear(FERt *rt) {
  reap_old(rt);
  if (rt->cc_on)   { fe_cc_free(&rt->cc); rt->cc_on = false; }
  if (rt->apps_on) { fe_apps_free(&rt->apps); rt->apps_on = false; }
  fe_time_restore(rt->P->display);
}
static void card_clear(FERt *rt) {
  card_reap_cb(rt);
  if (rt->card_on) { fe_card_free(&rt->card); rt->card_on = false; }
}
/* animated returns to the watch face: the page slides out, then is freed */
static void cc_leave(FERt *rt) {
  if (!rt->cc_on) return;
  reap_old(rt);
  const fp_display *d = rt->P->display;
  if (d->anim_stop) d->anim_stop(&rt->cc.ring);
  rt->cc_old = rt->cc; rt->cc_old_on = true; rt->cc_on = false;
  fe_page_leave(d, rt->cc_old.root, -1, reap_cb, rt);
}
static void apps_leave(FERt *rt) {
  if (!rt->apps_on) return;
  reap_old(rt);
  const fp_display *d = rt->P->display;
  if (d->anim_stop) { d->anim_stop(&rt->apps.bubble); d->anim_stop(&rt->apps); }
  rt->apps_old = rt->apps; rt->apps_old_on = true; rt->apps_on = false;
  rt->apps_old.bubble.owner = &rt->apps_old;
  fe_page_leave(d, rt->apps_old.root, +1, reap_cb, rt);
}
static void card_leave(FERt *rt) {
  if (!rt->card_on) return;
  card_reap_cb(rt);
  const fp_display *d = rt->P->display;
  if (d->anim_stop) d->anim_stop(&rt->card.ring);
  rt->card_old = rt->card; rt->card_old_on = true; rt->card_on = false;
  rt->card_old.ring.owner = &rt->card_old;
  fe_card_close(&rt->card_old, card_reap_cb, rt);
}
static void power_off_cb(void *user) {
  FERt *rt = (FERt *)user;
  if (!rt->off_pending) return;
  rt->off_pending = false;
  rt->P->screen->set(false);
  fe_time_restore(rt->P->display);           /* invisible now; ready for wake */
}

static void forget_app(FERt *rt) {
  rt->fg_reg = -1; rt->app_on_rotate = 0; rt->app_on_click = 0; rt->app_input_user = 0;
  rt->cc_return_cb = 0; rt->cc_return_ctx = 0;
}

/* ----------------------------- effects ----------------------------------- */
static void run(FERt *rt, const FEFxList *fx) {
  const frontend_port *P = rt->P;
  const fp_display *d = P->display;
  for (unsigned i = 0; i < fx->n; i++) {
    int a = fx->fx[i].arg;
    switch (fx->fx[i].kind) {
    case FE_FX_SCREEN_OFF:
      pages_clear(rt); card_clear(rt);
      rt->off_pending = true;
      fe_screen_off_fade(d, power_off_cb, rt);     /* fade + 0.96, then panel off */
      break;
    case FE_FX_SCREEN_ON:  P->screen->set(true); P->screen->report_activity(); break;
    case FE_FX_OPEN_CC:
      if (rt->cc_on || rt->apps_on) pages_clear(rt);
      rt->cc_on = fe_cc_build(d, d->root(), &rt->cc);
      if (!rt->cc_on) fe_cc_free(&rt->cc);
      cc_repaint(rt);
      if (rt->cc_on && rt->S.cc_return == FE_RET_TIME) fe_page_enter(d, rt->cc.root, -1);  /* from the left */
      break;
    case FE_FX_CLOSE_CC:
      if (rt->S.page == FE_PAGE_TIME) cc_leave(rt);   /* slide back to the watch face */
      else if (rt->cc_on) { fe_cc_free(&rt->cc); rt->cc_on = false; }
      if (rt->cc_return_cb) { void (*cb)(void *) = rt->cc_return_cb; void *cx = rt->cc_return_ctx;
                              rt->cc_return_cb = 0; rt->cc_return_ctx = 0; cb(cx); }
      break;
    case FE_FX_CC_SET_SELECT: case FE_FX_CC_FOCUS: case FE_FX_CC_ADJUST_BEGIN:
      cc_repaint(rt); break;
    case FE_FX_CC_ADJUST_STEP:
      if (rt->S.cc_focus == FE_CC_BRIGHT) {
        int v = P->settings->brightness_get() + a; v = v < 1 ? 1 : v > 17 ? 17 : v;
        P->settings->brightness_set(v, false);
      } else if (rt->S.cc_focus == FE_CC_VOLUME) {
        int v = P->settings->volume_get() + a * 5; v = v < 0 ? 0 : v > 100 ? 100 : v;
        P->settings->volume_set(v);
      }
      cc_repaint(rt); break;
    case FE_FX_CC_ADJUST_END:
      if (a == FE_CC_BRIGHT) P->settings->brightness_set(P->settings->brightness_get(), true);
      cc_repaint(rt); break;
    case FE_FX_CC_ACTIVATE:
      if (a == FE_CC_RAISE) P->settings->raise_set(!P->settings->raise_get());
      else if (a == FE_CC_DND) P->settings->dnd_set(!P->settings->dnd_get());
      cc_repaint(rt); break;
    case FE_FX_OPEN_APPS:
      if (rt->cc_on || rt->apps_on) pages_clear(rt);
      grid_model(rt);
      rt->apps_on = fe_apps_build(d, d->root(), &rt->apps, rt->grid, rt->grid_n, rt->S.apps_focus);
      if (!rt->apps_on) fe_apps_free(&rt->apps);
      else fe_page_enter(d, rt->apps.root, +1);       /* from the right */
      break;
    case FE_FX_CLOSE_APPS: apps_leave(rt); break;
    case FE_FX_APPS_FOCUS: if (rt->apps_on) fe_apps_focus(&rt->apps, rt->S.apps_focus); break;
    case FE_FX_LAUNCH_APP:
      pages_clear(rt); forget_app(rt);
      if (a >= 0 && a < rt->grid_n && rt->grid_reg[a] >= 0) {
        const sg_app *app = fe_reg_at(&rt->reg, rt->grid_reg[a]);
        rt->fg_reg = rt->grid_reg[a];
        rt->S.card_enabled = !fe_reg_handles_click(app);
        if (app && app->on_start) app->on_start(d->root(), app->user);
      } else if (a >= 0 && a < rt->grid_n) {
        rt->S.card_enabled = true;                 /* stock apps keep the card */
        P->apps->stock_launch(rt->grid_stock[a]);
      }
      break;
    case FE_FX_SHOW_CARD:
      card_clear(rt);
      rt->card_on = fe_card_build(d, d->root(), &rt->card);
      if (!rt->card_on) fe_card_free(&rt->card);
      break;
    case FE_FX_HIDE_CARD:  card_leave(rt); break;
    case FE_FX_CARD_FOCUS: if (rt->card_on) fe_card_focus(&rt->card, a); break;
    case FE_FX_EXIT_APP: {
      card_leave(rt);
      const sg_app *app = rt->fg_reg >= 0 ? fe_reg_at(&rt->reg, rt->fg_reg) : 0;
      if (app && app->on_stop) app->on_stop(app->user);
      forget_app(rt);
      P->apps->back_home();
      break;
    }
    case FE_FX_OPEN_AI:        pages_clear(rt); card_clear(rt); P->assistant->open(); break;
    case FE_FX_DELEGATE_APP:   if (rt->app_on_rotate) rt->app_on_rotate(a, rt->app_input_user); break;
    case FE_FX_DELEGATE_CLICK: if (rt->app_on_click) rt->app_on_click(rt->app_input_user); break;
    case FE_FX_PASS_STOCK: case FE_FX_NONE: default: break;
    }
  }
}

static bool consumes(const FEFxList *fx) {
  for (unsigned i = 0; i < fx->n; i++) if (fx->fx[i].kind != FE_FX_PASS_STOCK) return true;
  return false;
}

/* If the user left a stock app by the app's own means, or the screen went
 * dark, the page the machine believes in may be stale: resync before input. */
static void resync(FERt *rt) {
  const frontend_port *P = rt->P;
  bool on = P->screen->is_on();
  if (!on && rt->S.page != FE_PAGE_SCREEN_OFF) {
    pages_clear(rt); card_clear(rt);
    rt->S.page = FE_PAGE_SCREEN_OFF; rt->S.card = false; rt->S.cc_select = rt->S.cc_adjust = false;
  } else if (on && rt->S.page == FE_PAGE_SCREEN_OFF) {
    rt->S.page = FE_PAGE_TIME;                      /* woken by the stock path */
    fe_time_restore(P->display);
  }
  if (rt->S.page == FE_PAGE_IN_APP && rt->fg_reg < 0 && P->apps->foreground() == 0 && !rt->S.card) {
    rt->S.page = FE_PAGE_TIME;                      /* stock app already closed */
  }
}

/* ----------------------------- hooks ------------------------------------- */
static bool on_key(fp_key key, void *user) {
  FERt *rt = (FERt *)user;
  if (rt->off_pending) return true;                 /* panel is fading out */
  resync(rt);
  if (rt->S.page == FE_PAGE_SCREEN_OFF && key == FP_KEY_CLICK) return false;  /* stock wake */
  FEFxList fx;
  fe_step(&rt->S, key == FP_KEY_LONG ? FE_EV_LONGPRESS : FE_EV_CLICK, &fx);
  run(rt, &fx);
  return consumes(&fx);
}

static void on_detent(int delta, void *user) {
  FERt *rt = (FERt *)user;
  if (!delta || rt->off_pending) return;
  uint32_t now = rt->P->display->tick_ms();
  if ((uint32_t)(now - rt->last_detent) < FE_DETENT_MS) return;
  rt->last_detent = now;
  resync(rt);
  delta *= FE_ENCODER_SIGN;   /* raw device detent -> logical (+1 = clockwise) */
  FEFxList fx;
  fe_step(&rt->S, delta > 0 ? FE_EV_ROTATE_CW : FE_EV_ROTATE_CCW, &fx);
  run(rt, &fx);
}

/* ----------------------------- public services --------------------------- */
sg_err_t sg_frontend_open_control_center(void (*return_cb)(void *user), void *user) {
  FERt *rt = fe_rt();
  if (!rt || rt->S.page != FE_PAGE_IN_APP) return SG_ERR_INVAL;
  rt->cc_return_cb = return_cb; rt->cc_return_ctx = user;
  FEFxList fx; fe_app_open_control_center(&rt->S, &fx); run(rt, &fx);
  return SG_OK;
}
sg_err_t sg_frontend_exit_app(void) {
  FERt *rt = fe_rt();
  if (!rt || rt->S.page != FE_PAGE_IN_APP) return SG_ERR_INVAL;
  FEFxList fx; fe_app_exit_to_time(&rt->S, &fx); run(rt, &fx);
  return SG_OK;
}
void sg_front_set_app_input(void (*on_rotate)(int32_t, void *), void (*on_click)(void *), void *user) {
  FERt *rt = fe_rt();
  if (rt) { rt->app_on_rotate = on_rotate; rt->app_on_click = on_click; rt->app_input_user = user; }
}

/* SDK contract: app table (forwarded to the runtime-owned FEReg). */
sg_err_t sg_app_register(const sg_app *app) {
  FERt *rt = fe_rt();
  return rt ? fe_reg_add(&rt->reg, app) : SG_ERR_AGAIN;
}
sg_err_t sg_app_unregister(const char *id) {
  FERt *rt = fe_rt();
  return rt ? fe_reg_remove(&rt->reg, id) : SG_ERR_AGAIN;
}

/* SDK contract: programmatic launch of a registered app. */
sg_err_t sg_app_launch(const char *id) {
  FERt *rt = fe_rt();
  if (!rt) return SG_ERR_AGAIN;
  const sg_app *app = fe_reg_find(&rt->reg, id);
  if (!app) return SG_ERR_NOTFOUND;
  pages_clear(rt); card_clear(rt); forget_app(rt);
  for (int i = 0; i < fe_reg_count(&rt->reg); i++) if (fe_reg_at(&rt->reg, i) == app) rt->fg_reg = i;
  rt->S.page = FE_PAGE_IN_APP; rt->S.card = false;
  rt->S.card_enabled = !fe_reg_handles_click(app);
  if (app->on_start) app->on_start(rt->P->display->root(), app->user);
  return SG_OK;
}

/* ----------------------------- init -------------------------------------- */
bool fe_runtime_init(const frontend_port *port) {
  if (!port || !port->input || !port->screen || !port->settings ||
      !port->apps || !port->assistant || !port->display || fe_rt()) return false;
  FERt *rt = (FERt *)sg_calloc(1, sizeof *rt);
  if (!rt) return false;
  rt->P = port;
  rt->fg_reg = -1;
  fe_init(&rt->S, FE_CC_N, FE_CC_SLIDER_MASK, 1);
  grid_model(rt);
  if (sg_ctx_set(SG_CTX_FRONTEND, rt) != SG_OK) { sg_free(rt); return false; }
  /* Keys are essential (they carry click / long-press, i.e. every control);
   * rotation is best-effort. A failure to wrap the encoder must not abort init
   * and strand the whole front-end, apps and crown keys. */
  if (!port->input->hook_keys(on_key, rt)) {
    SG_LOGE(FE_TAG, "key hook failed");
    sg_ctx_set(SG_CTX_FRONTEND, 0); sg_free(rt); return false;
  }
  if (!port->input->hook_encoder(on_detent, rt))
    SG_LOGE(FE_TAG, "encoder hook failed: rotation disabled");
  SG_LOGI(FE_TAG, "front-end v1 up: cc=%d grid=%d", FE_CC_N, rt->grid_n);
  return true;
}
