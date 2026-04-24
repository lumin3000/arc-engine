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

    // Game callbacks ------------------------------------------------------
    // All optional. Called by the engine at the right points in the frame
    // lifecycle. The game uses these to register its own sprites, JS
    // bindings, and per-frame logic.

    // Invoked after engine subsystems are initialized but before
    // js_runtime_init(). Game should call engine_register_sprites() here.
    void (*on_init)(void);

    // Invoked once the JS context exists but before JS user code loads.
    // Game registers its own JS bindings here (e.g. blood_bindings).
    void (*on_register_js_bindings)(void *js_ctx);  // JSContext* (avoid QJS include here)

    // Invoked every frame before the engine renders. Game-side update.
    void (*on_frame)(float delta_t);

    // Invoked on shutdown, before sg_shutdown().
    void (*on_cleanup)(void);
} Engine_Config;

// Runs the engine. Never returns (calls exit() internally).
int engine_run(const Engine_Config *cfg, int argc, char **argv);

// Accessors for engine state (used by engine_bindings.c's coord/game APIs).
// Not intended as game-facing API.
extern int window_w;
extern int window_h;

#endif
