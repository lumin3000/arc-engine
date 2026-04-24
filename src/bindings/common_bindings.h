#ifndef ARC_COMMON_BINDINGS_H
#define ARC_COMMON_BINDINGS_H

// Convenience registrars that wire the engine's built-in JS bindings
// into a given JSContext. bedrock/ never #includes any bindings/ header;
// instead the consumer passes these function pointers via Engine_Config
// so the engine-boundary rule in arc-engine/CLAUDE.md §"引擎边界规则"
// holds.
//
// IMPORTANT CTX SEMANTICS: jtask weak-links js_init_message_module and
// invokes it in EACH worker context during service creation. The
// registrar installed into arc_engine_state()->js_main_bindings_registrar
// is therefore called once per worker ctx (plus once for the bootstrap
// ctx via js_runtime_init). It must be safe to call on any ctx — all
// listed init functions just install globalThis.<module>.

#include "quickjs.h"

// Register the full set of engine bindings. Called for each jtask worker
// context as well as the bootstrap context. Matches the prior inline
// sequence at the tail of js_init_message_module.
void arc_register_main_js_bindings(JSContext *ctx);

// Register the subset of bindings that render_service's private context
// additionally needs (currently just re-applies the same set since all
// render-side JS is bundled alongside game-side JS). Kept as a separate
// hook for the future case where the two bundles diverge.
void arc_register_render_js_bindings(JSContext *ctx);

#endif
