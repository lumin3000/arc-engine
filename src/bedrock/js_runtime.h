#ifndef JS_RUNTIME_H
#define JS_RUNTIME_H

#include <stdbool.h>

int js_runtime_init(void);

void js_runtime_shutdown(void);

void js_runtime_render(void);

void js_runtime_handle_external_command(const char *cmd);

bool js_runtime_is_ready(void);

extern const char *g_run_mode;

int  js_runtime_get_error_count(void);
void js_runtime_reset_error_count(void);

// Accessor for the primary JSContext. Returns NULL before js_runtime_init()
// completes. Used by engine_run() to let the game register its own JS
// bindings after engine bindings are registered.
// Returns a JSContext* (void* to avoid QJS include in engine-facing header).
void *js_runtime_get_context(void);

// Override the bootstrap.js path (the JS file that jtask loads to
// initialize its core). Must be called before js_runtime_init(). If not
// called, the default "external/jtask/jslib/bootstrap.js" is used (which
// assumes the consumer project has jtask directly under ./external/).
//
// For arc-engine submodule consumers, set this to
// "external/arc-engine/external/jtask/jslib/bootstrap.js".
void js_runtime_set_bootstrap_path(const char *path);

// Override the scripts/main.js path loaded at the end of js_runtime_init().
// Must be called before js_runtime_init(). Default is "scripts/main.js".
void js_runtime_set_main_script_path(const char *path);

// Register a callback invoked INSIDE js_runtime_init(), after engine JS
// bindings are registered but before scripts/main.js is evaluated. The
// game uses this to register its own JS bindings so that by the time
// jtask services spawn (inside main.js -> start.js), the full set of
// bindings is already on the JSContext — otherwise game JS that runs in
// service workers can race with the registration and see undefined
// bindings.
//
// The callback receives the JSContext as void* (avoids QJS include in
// the engine-facing header).
//
// Must be called before js_runtime_init(). Only one callback is kept;
// a second call replaces the first.
typedef void (*JS_Runtime_Game_Bindings_Fn)(void *js_ctx);
void js_runtime_set_game_bindings_registrar(JS_Runtime_Game_Bindings_Fn fn);

#endif
