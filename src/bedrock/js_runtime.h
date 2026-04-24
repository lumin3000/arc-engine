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

#endif
