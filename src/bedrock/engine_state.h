#ifndef ARC_ENGINE_STATE_H
#define ARC_ENGINE_STATE_H

#include "../types.h"
#include "quickjs.h"
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

typedef void (*JS_Runtime_Game_Bindings_Fn)(JSContext *js_ctx);

// Registrar that installs engine-provided JS bindings into a JSContext.
// See src/bindings/common_bindings.h for ready-made implementations.
typedef void (*Engine_JS_Bindings_Registrar)(JSContext *js_ctx);

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

    // Engine-binding registrar invoked inside js_init_message_module
    // (which jtask weak-links and calls for every worker ctx + the
    // bootstrap ctx). Consumers supply arc_register_main_js_bindings
    // from src/bindings/common_bindings.h via Engine_Config.
    Engine_JS_Bindings_Registrar js_main_bindings_registrar;

    // Engine-binding registrar for the render_service worker ctx,
    // invoked from do_inject_render_modules. Currently identical to the
    // main registrar (shared bundle); separate slot for future
    // divergence.
    Engine_JS_Bindings_Registrar js_render_bindings_registrar;

    // Per-frame hook invoked inside the render pass, after mesh queue
    // 0..4000 has been submitted, before the main quad draw loop. Games
    // with custom render paths (skinned animation etc.) install a hook
    // here via Engine_Config.on_post_mesh_render. NULL = no-op.
    void (*post_mesh_render_hook)(void);
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
