#ifndef ARC_ENGINE_ASSET_H
#define ARC_ENGINE_ASSET_H

#include "../../external/sokol/c/sokol_gfx.h"

// Asset search configuration. Games register the directories to scan
// when resolving textures by short name (e.g. "icon" ->
// "res/images/icon.png"). The engine-side binding layer consults the
// registry when loading resources; there is no hardcoded default root —
// a game that registers zero paths will only load textures via the
// absolute path given to the binding.

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
// from on_render_ready to inject real views (game-defined fragment
// shader textures). `slot` is one of the VIEW_* macros from
// generated_shader.h. Silently ignored if slot is out of range.
void engine_register_view(int slot, sg_view view);

// Per-frame fragment-shader global (Unity's Shader.SetGlobalFloat
// equivalent). graphics.c writes this into Shader_Data.params[1] each
// time it submits a mesh — games that need world-to-UV mapping set it
// to 1.0 / (world_size_pixels) so the fragment shader can convert
// world coords to [0,1] sampling UV. Default 0.0 means the path is
// inert. Set on demand from on_frame or whenever world size changes.
void engine_set_world_inv_size(float value);

// Read side used by graphics.c. Not intended for game use.
float engine_get_world_inv_size(void);

#endif
