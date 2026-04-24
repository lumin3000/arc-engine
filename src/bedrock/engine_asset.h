#ifndef ARC_ENGINE_ASSET_H
#define ARC_ENGINE_ASSET_H

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

#endif
