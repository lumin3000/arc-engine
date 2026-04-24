#ifndef ARC_ENGINE_STATE_H
#define ARC_ENGINE_STATE_H

#include "../types.h"
#include <stdint.h>

// Engine-internal state shared between engine_main.c (owner) and the
// engine bindings layer (reader). This header replaces the prior
// mechanism where engine_bindings.c redeclared Game_State / Core_Context
// as local typedefs and pulled window_w / window_h / ctx / argv via bare
// `extern` — see docs/s16_review.md §A4, §A5.

typedef struct {
    uint64_t ticks;
    double   game_time_elapsed;
    Vec3     cam_pos;
    Vec3     cam_rot;
    Vec3     cam_vel;
    float    zoom_level;
    float    desired_zoom_level;
} Game_State;

typedef struct {
    Game_State *gs;
    float       delta_t;
} Core_Context;

// Forward declaration to keep this header free of engine_main.h / QJS
// includes. engine_main.c defines the layout.
struct Engine_Config;
typedef void (*JS_Runtime_Game_Bindings_Fn)(void *js_ctx);

// Arc_Engine_State collects the scattered engine-lifetime globals into a
// single singleton so their lifecycle and ownership is no longer spread
// across file-scope statics in engine_main.c / js_runtime.c. See
// docs/s16_review.md §A2, §A3.
typedef struct {
    // Saved process-level argv (for app_restart etc.)
    char **saved_argv;
    int    saved_argc;

    // Last-frame timestamp used by engine_on_frame to compute delta_t.
    double last_frame_time;

    // JS runtime configuration overrides. Owned strings — freed when
    // replaced by js_runtime_set_* functions. engine_main.c forwards
    // Engine_Config fields into these slots before js_runtime_init().
    char *js_bootstrap_path;        // NULL => default
    char *js_main_script_path;      // NULL => default
    JS_Runtime_Game_Bindings_Fn js_game_bindings_fn;
} Arc_Engine_State;

// Returns the singleton. Never NULL. Storage is in engine_main.c.
Arc_Engine_State *arc_engine_state(void);

// Owned by engine_main.c. Do not mutate from bindings except through the
// established setter paths (camera_set_position etc.).
extern Core_Context ctx;
extern int   window_w;
extern int   window_h;
extern float g_master_volume;

char **app_get_argv(void);
int    app_get_argc(void);

#endif
