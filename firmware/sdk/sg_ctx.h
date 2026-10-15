/* sg_ctx.h - module context slots (SDK extension).
 *
 * Modules keep ALL mutable state in a heap context created by their init and
 * pass it through every callback's `user` argument (the module image has no
 * writable .data/.bss). The few SDK-contract entry points that carry no user
 * pointer (sg_app_register, sg_app_launch, sg_frontend_*, ...) reach their
 * owner's context through one of these slots, which live in the boot-owned
 * SDK runtime context.
 *
 * sg_ctx_get: any thread (NULL before boot / unset). sg_ctx_set: UI thread.
 * SPDX-License-Identifier: Proprietary  (Spare original work)
 */
#ifndef SG_CTX_H
#define SG_CTX_H
#include "sg_sdk.h"

typedef enum {
    SG_CTX_MAIN = 0,        /* firmware/main (sg_main.c)          */
    SG_CTX_FRONTEND,        /* frontend-v1 runtime (fe_runtime.c) */
    SG_CTX__COUNT
} sg_ctx_id;

void    *sg_ctx_get(sg_ctx_id id);
sg_err_t sg_ctx_set(sg_ctx_id id, void *ctx);
#endif
