#ifndef BEDROCK_GFX_MATERIAL_H
#define BEDROCK_GFX_MATERIAL_H

#include "../../../external/sokol/c/sokol_gfx.h"
#include "../../types.h"
#include "pipeline_cache.h"
#include <stdbool.h>

typedef enum {
  SHADER_TYPE_NONE = 0,

  SHADER_TYPE_USER_1,
  SHADER_TYPE_USER_2,
  SHADER_TYPE_USER_3,
  SHADER_TYPE_USER_4,
  SHADER_TYPE_USER_5,

  SHADER_TYPE_USER_6,
  SHADER_TYPE_USER_7,
  SHADER_TYPE_USER_8,

  SHADER_TYPE_TEXTURED,
  SHADER_TYPE_CUTOUT,
  SHADER_TYPE_TRANSPARENT,
  SHADER_TYPE_META_OVERLAY,
  SHADER_TYPE_EDGE_DETECT,

  SHADER_TYPE_COUNT
} ShaderType;

int shader_type_to_tex_index(ShaderType shader_type);

#define RQ_BACKGROUND 1000
#define RQ_GEOMETRY 2000
#define RQ_ALPHA_TEST 2450
#define RQ_TRANSPARENT 3000
#define RQ_OVERLAY 4000

typedef struct Material {

  sg_image texture;
  sg_view texture_view;
  Vec4 color;
  ShaderType shader_type;
  int renderQueue;

  BlendMode blend_mode;
  float param1;

  Vec4 color_two;
  sg_image mask_texture;
  float params[4];

  sg_shader shader;
  sg_pipeline pipeline;

  bool enabled;
} Material;

void material_init(Material *mat);

void material_set_shader_type(Material *mat, ShaderType type);

int material_get_tex_index(const Material *mat);

void material_set_shader(Material *mat, sg_shader shader);

void material_set_blend_mode(Material *mat, BlendMode mode);

void material_update_pipeline(Material *mat);

void material_set_texture(Material *mat, sg_image texture);

void material_set_color(Material *mat, float r, float g, float b, float a);
void material_set_color_v4(Material *mat, const Vec4 color);

void material_set_render_queue(Material *mat, int renderQueue);

void material_set_param(Material *mat, float val);

bool material_is_valid(const Material *mat);

bool material_equals(const Material *a, const Material *b);

#endif
