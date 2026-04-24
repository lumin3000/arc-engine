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

// Engine-internal configuration setters (bootstrap path / main script path /
// game bindings registrar) live in js_runtime_internal.h. Games configure
// these via Engine_Config (engine_main.h).

#endif
