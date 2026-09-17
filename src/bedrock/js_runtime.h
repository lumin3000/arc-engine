#ifndef JS_RUNTIME_H
#define JS_RUNTIME_H

#include "quickjs.h"
#include <stdbool.h>

int js_runtime_init(void);

void js_runtime_shutdown(void);

// Returns true only when a complete GPU submission ran on this host turn.
bool js_runtime_render(void);

// Fail immediately if GPU/UI work is called outside the owning host thread.
void js_runtime_assert_render_thread(void);

void js_runtime_handle_external_command(const char *cmd);

bool js_runtime_is_ready(void);

extern const char *g_run_mode;

int  js_runtime_get_error_count(void);
void js_runtime_reset_error_count(void);

// Accessor for the primary JSContext. Returns NULL before js_runtime_init()
// completes.
JSContext *js_runtime_get_context(void);

// Engine-internal configuration setters (bootstrap path / main script path /
// game bindings registrar) live in js_runtime_internal.h. Games configure
// these via Engine_Config (engine_main.h).

#endif
