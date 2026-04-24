#ifndef BEDROCK_GFX_GENERIC_SHADER_H
#define BEDROCK_GFX_GENERIC_SHADER_H

#include "../../../external/sokol/c/sokol_gfx.h"
#include <stdbool.h>

sg_shader generic_shader_create(const char* vs_src, const char* fs_src);

void generic_shader_destroy(sg_shader shd);

#endif
