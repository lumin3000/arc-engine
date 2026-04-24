#ifndef ARC_ENGINE_MAIN_H
#define ARC_ENGINE_MAIN_H

#include "../types.h"
#include "../../external/sokol/c/sokol_app.h"
#include "quickjs.h"
#include <stdbool.h>

// Engine entry point. Called by the game's main(). The engine owns the
// sapp_run loop, init/frame/cleanup/event callbacks, argument parsing,
// camera controls, stdin command handling, FMOD listener updates, and
// js_runtime rendering. The game customizes behavior via Engine_Config.

typedef struct {
    // Window
    const char *window_title;        // required
    int window_w;                    // defaults to 1280 if 0
    int window_h;                    // defaults to 720  if 0

    // Startup
    const char *default_run_mode;    // default value for --mode
    float master_volume;             // 0..1; 0.25 if 0 and sound is used
    float zoom_level;                // initial zoom; 2.0 if 0
    Vec3 camera_initial_pos;
    Vec3 camera_initial_rot;

    // Base orthographic half-height at zoom_level=1.0. 0 falls back to
    // the engine default of 10.0f — matching previous hardcoded behaviour.
    float base_ortho_size;

    // If true, the engine does NOT apply the built-in WASD pan, Q/E zoom,
    // and mouse-scroll zoom each frame. Default (false) keeps legacy
    // behaviour; UI-only or JS-driven games set this to true.
    bool disable_default_camera_controls;

    // Optional: play this FMOD event at startup (e.g. "event:/ambiance").
    // NULL to skip.
    const char *startup_ambiance_event;

    // Optional: override paths for jtask bootstrap and the main JS entry.
    // Useful for consumers that bundle arc-engine as a submodule, where
    // the default "external/jtask/..." is not where jtask actually lives.
    // Typical values for a consumer using arc-engine as submodule at
    // ./external/arc-engine/ :
    //   .jtask_bootstrap_path  = "external/arc-engine/external/jtask/jslib/bootstrap.js"
    //   .js_main_script_path   = "scripts/main.js"  // or leave NULL; game keeps its own
    const char *jtask_bootstrap_path;
    const char *js_main_script_path;

    // Game callbacks ------------------------------------------------------
    // All optional. Called by the engine at the right points in the frame
    // lifecycle. The game uses these to register its own sprites, JS
    // bindings, and per-frame logic.

    // Invoked after engine subsystems are initialized but before
    // js_runtime_init(). Game should call engine_register_sprites() here.
    void (*on_init)(void);

    // Invoked INSIDE js_runtime_init() — after all engine JS bindings
    // are registered, before scripts/main.js is evaluated (and before
    // any jtask service worker sees the global scope). This is the
    // correct place to register game-specific JS bindings; registering
    // later causes service workers to race with the registration and
    // see undefined globals.
    void (*on_register_js_bindings)(JSContext *js_ctx);

    // Invoked every frame before the engine renders. Game-side update.
    void (*on_frame)(float delta_t);

    // Invoked on shutdown, before sg_shutdown().
    void (*on_cleanup)(void);
} Engine_Config;

// Object-style engine handle. The engine is a process-level singleton
// backed by sapp_run; arc_engine_create()/run() is the public entry
// pattern preferred over the older `engine_run(cfg, argc, argv)` that
// coupled configuration and execution in a single call.
//
// Typical usage:
//
//   Arc_Engine *eng = arc_engine_create(&cfg, argc, argv);
//   return arc_engine_run(eng);   // does not return
//
// arc_engine_create() only records configuration; subsystem initialization
// happens inside arc_engine_run() when sapp invokes the init callback.
// Between create and run the game may inspect the engine handle but
// should not expect the JS context to be available yet — call
// arc_engine_js_context() from within a callback that fires after
// js_runtime_init() (e.g. on_register_js_bindings).

typedef struct Arc_Engine Arc_Engine;

Arc_Engine *arc_engine_create(const Engine_Config *cfg, int argc, char **argv);
int         arc_engine_run(Arc_Engine *eng);           // does not return
JSContext  *arc_engine_js_context(Arc_Engine *eng);    // NULL before js_runtime_init

// Internal engine state (window_w / window_h / ctx / argv / g_master_volume)
// lives in engine_state.h. Not intended as game-facing API.

#endif
