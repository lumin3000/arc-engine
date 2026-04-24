#ifndef ARC_ENGINE_TYPES_H
#define ARC_ENGINE_TYPES_H

#include <stdbool.h>
#include <stdint.h>

typedef float Vec2[2];
typedef float Vec3[3];
typedef float Vec4[4];
typedef float Matrix4[16];
typedef int Vec2i[2];

typedef uint8_t Quad_Flags;
#define QUAD_FLAG_NONE 0
#define QUAD_FLAG_BACKGROUND_PIXELS (1 << 0)
#define QUAD_FLAG_FLAG2 (1 << 1)
#define QUAD_FLAG_FLAG3 (1 << 2)

typedef enum {
  ZLayer_nil = 0,
  ZLayer_background,
  ZLayer_shadow,
  ZLayer_playspace,
  ZLayer_vfx,
  ZLayer_ui,
  ZLayer_tooltip,
  ZLayer_pause_menu,
  ZLayer_top,
  ZLAYER_COUNT
} ZLayer;

typedef enum {
  Pivot_bottom_left = 0,
  Pivot_bottom_center,
  Pivot_bottom_right,
  Pivot_center_left,
  Pivot_center_center,
  Pivot_center_right,
  Pivot_top_left,
  Pivot_top_center,
  Pivot_top_right,
} Pivot;

typedef enum {
  Direction_north = 0,
  Direction_east,
  Direction_south,
  Direction_west,
} Direction;

// Sprite identifier. The engine treats it as an opaque int.
// Index 0 is reserved for "nil" (used as sentinel for "no sprite").
// Indices 1..N are assigned by the game via engine_register_sprites().
typedef int Sprite_Name;
#define Sprite_Name_nil 0

typedef struct {
  int frame_count;
  Vec2 offset;
  Pivot pivot;
} Sprite_Data;

typedef struct {
  const char *name;    // must be stable pointer (string literal or long-lived)
  int frame_count;     // >= 1
  Vec2 offset;
  Pivot pivot;
} Engine_Sprite_Desc;

typedef struct {
  Matrix4 ndc_to_world_xform;
  Vec4 bg_repeat_tex0_atlas_uv;
  Vec4 batch_props;
  Vec4 col_override;
  Vec4 col_override_2;
  Vec4 params;
} Shader_Data;

// Sprite registry. The game calls engine_register_sprites() once at
// startup. The engine allocates internal storage and serves lookups via
// get_sprite_offset() / get_frame_count() / sprite_name_to_string().
//
// Sprite index 0 is always "nil" and is registered implicitly by the
// engine; game-provided descs start at index 1.
void engine_register_sprites(const Engine_Sprite_Desc *descs, int count);
int  engine_sprite_count(void);  // returns total including nil
Sprite_Name engine_get_sprite_index_by_name(const char *name);  // -1 = not found

// Shared access for internal engine code (render.c etc.)
extern Sprite_Data *sprite_data;
#define SPRITE_NAME_COUNT engine_sprite_count()

void get_sprite_offset(Sprite_Name sprite, Vec2 out_offset, Pivot *out_pivot);
int  get_frame_count(Sprite_Name sprite);
const char *sprite_name_to_string(Sprite_Name sprite);

#define VIEW_tex0 0
#define VIEW_font_tex 1
#define VIEW_flow_map 2
#define VIEW_ripple_tex 3
#define VIEW_noise_tex 4

typedef struct sg_shader_desc sg_shader_desc;
typedef enum sg_backend sg_backend;

#define COLOR_WHITE ((Vec4){1.0f, 1.0f, 1.0f, 1.0f})
#define MATRIX4_IDENTITY                                                       \
  ((Matrix4){1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1})

#endif
