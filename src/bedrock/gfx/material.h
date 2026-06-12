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

// 渲染队列带注册表——渲染契约的唯一发放点（docs/plan_render_contract.md）。
// renderQueue 必须落在某个已注册带内，material_set_render_queue 对带外值 FATAL。
// 新增带在此登记：X(带名, 起, 止开区间, 用途)；区间重叠由 static_assert 拒绝。
#define RENDER_QUEUE_BANDS(X)                                                  \
  X(RQ_BACKGROUND, 1000, 1600, "全屏底层: 地形 base/水面")                     \
  X(RQ_BACKGROUND_BLEND, 1600, 2000, "底层过渡: 地形 blend 混边")              \
  X(RQ_GEOMETRY, 2000, 2450, "默认内容: section/物体")                         \
  X(RQ_ALPHA_TEST, 2450, 3000, "alpha 裁切内容")                               \
  X(RQ_TRANSPARENT, 3000, 4000, "透明/叠加罩层")                               \
  X(RQ_OVERLAY, 4000, 5000, "顶层覆盖")

enum {
#define X(name, start, end, desc) name = start, name##_END = end,
  RENDER_QUEUE_BANDS(X)
#undef X
};

bool render_queue_valid(int renderQueue);

// 带序号 (按注册表声明序, 0-based); 带外返回 -1。RQ_BAND_COUNT 为注册带总数。
enum {
#define X(name, start, end, desc) name##_BAND_IDX,
  RENDER_QUEUE_BANDS(X)
#undef X
      RQ_BAND_COUNT
};
int render_queue_band_index(int renderQueue);

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
