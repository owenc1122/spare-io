/* fe_registry — implementation of the launcher app table. See fe_registry.h.
 * Pure (no OS/LVGL/vendor); the UI thread is the only caller, so no locking.
 * All state is in the caller's FEReg (no module statics). */
#include "fe_registry.h"
#include "sg_front.h"

static int id_eq(const char *a, const char *b) {
  if (!a || !b) return 0;
  for (unsigned i = 0; i < 128; i++) { if (a[i] != b[i]) return 0; if (!a[i]) return 1; }
  return 0;
}
static int idx_of(const FEReg *r, const char *id) {
  for (int i = 0; i < r->count; i++) if (id_eq(r->apps[i].id, id)) return i;
  return -1;
}
static void notify(FEReg *r) { if (r->cb) r->cb(r->cb_ctx); }

sg_err_t fe_reg_add(FEReg *r, const sg_app *app) {
  if (!r || !app || !app->id || !app->on_start || !app->on_stop) return SG_ERR_INVAL;
  int i = idx_of(r, app->id);
  if (i >= 0) { r->apps[i] = *app; notify(r); return SG_OK; }   /* replace in place */
  if (r->count >= FE_APP_MAX) return SG_ERR_NOMEM;
  r->apps[r->count++] = *app;
  notify(r);
  return SG_OK;
}

sg_err_t fe_reg_remove(FEReg *r, const char *id) {
  if (!r) return SG_ERR_INVAL;
  int i = idx_of(r, id);
  if (i < 0) return SG_ERR_NOTFOUND;
  for (int j = i; j < r->count - 1; j++) r->apps[j] = r->apps[j + 1];
  r->count--;
  notify(r);
  return SG_OK;
}

int           fe_reg_count(const FEReg *r)                 { return r ? r->count : 0; }
const sg_app *fe_reg_at(const FEReg *r, int i)             { return (r && i >= 0 && i < r->count) ? &r->apps[i] : 0; }
const sg_app *fe_reg_find(const FEReg *r, const char *id)  { int i = r ? idx_of(r, id) : -1; return i >= 0 ? &r->apps[i] : 0; }
void          fe_reg_set_change_cb(FEReg *r, void (*cb)(void *), void *ctx) { r->cb = cb; r->cb_ctx = ctx; }
int           fe_reg_handles_click(const sg_app *a) { return a && (a->flags & SG_APP_HANDLES_CLICK) ? 1 : 0; }
