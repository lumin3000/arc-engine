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

#endif
