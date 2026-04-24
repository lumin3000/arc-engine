
#ifndef UNIFIED_MESH_H
#define UNIFIED_MESH_H

#include "quickjs.h"
#include <stdbool.h>

int js_init_unified_mesh_module(JSContext *ctx);

int unified_mesh_alloc_slot(void);

void unified_mesh_free_slot(int slot_id);

bool unified_mesh_begin_collect(int slot_id);

bool unified_mesh_append_vertices(int slot_id,
                                   const float *pos, int vert_count,
                                   const float *uv,
                                   const float *uv_rect,
                                   const unsigned char *color,
                                   const unsigned int *indices, int index_count,
                                   const unsigned char *sway);

bool unified_mesh_end_collect(int slot_id, int atlas_texture_id, int shader_type);

void unified_mesh_draw(int slot_id);

int unified_mesh_get_atlas_texture_id(int index);

const float* unified_mesh_get_collect_uv_rect(int slot_id, int *out_vert_count);

void unified_mesh_dump_atlas(void);

#endif
