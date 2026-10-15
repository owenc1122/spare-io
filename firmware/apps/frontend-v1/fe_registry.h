#ifndef FE_REGISTRY_H
#define FE_REGISTRY_H
/* fe_registry — the Spare launcher's app table. Implements the shared SDK
 * contract sg_app_register / sg_app_unregister (firmware/sdk/sg_sdk.h; the
 * runtime forwards those to its table) and adds internal enumeration. Pure, bounded, no heap, no
 * vendor dependency; host-testable. Clean-room, Spare original.
 *
 * (sg_app_launch is implemented by the runtime, fe_runtime.c, since launching
 * needs the display port.) */
#include "../../sdk/sg_sdk.h"

#define FE_APP_MAX 24

/* The table is caller-owned (the runtime embeds one in its heap context; the
 * module image has no writable statics). Zero-initialised == empty. */
typedef struct FEReg {
  sg_app apps[FE_APP_MAX];
  int    count;
  void (*cb)(void *);
  void  *cb_ctx;
} FEReg;

/* sg_app_register / sg_app_unregister semantics on an explicit table. */
sg_err_t fe_reg_add(FEReg *r, const sg_app *app);
sg_err_t fe_reg_remove(FEReg *r, const char *id);

/* Enumeration in registration order. */
int           fe_reg_count(const FEReg *r);
const sg_app *fe_reg_at(const FEReg *r, int index);
const sg_app *fe_reg_find(const FEReg *r, const char *id);

/* The runtime subscribes to be told the grid must rebuild after a late
 * register/unregister. At most one subscriber. */
void fe_reg_set_change_cb(FEReg *r, void (*cb)(void *), void *ctx);

/* Does this app handle its own clicks (SG_APP_HANDLES_CLICK)? */
int fe_reg_handles_click(const sg_app *a);

#endif
