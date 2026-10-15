/* Host test for the launcher app registry (fe_reg_add/fe_reg_remove = sg_app_register contract + internal
 * enumeration + HANDLES_CLICK flag). -Wall -Wextra -fsanitize=address,undefined. */
#include "../fe_registry.h"
#include "../sg_front.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int started, stopped;
static sg_err_t on_start(sg_lv_obj parent, void *user) { (void)parent; (void)user; started++; return SG_OK; }
static sg_err_t on_stop(void *user) { (void)user; stopped++; return SG_OK; }

static sg_app mk(const char *id, const char *title, uint32_t flags) {
  sg_app a; memset(&a, 0, sizeof a);
  a.id = id; a.title = title; a.on_start = on_start; a.on_stop = on_stop; a.flags = flags;
  return a;
}

int main(void) {
  static FEReg reg; FEReg *r = &reg;   /* host test: caller-owned table */
  assert(fe_reg_count(r) == 0 && fe_reg_at(r, 0) == 0 && fe_reg_find(r, "x") == 0);

  sg_app music = mk("music", "音乐", SG_APP_HANDLES_CLICK);
  assert(fe_reg_add(r, &music) == SG_OK && fe_reg_count(r) == 1);
  assert(fe_reg_find(r, "music") == fe_reg_at(r, 0));
  assert(fe_reg_handles_click(fe_reg_at(r, 0)) == 1);

  sg_app nav = mk("nav", "导航", 0);
  assert(fe_reg_add(r, &nav) == SG_OK && fe_reg_count(r) == 2);
  assert(fe_reg_handles_click(fe_reg_at(r, 1)) == 0);

  /* replace by id keeps slot */
  sg_app music2 = mk("music", "音乐+", 0);
  assert(fe_reg_add(r, &music2) == SG_OK && fe_reg_count(r) == 2);
  assert(!strcmp(fe_reg_at(r, 0)->title, "音乐+") && fe_reg_handles_click(fe_reg_at(r, 0)) == 0);

  /* invalid */
  sg_app bad = mk(0, "x", 0); assert(fe_reg_add(r, &bad) == SG_ERR_INVAL);
  sg_app noop = mk("z", "z", 0); noop.on_start = 0;
  assert(fe_reg_add(r, &noop) == SG_ERR_INVAL);

  /* unregister shifts */
  assert(fe_reg_remove(r, "nav") == SG_OK && fe_reg_count(r) == 1 && fe_reg_find(r, "nav") == 0);
  assert(fe_reg_remove(r, "nav") == SG_ERR_NOTFOUND);

  /* bounded */
  char ids[FE_APP_MAX + 4][16];
  for (int n = 0; n < FE_APP_MAX + 4; n++) {
    snprintf(ids[n], sizeof ids[n], "f%d", n);
    sg_app f = mk(ids[n], "f", 0);
    sg_err_t e = fe_reg_add(r, &f);
    assert(e == SG_OK || e == SG_ERR_NOMEM);
  }
  assert(fe_reg_count(r) == FE_APP_MAX);

  printf("fe_registry: all tests passed\n");
  return 0;
}
