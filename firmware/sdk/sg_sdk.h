/*
 * sg_sdk.h - Spare Glass firmware extension SDK (public contract)
 *
 * Every declaration here is Spare's own design. It is the surface through
 * which glasses-side apps (launcher, music, navigation, assistant) reach the
 * Spare io runtime. The runtime that implements it is not public.
 *
 * Function DECLARATIONS only. ABI version below must bump on any breaking
 * change.
 *
 * SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 */
#ifndef SG_SDK_H
#define SG_SDK_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- ABI version --------------------------------------------------------- */
#define SG_SDK_ABI_MAJOR 0
#define SG_SDK_ABI_MINOR 1
#define SG_SDK_ABI_VERSION ((SG_SDK_ABI_MAJOR << 16) | SG_SDK_ABI_MINOR)

/* ---- Basic types --------------------------------------------------------- */
typedef int32_t sg_err_t;          /* 0 == SG_OK, negative == error */
#define SG_OK            (0)
#define SG_ERR_INVAL     (-1)
#define SG_ERR_NOMEM     (-2)
#define SG_ERR_NOTFOUND  (-3)
#define SG_ERR_AGAIN     (-4)
#define SG_ERR_IO        (-5)
#define SG_ERR_UNSUP     (-6)

typedef uint8_t  sg_bool;          /* 0 / 1 */
typedef uint32_t sg_handle;        /* opaque registration handle, 0 == invalid */

/* ---- Logging ------------------------------------------------------------- */
typedef enum {
    SG_LOG_ERR = 0,
    SG_LOG_WARN = 1,
    SG_LOG_INFO = 2,
    SG_LOG_DBG  = 3
} sg_log_level;

void sg_log(sg_log_level level, const char *tag, const char *fmt, ...);
#define SG_LOGE(tag, ...) sg_log(SG_LOG_ERR,  tag, __VA_ARGS__)
#define SG_LOGW(tag, ...) sg_log(SG_LOG_WARN, tag, __VA_ARGS__)
#define SG_LOGI(tag, ...) sg_log(SG_LOG_INFO, tag, __VA_ARGS__)
#define SG_LOGD(tag, ...) sg_log(SG_LOG_DBG,  tag, __VA_ARGS__)

/* ---- Memory -------------------------------------------------------------- */
/* Backed by the firmware allocator (resolved vendor malloc/free). All module
 * .bss/heap flows through here; the builder reserves no static RAM of its own
 * beyond each module's fixed .bss. */
void *sg_malloc(size_t n);
void *sg_calloc(size_t count, size_t size);
void *sg_realloc(void *p, size_t n);
void  sg_free(void *p);

/* ---- Symbol resolution (runtime fallback) -------------------------------- */
/* Normal references are bound at build time. This is for optional late binding
 * / plugin discovery. Returns NULL if unknown. */
void *sg_sym(const char *name);

/* ---- File receive (phone -> glasses) ------------------------------------- */
/* Registered by filename. Called once a complete file has arrived from the
 * phone. `data` is owned by the SDK and valid only for the duration of the
 * call; copy what you need. Return SG_OK for local handler success; the
 * vendor transport ACK precedes this callback. Send business ACKs separately. */
typedef sg_err_t (*sg_file_cb)(const char *name, const uint8_t *data,
                               size_t len, void *user);
sg_handle sg_file_register(const char *name, sg_file_cb cb, void *user);
sg_err_t  sg_file_unregister(sg_handle h);

/* ---- Send back to phone -------------------------------------------------- */
/* Send a named command plus an opaque binary payload to the paired phone. */
sg_err_t sg_send(const char *cmd, const uint8_t *data, size_t len);

/* ---- Input: keys --------------------------------------------------------- */
/* Logical keys. Physical mapping is resolved from the vendor input layer. */
typedef enum {
    SG_KEY_UNKNOWN = 0,
    SG_KEY_POWER,
    SG_KEY_BACK,
    SG_KEY_CONFIRM,
    SG_KEY_VOL_UP,
    SG_KEY_VOL_DOWN,
    SG_KEY_CROWN         /* crown press */
} sg_key;

typedef enum {
    SG_KEY_DOWN = 0,
    SG_KEY_UP   = 1,
    SG_KEY_LONG = 2,
    SG_KEY_REPEAT = 3,
    SG_KEY_DOUBLE = 4,   /* multi-click gesture end (no UP follows) */
    SG_KEY_TRIPLE = 5
} sg_key_action;
/* Crown gestures as delivered:
 *   click:        DOWN, UP          (UP when the vendor resolves the click)
 *   long press:   DOWN, LONG, [REPEAT...], UP
 *   double/triple DOWN, DOUBLE | TRIPLE
 * Callbacks run on the UI thread; hook/unhook must be called there too. */

/* Global key interception. Return non-zero to CONSUME the event (vendor
 * handler is not called); return 0 to pass through. */
typedef int (*sg_key_cb)(sg_key key, sg_key_action action, void *user);
sg_handle sg_key_hook(sg_key_cb cb, void *user);
sg_err_t  sg_key_unhook(sg_handle h);

/* ---- Input: encoder (crown rotation) ------------------------------------- */
/* delta: signed detents, +clockwise. */
typedef void (*sg_encoder_cb)(int32_t delta, void *user);
sg_handle sg_encoder_subscribe(sg_encoder_cb cb, void *user);
sg_err_t  sg_encoder_unsubscribe(sg_handle h);

/* ---- Input: fold / hinge (hall sensor) ----------------------------------- */
/* folded == 1 when the frame is folded closed. Callback fires on change. */
typedef void (*sg_fold_cb)(sg_bool folded, void *user);
sg_handle sg_fold_subscribe(sg_fold_cb cb, void *user);
sg_err_t  sg_fold_unsubscribe(sg_handle h);

/* ---- Sensors: IMU + magnetometer (synchronous samples) ------------------- */
/* Raw vendor units: timestamp microseconds (monotonic),
 * accel / gyro / mag as reported by the sensor hub, axes in the device frame.
 * Units and axis signs are NOT normalised here; consumers auto-calibrate. */
typedef struct sg_imu_sample {
    uint64_t t_us;
    uint32_t seq;                   /* vendor word at +8, unused/opaque */
    float ax, ay, az, gx, gy, gz, mx, my, mz;
} sg_imu_sample;
/* Runs on the vendor sensor thread: copy and return, never block. After
 * sg_imu_unsubscribe returns, one in-flight call may still be completing, so
 * `user` must outlive the subscription by that much (module-lifetime data). */
typedef void (*sg_imu_cb)(const sg_imu_sample *s, void *user);
/* Non-blocking, any thread. The first subscriber starts reporting on a
 * low-priority worker (vendor start may block for seconds); the last
 * unsubscribe stops it and restores the previous vendor handler owner. */
sg_handle sg_imu_subscribe(sg_imu_cb cb, void *user);
sg_err_t  sg_imu_unsubscribe(sg_handle h);

/* ---- UI (LVGL) ----------------------------------------------------------- */
/* Opaque LVGL object pointer; we do not re-expose the vendor LVGL ABI here.
 * Modules that need lvgl.h include the vendor-matched copy and cast. */
typedef void *sg_lv_obj;

sg_lv_obj sg_ui_root(void);        /* active screen / root container */
sg_lv_obj sg_ui_top_layer(void);   /* top/overlay layer */

/* An LVGL font (const lv_font_t*) with CJK glyph coverage at the given pixel
 * size. Returns NULL (caller should fall back to LV_FONT_DEFAULT) if the
 * requested size is unavailable. Nearest available size may be returned. */
const void *sg_ui_font_cjk(int px);

/* Run `fn` on the UI (LVGL) thread. Safe to call from any context. */
typedef void (*sg_ui_task_cb)(void *user);
sg_err_t  sg_ui_async(sg_ui_task_cb fn, void *user);
/* One-shot/periodic timer on the UI thread. period_ms, repeat flag. */
sg_handle sg_ui_timer(uint32_t period_ms, sg_bool repeat,
                      sg_ui_task_cb fn, void *user);
sg_err_t  sg_ui_timer_cancel(sg_handle h);

/* ---- System -------------------------------------------------------------- */
sg_err_t sg_screen_set(sg_bool on);              /* display on/off */
/* Ref-counted "keep screen on" token. on=1 acquires a hold, on=0 releases one;
 * the display stays on while any hold is active. Balanced calls required. */
sg_err_t sg_screen_keep_on(sg_bool on);
sg_err_t sg_brightness_set(uint8_t level_0_100);
int32_t  sg_brightness_get(void);                /* <0 on error */
sg_err_t sg_volume_set(uint8_t level_0_100);
int32_t  sg_volume_get(void);                    /* <0 on error */

/* Preferences: small key/value store persisted by the vendor settings layer. */
sg_err_t sg_pref_get(const char *key, void *buf, size_t buf_len, size_t *out_len);
sg_err_t sg_pref_set(const char *key, const void *buf, size_t len);

/* Wall-clock time, milliseconds since Unix epoch (0 if unset). */
uint64_t sg_time_ms(void);
/* Monotonic uptime milliseconds. */
uint64_t sg_uptime_ms(void);

/* ---- Application registration -------------------------------------------- */
/* The frontend module provides sg_app_register(); other app modules call it
 * at init to add themselves to the launcher. The frontend owns the concrete
 * implementation; this is the shared contract. */
typedef struct sg_app {
    const char *id;                 /* stable unique id, e.g. "nav" */
    const char *title;              /* launcher display name */
    const void *icon;               /* optional, module-defined (e.g. lv img) */
    /* Lifecycle. `parent` is the LVGL container to build into. */
    sg_err_t (*on_start)(sg_lv_obj parent, void *user);
    sg_err_t (*on_stop)(void *user);
    void      *user;
    uint32_t   flags;               /* reserved, pass 0 */
} sg_app;

sg_err_t sg_app_register(const sg_app *app);
sg_err_t sg_app_unregister(const char *id);
sg_err_t sg_app_launch(const char *id);          /* programmatic launch */
/* Leave the named app's page and return to the launcher/time page (STOP).
 * Pass the app's own id; its on_stop() is invoked. */
sg_err_t sg_app_exit(const char *id);

/* ---- Module entry point -------------------------------------------------- */
/* Every module exports exactly one of these, named sg_module_main.
 * Called once, on the UI thread, during our init shim. ABI version lets the
 * loader reject modules built against an incompatible header. */
typedef struct sg_module_info {
    uint32_t    abi_version;        /* set to SG_SDK_ABI_VERSION */
    const char *name;
} sg_module_info;

sg_err_t sg_module_main(const sg_module_info *info);

#ifdef __cplusplus
} /* extern "C" */
#endif
#endif /* SG_SDK_H */
