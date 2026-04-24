#ifndef ARC_ENGINE_ASSET_H
#define ARC_ENGINE_ASSET_H

#include "../../external/sokol/c/sokol_gfx.h"

// Asset search configuration. Games register the directories to scan
// when resolving textures by short name (e.g. "player" ->
// "res/images/player.png"). The engine-side binding layer consults the
// registry when loading resources; there is no hardcoded default root —
// a game that registers zero paths will only load textures via the
// absolute path given to the binding.
//
// See docs/s16_review.md §B1.

// Register texture search paths. Call order defines resolution order
// (first wins). Passing an already-registered path is a no-op. The
// engine does not copy the strings; callers must provide stable
// pointers (string literals or process-lifetime storage).
void engine_register_texture_search_paths(const char *const *paths, int count);

// Returns the number of registered paths (0 before any registration).
int engine_texture_search_path_count(void);

// Returns the i-th registered path. Result is a stable pointer owned
// by the caller that registered it. NULL if i is out of range.
const char *engine_texture_search_path(int i);

// Override a render_state.bind.views[slot] entry. The engine seeds all
// VIEW_* slots with 1x1 dummies during render_init(); games call this
// from on_render_ready to inject real views (e.g. terrain flow map,
// water ripple/noise textures). `slot` is one of the VIEW_* macros
// from generated_shader.h. Silently ignored if slot is out of range.
void engine_register_view(int slot, sg_view view);

#endif
