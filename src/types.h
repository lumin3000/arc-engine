#ifndef BALD_USER_TYPES_H
#define BALD_USER_TYPES_H

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
  Sprite_Name_nil = 0,
  Sprite_Name_fmod_logo,
  Sprite_Name_player_still,
  Sprite_Name_shadow_medium,
  Sprite_Name_bg_repeat_tex0,
  Sprite_Name_player_death,
  Sprite_Name_player_run,
  Sprite_Name_player_idle,

  SPRITE_NAME_COUNT
} Sprite_Name;

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

typedef struct {
  int frame_count;
  Vec2 offset;
  Pivot pivot;
} Sprite_Data;

typedef struct {
  Matrix4 ndc_to_world_xform;
  Vec4 bg_repeat_tex0_atlas_uv;
  Vec4 batch_props;
  Vec4 col_override;
  Vec4 col_override_2;
  Vec4 params;
} Shader_Data;

extern Sprite_Data sprite_data[SPRITE_NAME_COUNT];

void get_sprite_offset(Sprite_Name sprite, Vec2 out_offset, Pivot *out_pivot);
int get_frame_count(Sprite_Name sprite);
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
