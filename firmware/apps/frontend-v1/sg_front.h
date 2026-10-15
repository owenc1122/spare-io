#ifndef SG_FRONT_H
#define SG_FRONT_H
/*
 * sg_front.h — public surface the Spare front-end module offers to OTHER app
 * modules (music, assistant, nav, ...). Clean-room, Spare original.
 *
 * App registration is the shared SDK contract in firmware/sdk/sg_sdk.h
 * (sg_app / sg_app_register / sg_app_unregister / sg_app_launch); the front-end
 * owns that implementation. This header adds the front-end-specific extras:
 * the per-app behaviour flag, the in-app launcher services, and input
 * forwarding for self-handling apps.
 *
 * Uniform controls (owner): crown rotate = move focus, single click = confirm,
 * long press = AI assistant. No double-click anywhere.
 */
#include "../../sdk/sg_sdk.h"

/* Flag bit for sg_app.flags. When set, the app receives single clicks and crown
 * rotation itself and the global in-app 3-button card is NOT shown — the app
 * draws its own 返回/控制中心/退出 controls and calls the services below.
 * (The music app uses this; value matches music_port.h = bit 0.) */
#define SG_APP_HANDLES_CLICK (1u << 0)

/* Open the control center over the current app. Its 返回 item calls `return_cb`
 * (the app re-shows its page) instead of returning to the time page. Returns
 * SG_OK if it opened, else an error. No-op unless an app is foreground. */
sg_err_t sg_frontend_open_control_center(void (*return_cb)(void *user), void *user);

/* Quit the foreground app and return to the time page. */
sg_err_t sg_frontend_exit_app(void);

/* A SG_APP_HANDLES_CLICK app registers its input entry points here in on_start
 * (and passes NULLs, or lets on_stop clear them, when leaving). While it is the
 * foreground app the front-end forwards crown rotation (signed detents) and
 * single clicks to these, with `user` passed back (apps keep their state in a
 * heap context: no writable statics); long press still goes to the AI
 * assistant. Apps that prefer to self-hook (sg_key_hook/sg_encoder_subscribe)
 * may ignore this. */
void sg_front_set_app_input(void (*on_rotate)(int32_t detents, void *user),
                            void (*on_click)(void *user), void *user);

#endif
