#ifndef ARC_COMMON_BINDINGS_H
#define ARC_COMMON_BINDINGS_H

// Convenience registrars that wire the engine's built-in JS bindings
// (draw/graphics/input/loader/unified_mesh/font/diag/imgui/batch/engine)
// into a given JSContext. Consumers plug these into Engine_Config's
// binding registrar fields; bedrock/ therefore never #includes any
// bindings/ header directly, preserving the engine-boundary rule in
// arc-engine/CLAUDE.md §"引擎边界规则".

#include "quickjs.h"

// Register the full set of engine bindings for the bootstrap context.
// Matches the prior inline sequence in js_runtime.c's message-module
// initialization.
void arc_register_main_js_bindings(JSContext *ctx);

// Register the subset of bindings that render_service's private context
// needs (draw/graphics/engine/input).
void arc_register_render_js_bindings(JSContext *ctx);

#endif
