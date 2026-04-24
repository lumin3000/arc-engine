#include "material.h"
#include <math.h>
#include <string.h>

int shader_type_to_tex_index(ShaderType shader_type) {
  switch (shader_type) {
  case SHADER_TYPE_TERRAIN_FADE:
    return 254;
  case SHADER_TYPE_TERRAIN_HARD:
    return 253;
  case SHADER_TYPE_TERRAIN_FADE_ROUGH:
    return 252;
  case SHADER_TYPE_TERRAIN_WATER:
    return 251;
  case SHADER_TYPE_TERRAIN_WATER_MOVING:
    return 247;
  case SHADER_TYPE_TERRAIN_HARD_POLLUTED:
    return 250;
  case SHADER_TYPE_TERRAIN_FADE_POLLUTED:
    return 249;
  case SHADER_TYPE_TERRAIN_FADE_ROUGH_POLLUTED:
    return 248;
  case SHADER_TYPE_TEXTURED:
    return 0;
  case SHADER_TYPE_CUTOUT:
    return 2;
  case SHADER_TYPE_TRANSPARENT:
    return 3;
  case SHADER_TYPE_META_OVERLAY:
    return 4;
  case SHADER_TYPE_EDGE_DETECT:
    return 5;
  case SHADER_TYPE_NONE:
  default:
    return 255;
  }
}

void material_init(Material *mat) {
  if (!mat)
    return;
  memset(mat, 0, sizeof(Material));

  mat->color[0] = 1.0f;
  mat->color[1] = 1.0f;
  mat->color[2] = 1.0f;
  mat->color[3] = 1.0f;

  mat->texture.id = SG_INVALID_ID;
  mat->texture_view.id = SG_INVALID_ID;
  mat->shader.id = SG_INVALID_ID;
  mat->pipeline.id = SG_INVALID_ID;
  mat->shader_type = SHADER_TYPE_NONE;
  mat->blend_mode = BLEND_MODE_ALPHA;
  mat->renderQueue = RQ_GEOMETRY;
  mat->enabled = true;
  mat->param1 = 0.0f;

  mat->color_two[0] = 1.0f;
  mat->color_two[1] = 1.0f;
  mat->color_two[2] = 1.0f;
  mat->color_two[3] = 1.0f;
  mat->mask_texture.id = SG_INVALID_ID;
  memset(mat->params, 0, sizeof(mat->params));
}

void material_set_shader_type(Material *mat, ShaderType type) {
  if (!mat)
    return;
  mat->shader_type = type;
}

int material_get_tex_index(const Material *mat) {
  if (!mat)
    return 255;
  return shader_type_to_tex_index(mat->shader_type);
}

void material_set_shader(Material *mat, sg_shader shader) {
  if (!mat)
    return;
  mat->shader = shader;
  material_update_pipeline(mat);
}

void material_set_blend_mode(Material *mat, BlendMode mode) {
  if (!mat)
    return;
  mat->blend_mode = mode;
  material_update_pipeline(mat);
}

void material_update_pipeline(Material *mat) {
  if (!mat)
    return;
  if (mat->shader.id == SG_INVALID_ID)
    return;

  mat->pipeline = pipeline_get(mat->shader, mat->blend_mode);
}

void material_set_texture(Material *mat, sg_image texture) {
  if (!mat)
    return;

  if (mat->texture_view.id != SG_INVALID_ID) {
    sg_destroy_view(mat->texture_view);
  }

  mat->texture = texture;

  if (texture.id != SG_INVALID_ID) {
    mat->texture_view = sg_make_view(&(sg_view_desc){
        .texture.image = texture,
    });
  } else {
    mat->texture_view.id = SG_INVALID_ID;
  }
}

void material_set_color(Material *mat, float r, float g, float b, float a) {
  if (!mat)
    return;
  mat->color[0] = r;
  mat->color[1] = g;
  mat->color[2] = b;
  mat->color[3] = a;
}

void material_set_color_v4(Material *mat, const Vec4 color) {
  if (!mat)
    return;
  memcpy(mat->color, color, sizeof(Vec4));
}

void material_set_render_queue(Material *mat, int renderQueue) {
  if (!mat)
    return;
  mat->renderQueue = renderQueue;
}

void material_set_param(Material *mat, float val) {
  if (!mat)
    return;
  mat->param1 = val;
}

bool material_is_valid(const Material *mat) { return mat && mat->enabled; }

bool material_equals(const Material *a, const Material *b) {
  if (!a || !b)
    return false;
  if (a == b)
    return true;

  bool pip_match = (a->pipeline.id == b->pipeline.id);
  bool tex_match = (a->texture.id == b->texture.id);
  bool col_match = (memcmp(&a->color, &b->color, sizeof(Vec4)) == 0);
  bool par_match = (fabsf(a->param1 - b->param1) < 0.001f);
  bool shader_type_match = (a->shader_type == b->shader_type);

  bool col2_match = (memcmp(&a->color_two, &b->color_two, sizeof(Vec4)) == 0);
  bool mask_match = (a->mask_texture.id == b->mask_texture.id);
  bool params_match = (memcmp(a->params, b->params, sizeof(a->params)) == 0);

  return pip_match && tex_match && col_match && par_match &&
         shader_type_match && col2_match && mask_match && params_match;
}
