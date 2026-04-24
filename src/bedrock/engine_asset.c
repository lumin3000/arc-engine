
#include "engine_asset.h"
#include "gfx/render.h"
#include "../log.h"
#include <stdlib.h>
#include <string.h>

static const char **g_paths = NULL;
static int g_count = 0;
static int g_capacity = 0;

static int has_path(const char *p) {
  for (int i = 0; i < g_count; i++) {
    if (g_paths[i] && strcmp(g_paths[i], p) == 0) return 1;
  }
  return 0;
}

void engine_register_texture_search_paths(const char *const *paths, int count) {
  if (count <= 0 || !paths) return;

  int needed = g_count + count;
  if (needed > g_capacity) {
    int new_cap = g_capacity ? g_capacity : 4;
    while (new_cap < needed) new_cap *= 2;
    const char **reallocd =
        (const char **)realloc(g_paths, (size_t)new_cap * sizeof(const char *));
    if (!reallocd) {
      LOG_ERROR("[engine_asset] realloc failed (count=%d)\n", needed);
      exit(1);
    }
    g_paths = reallocd;
    g_capacity = new_cap;
  }

  for (int i = 0; i < count; i++) {
    if (!paths[i] || has_path(paths[i])) continue;
    g_paths[g_count++] = paths[i];
  }
}

int engine_texture_search_path_count(void) { return g_count; }

const char *engine_texture_search_path(int i) {
  if (i < 0 || i >= g_count) return NULL;
  return g_paths[i];
}

void engine_register_view(int slot, sg_view view) {
  int max_views = (int)(sizeof(render_state.bind.views) /
                        sizeof(render_state.bind.views[0]));
  if (slot < 0 || slot >= max_views) {
    LOG_ERROR("[engine_register_view] slot %d out of range [0, %d)\n",
              slot, max_views);
    return;
  }
  render_state.bind.views[slot] = view;
}
