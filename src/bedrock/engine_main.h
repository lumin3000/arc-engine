#ifndef ARC_ENGINE_MAIN_H
#define ARC_ENGINE_MAIN_H

#include "../types.h"
#include "../../external/sokol/c/sokol_app.h"

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
    const char *default_run_mode;    // default value for --mode; e.g. "gunslinger"
    float master_volume;             // 0..1; 0.25 if 0 and sound is used
    float zoom_level;                // initial zoom; 2.0 if 0
    Vec3 camera_initial_pos;
    Vec3 camera_initial_rot;

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
    // correct place to register game-specific JS bindings like
    // blood_bindings; registering later causes service workers to race
    // with the registration and see undefined globals.
    // Receives JSContext* (typed as void* to avoid QJS include here).
    void (*on_register_js_bindings)(void *js_ctx);

    // Invoked every frame before the engine renders. Game-side update.
    void (*on_frame)(float delta_t);

    // Invoked on shutdown, before sg_shutdown().
    void (*on_cleanup)(void);
} Engine_Config;

// Runs the engine. Never returns (calls exit() internally).
int engine_run(const Engine_Config *cfg, int argc, char **argv);

// Internal engine state (window_w / window_h / ctx / argv / g_master_volume)
// lives in engine_state.h. Not intended as game-facing API.

#endif
