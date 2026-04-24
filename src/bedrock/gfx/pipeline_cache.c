#include "pipeline_cache.h"
#include "../../log.h"
#include "../../types.h"
#include "generated_shader.h"
#include "render.h"
#include <stddef.h>
#include <string.h>

#define MAX_PIPELINES 256

typedef struct {
  sg_shader shader;
  BlendMode blend;
  sg_pipeline pip;
  bool employed;
} PipelineEntry;

static struct {
  PipelineEntry entries[MAX_PIPELINES];
  int count;
  sg_pipeline_desc base_desc;
  bool initialized;
} g_cache;

void pipeline_cache_init(void) {
  if (g_cache.initialized)
    return;

  memset(&g_cache, 0, sizeof(g_cache));

  sg_pipeline_desc desc = {0};

  desc.layout.buffers[0].stride = sizeof(Vertex);
  desc.layout.attrs[ATTR_quad_position].format = SG_VERTEXFORMAT_FLOAT3;
  desc.layout.attrs[ATTR_quad_position].offset = offsetof(Vertex, pos);
  desc.layout.attrs[ATTR_quad_color0].format = SG_VERTEXFORMAT_FLOAT4;
  desc.layout.attrs[ATTR_quad_color0].offset = offsetof(Vertex, col);
  desc.layout.attrs[ATTR_quad_uv0].format = SG_VERTEXFORMAT_FLOAT2;
  desc.layout.attrs[ATTR_quad_uv0].offset = offsetof(Vertex, uv);
  desc.layout.attrs[ATTR_quad_local_uv0].format = SG_VERTEXFORMAT_FLOAT2;
  desc.layout.attrs[ATTR_quad_local_uv0].offset = offsetof(Vertex, uv);
  desc.layout.attrs[ATTR_quad_size0].format = SG_VERTEXFORMAT_FLOAT2;
  desc.layout.attrs[ATTR_quad_size0].offset = offsetof(Vertex, uv);
  desc.layout.attrs[ATTR_quad_extras0].format = SG_VERTEXFORMAT_UBYTE4N;
  desc.layout.attrs[ATTR_quad_extras0].offset = offsetof(Vertex, _pad0);
  desc.layout.attrs[ATTR_quad_uv_rect0].format = SG_VERTEXFORMAT_FLOAT4;
  desc.layout.attrs[ATTR_quad_uv_rect0].offset = offsetof(Vertex, uv_rect);

  desc.index_type = SG_INDEXTYPE_UINT32;
  desc.cull_mode = SG_CULLMODE_NONE;
  desc.depth.write_enabled = false;

  desc.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
  desc.depth.write_enabled = true;

  g_cache.base_desc = desc;
  g_cache.initialized = true;
}

static void apply_blend_mode(sg_pipeline_desc *desc, BlendMode blend) {
  desc->colors[0].blend.enabled = true;

  switch (blend) {
  case BLEND_MODE_OPAQUE:
    desc->colors[0].blend.enabled = false;

    break;
  case BLEND_MODE_ALPHA:
    desc->colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
    desc->colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    desc->colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_ONE;
    desc->colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;

    desc->depth.write_enabled = false;
    break;
  case BLEND_MODE_ADDITIVE:
    desc->colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
    desc->colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE;
    desc->colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_ZERO;
    desc->colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ONE;

    desc->depth.write_enabled = false;
    break;
  case BLEND_MODE_PREMULTIPLIED:
    desc->colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_ONE;
    desc->colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    desc->colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_ONE;
    desc->colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    break;
  case BLEND_MODE_NO_DEPTH_TEST:

    desc->colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
    desc->colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    desc->colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_ONE;
    desc->colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    desc->depth.compare = SG_COMPAREFUNC_ALWAYS;
    desc->depth.write_enabled = false;
    break;
  }
}

sg_pipeline pipeline_get(sg_shader shader, BlendMode blend) {
  if (!g_cache.initialized)
    pipeline_cache_init();

  for (int i = 0; i < g_cache.count; i++) {
    if (g_cache.entries[i].shader.id == shader.id &&
        g_cache.entries[i].blend == blend) {
      return g_cache.entries[i].pip;
    }
  }

  if (g_cache.count >= MAX_PIPELINES) {
    LOG_ERROR("Pipeline cache full");
    return (sg_pipeline){0};
  }

  sg_pipeline_desc desc = g_cache.base_desc;
  desc.shader = shader;
  apply_blend_mode(&desc, blend);

  sg_pipeline pip = sg_make_pipeline(&desc);

  PipelineEntry *entry = &g_cache.entries[g_cache.count++];
  entry->shader = shader;
  entry->blend = blend;
  entry->pip = pip;
  entry->employed = true;

  return pip;
}
