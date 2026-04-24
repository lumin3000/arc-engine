
#include "types.h"
#include "log.h"
#include <stdlib.h>
#include <string.h>

// Dynamic sprite registry. Game calls engine_register_sprites() once at
// startup; engine owns the storage for the lifetime of the process.
//
// Layout:
//   sprite_data[0]            — nil (implicit, inserted by engine)
//   sprite_data[1..count-1]   — game-provided
//
// sprite_names[i] points to the string given by the game (must be stable
// memory — typically a string literal).

Sprite_Data *sprite_data = NULL;
static const char **sprite_names = NULL;
static int g_sprite_count = 0;

static void ensure_nil_registered(void) {
  if (g_sprite_count != 0) return;
  sprite_data = (Sprite_Data *)calloc(1, sizeof(Sprite_Data));
  sprite_names = (const char **)calloc(1, sizeof(const char *));
  sprite_data[0].frame_count = 1;
  sprite_data[0].offset[0] = 0.0f;
  sprite_data[0].offset[1] = 0.0f;
  sprite_data[0].pivot = Pivot_bottom_left;
  sprite_names[0] = "nil";
  g_sprite_count = 1;
}

void engine_register_sprites(const Engine_Sprite_Desc *descs, int count) {
  ensure_nil_registered();

  if (count <= 0 || !descs) {
    return;
  }

  int new_count = g_sprite_count + count;
  Sprite_Data *new_data =
      (Sprite_Data *)realloc(sprite_data, new_count * sizeof(Sprite_Data));
  const char **new_names =
      (const char **)realloc(sprite_names, new_count * sizeof(const char *));
  if (!new_data || !new_names) {
    LOG_ERROR("[engine_register_sprites] realloc failed (count=%d)\n", count);
    exit(1);
  }
  sprite_data = new_data;
  sprite_names = new_names;

  for (int i = 0; i < count; i++) {
    int idx = g_sprite_count + i;
    sprite_data[idx].frame_count =
        descs[i].frame_count > 0 ? descs[i].frame_count : 1;
    sprite_data[idx].offset[0] = descs[i].offset[0];
    sprite_data[idx].offset[1] = descs[i].offset[1];
    sprite_data[idx].pivot = descs[i].pivot;
    sprite_names[idx] = descs[i].name;
  }
  g_sprite_count = new_count;
}

int engine_sprite_count(void) {
  ensure_nil_registered();
  return g_sprite_count;
}

Sprite_Name engine_get_sprite_index_by_name(const char *name) {
  if (!name) return -1;
  for (int i = 0; i < g_sprite_count; i++) {
    if (sprite_names[i] && strcmp(sprite_names[i], name) == 0) {
      return (Sprite_Name)i;
    }
  }
  return -1;
}

void get_sprite_offset(Sprite_Name sprite, Vec2 out_offset, Pivot *out_pivot) {
  if (sprite < 0 || sprite >= g_sprite_count) {
    out_offset[0] = 0.0f;
    out_offset[1] = 0.0f;
    if (out_pivot) *out_pivot = Pivot_center_center;
    return;
  }
  Sprite_Data *data = &sprite_data[sprite];
  out_offset[0] = data->offset[0];
  out_offset[1] = data->offset[1];
  if (out_pivot) *out_pivot = data->pivot;
}

int get_frame_count(Sprite_Name sprite) {
  if (sprite < 0 || sprite >= g_sprite_count) {
    return 1;
  }
  int fc = sprite_data[sprite].frame_count;
  return fc > 0 ? fc : 1;
}

const char *sprite_name_to_string(Sprite_Name sprite) {
  if (sprite < 0 || sprite >= g_sprite_count) {
    return "unknown";
  }
  return sprite_names[sprite] ? sprite_names[sprite] : "unknown";
}
