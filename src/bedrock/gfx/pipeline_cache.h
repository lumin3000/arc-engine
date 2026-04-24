#ifndef BEDROCK_GFX_PIPELINE_CACHE_H
#define BEDROCK_GFX_PIPELINE_CACHE_H

#include "../../../external/sokol/c/sokol_gfx.h"

typedef enum {
  BLEND_MODE_OPAQUE = 0,
  BLEND_MODE_ALPHA,
  BLEND_MODE_ADDITIVE,
  BLEND_MODE_PREMULTIPLIED,
  BLEND_MODE_NO_DEPTH_TEST
} BlendMode;

void pipeline_cache_init(void);

sg_pipeline pipeline_get(sg_shader shader, BlendMode blend);

#endif
