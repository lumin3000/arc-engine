
#ifndef BALD_GFX_MESH_H
#define BALD_GFX_MESH_H

#include "../../../external/sokol/c/sokol_gfx.h"
#include "../../types.h"
#include <stdbool.h>

struct Material;

typedef struct {

  float *vertices;
  float *colors;
  float *uvs;
  int *triangles;

  int vert_count;
  int tri_count;
  int vert_capacity;
  int tri_capacity;

  void *gpu_vertices;
  int gpu_vert_capacity;

  sg_buffer vbuf;
  sg_buffer ibuf;
  bool gpu_valid;
  bool dirty;
} Mesh;

void mesh_init(Mesh *mesh, int initial_vert_capacity, int initial_tri_capacity);

void mesh_free(Mesh *mesh);

void mesh_clear(Mesh *mesh);

void mesh_set_vertices(Mesh *mesh, const float *verts, int count);

void mesh_set_colors(Mesh *mesh, const float *colors, int count);

void mesh_set_uvs(Mesh *mesh, const float *uvs, int count);

void mesh_set_triangles(Mesh *mesh, const int *tris, int count);

int mesh_add_vertex(Mesh *mesh, float x, float y, float z, float r, float g,
                    float b, float a, float u, float v);

bool mesh_add_triangle(Mesh *mesh, int i0, int i1, int i2);

void mesh_register_main_thread(void);

void mesh_upload_to_gpu_with_material(Mesh *mesh, const struct Material *mat);

void mesh_upload_to_gpu(Mesh *mesh);

void mesh_mark_dirty(Mesh *mesh);

bool mesh_is_empty(const Mesh *mesh);

#endif
