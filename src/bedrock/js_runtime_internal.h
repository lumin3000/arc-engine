#ifndef JS_RUNTIME_INTERNAL_H
#define JS_RUNTIME_INTERNAL_H

// Engine-internal JS runtime configuration. Only engine_main.c is allowed
// to include this header; games configure these paths and callbacks via
// Engine_Config (engine_main.h) and engine_main.c forwards the values
// here. Exposing these in js_runtime.h led to two parallel APIs doing the
// same job (see docs/s16_review.md §A1).

typedef void (*JS_Runtime_Game_Bindings_Fn)(void *js_ctx);

void js_runtime_set_bootstrap_path(const char *path);
void js_runtime_set_main_script_path(const char *path);
void js_runtime_set_game_bindings_registrar(JS_Runtime_Game_Bindings_Fn fn);

#endif
