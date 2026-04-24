#include "generic_shader.h"
#include "../../log.h"
#include "../../types.h"

#define ATTR_quad_position 0
#define ATTR_quad_color0 1
#define ATTR_quad_uv0 2
#define ATTR_quad_local_uv0 3
#define ATTR_quad_size0 4
#define ATTR_quad_bytes0 5
#define ATTR_quad_color_override0 6
#define ATTR_quad_params0 7

sg_shader generic_shader_create(const char *vs_src, const char *fs_src) {
  sg_shader_desc desc = {0};
  desc.label = "generic-shader";

  desc.vertex_func.source = vs_src;

  desc.fragment_func.source = fs_src;

  desc.uniform_blocks[0].stage = SG_SHADERSTAGE_VERTEX;
  desc.uniform_blocks[0].size = 64;
  desc.uniform_blocks[0].glsl_uniforms[0].type = SG_UNIFORMTYPE_FLOAT4;
  desc.uniform_blocks[0].glsl_uniforms[0].array_count = 4;
  desc.uniform_blocks[0].glsl_uniforms[0].glsl_name = "mvp";

  desc.attrs[ATTR_quad_position].glsl_name = "position";
  desc.attrs[ATTR_quad_color0].glsl_name = "color0";
  desc.attrs[ATTR_quad_uv0].glsl_name = "uv0";
  desc.attrs[ATTR_quad_local_uv0].glsl_name = "local_uv0";
  desc.attrs[ATTR_quad_size0].glsl_name = "size0";
  desc.attrs[ATTR_quad_bytes0].glsl_name = "bytes0";
  desc.attrs[ATTR_quad_color_override0].glsl_name = "color_override0";
  desc.attrs[ATTR_quad_params0].glsl_name = "params0";

  desc.views[0].texture.stage = SG_SHADERSTAGE_FRAGMENT;
  desc.views[0].texture.image_type = SG_IMAGETYPE_2D;
  desc.views[0].texture.sample_type = SG_IMAGESAMPLETYPE_FLOAT;

  desc.samplers[0].stage = SG_SHADERSTAGE_FRAGMENT;
  desc.samplers[0].sampler_type = SG_SAMPLERTYPE_FILTERING;

  desc.texture_sampler_pairs[0].stage = SG_SHADERSTAGE_FRAGMENT;
  desc.texture_sampler_pairs[0].view_slot = 0;
  desc.texture_sampler_pairs[0].sampler_slot = 0;
  desc.texture_sampler_pairs[0].glsl_name = "tex0";

  sg_shader shd = sg_make_shader(&desc);
  if (sg_query_shader_state(shd) != SG_RESOURCESTATE_VALID) {
    LOG_ERROR("Failed to create generic shader");
  }
  return shd;
}

void generic_shader_destroy(sg_shader shd) {
  if (shd.id != SG_INVALID_ID) {
    sg_destroy_shader(shd);
  }
}
