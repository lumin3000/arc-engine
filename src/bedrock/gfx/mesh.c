
#include "mesh.h"
#include "material.h"
#include "render.h"
#if defined(_WIN32)
#include <windows.h>
#else
#include <pthread.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
static DWORD s_main_thread_id = 0;
#else
static pthread_t s_main_thread_id = 0;
#endif

void mesh_register_main_thread(void) {
#if defined(_WIN32)
  s_main_thread_id = GetCurrentThreadId();
#else
  s_main_thread_id = pthread_self();
#endif
}

#define MESH_DEFAULT_VERT_CAP 256
#define MESH_DEFAULT_TRI_CAP 512
#define MESH_MAX_VERTS 65536
#define MESH_MAX_TRIS (65536 * 3)

void mesh_init(Mesh *mesh, int initial_vert_capacity,
               int initial_tri_capacity) {
  if (!mesh)
    return;

  memset(mesh, 0, sizeof(Mesh));

  int vert_cap =
      initial_vert_capacity > 0 ? initial_vert_capacity : MESH_DEFAULT_VERT_CAP;
  int tri_cap =
      initial_tri_capacity > 0 ? initial_tri_capacity : MESH_DEFAULT_TRI_CAP;

  mesh->vertices = (float *)malloc(vert_cap * 3 * sizeof(float));
  mesh->colors = (float *)malloc(vert_cap * 4 * sizeof(float));
  mesh->uvs = (float *)malloc(vert_cap * 2 * sizeof(float));
  mesh->triangles = (int *)malloc(tri_cap * sizeof(int));

  mesh->vert_count = 0;
  mesh->tri_count = 0;
  mesh->vert_capacity = vert_cap;
  mesh->tri_capacity = tri_cap;

  mesh->gpu_vertices = NULL;
  mesh->gpu_vert_capacity = 0;

  mesh->vbuf.id = SG_INVALID_ID;
  mesh->ibuf.id = SG_INVALID_ID;
  mesh->gpu_valid = false;
  mesh->dirty = true;
}

void mesh_clear(Mesh *mesh) {
  if (mesh) {
    mesh->vert_count = 0;
    mesh->tri_count = 0;
    mesh->dirty = true;
  }
}

void mesh_free(Mesh *mesh) {
  if (!mesh)
    return;
  if (mesh->vertices)
    free(mesh->vertices);
  if (mesh->colors)
    free(mesh->colors);
  if (mesh->uvs)
    free(mesh->uvs);
  if (mesh->triangles)
    free(mesh->triangles);
  if (mesh->gpu_vertices)
    free(mesh->gpu_vertices);
  if (mesh->vbuf.id != SG_INVALID_ID)
    sg_destroy_buffer(mesh->vbuf);
  if (mesh->ibuf.id != SG_INVALID_ID)
    sg_destroy_buffer(mesh->ibuf);
  memset(mesh, 0, sizeof(Mesh));
}

static bool mesh_ensure_vert_capacity(Mesh *mesh, int needed) {
  if (mesh->vert_count + needed <= mesh->vert_capacity)
    return true;

  int new_cap =
      mesh->vert_capacity > 0 ? mesh->vert_capacity : MESH_DEFAULT_VERT_CAP;
  while (new_cap < mesh->vert_count + needed)
    new_cap *= 2;
  if (new_cap > MESH_MAX_VERTS)
    return false;

  float *new_verts =
      (float *)realloc(mesh->vertices, new_cap * 3 * sizeof(float));
  float *new_cols = (float *)realloc(mesh->colors, new_cap * 4 * sizeof(float));
  float *new_uvs = (float *)realloc(mesh->uvs, new_cap * 2 * sizeof(float));

  if (!new_verts || !new_cols || !new_uvs) {
    return false;
  }

  mesh->vertices = new_verts;
  mesh->colors = new_cols;
  mesh->uvs = new_uvs;
  mesh->vert_capacity = new_cap;
  return true;
}

static bool mesh_ensure_tri_capacity(Mesh *mesh, int needed) {
  if (mesh->tri_count + needed <= mesh->tri_capacity)
    return true;

  int new_cap =
      mesh->tri_capacity > 0 ? mesh->tri_capacity : MESH_DEFAULT_TRI_CAP;
  while (new_cap < mesh->tri_count + needed)
    new_cap *= 2;
  if (new_cap > MESH_MAX_TRIS)
    return false;

  int *new_tris = (int *)realloc(mesh->triangles, new_cap * sizeof(int));
  if (!new_tris)
    return false;

  mesh->triangles = new_tris;
  mesh->tri_capacity = new_cap;
  return true;
}

void mesh_set_vertices(Mesh *mesh, const float *verts, int count) {
  if (!mesh || !verts)
    return;

  if (count > mesh->vert_capacity) {
    if (!mesh_ensure_vert_capacity(mesh, count - mesh->vert_count))
      return;
  }

  memcpy(mesh->vertices, verts, count * 3 * sizeof(float));
  mesh->vert_count = count;
  mesh->dirty = true;
}

void mesh_set_colors(Mesh *mesh, const float *colors, int count) {
  if (!mesh || !colors)
    return;
  if (count > mesh->vert_capacity) {
    if (!mesh_ensure_vert_capacity(mesh, count - mesh->vert_count))
      return;
  }
  memcpy(mesh->colors, colors, count * 4 * sizeof(float));
  mesh->dirty = true;
}

void mesh_set_uvs(Mesh *mesh, const float *uvs, int count) {
  if (!mesh || !uvs)
    return;
  if (count > mesh->vert_capacity) {
    if (!mesh_ensure_vert_capacity(mesh, count - mesh->vert_count))
      return;
  }
  memcpy(mesh->uvs, uvs, count * 2 * sizeof(float));
  mesh->dirty = true;
}

void mesh_set_triangles(Mesh *mesh, const int *tris, int count) {
  if (!mesh || !tris)
    return;
  if (count > mesh->tri_capacity) {
    if (!mesh_ensure_tri_capacity(mesh, count - mesh->tri_count))
      return;
  }
  memcpy(mesh->triangles, tris, count * sizeof(int));
  mesh->tri_count = count;
  mesh->dirty = true;
}

int mesh_add_vertex(Mesh *mesh, float x, float y, float z, float r, float g,
                    float b, float a, float u, float v) {
  if (!mesh)
    return -1;
  if (!mesh_ensure_vert_capacity(mesh, 1))
    return -1;

  int idx = mesh->vert_count;

  mesh->vertices[idx * 3 + 0] = x;
  mesh->vertices[idx * 3 + 1] = y;
  mesh->vertices[idx * 3 + 2] = z;

  mesh->colors[idx * 4 + 0] = r;
  mesh->colors[idx * 4 + 1] = g;
  mesh->colors[idx * 4 + 2] = b;
  mesh->colors[idx * 4 + 3] = a;

  mesh->uvs[idx * 2 + 0] = u;
  mesh->uvs[idx * 2 + 1] = v;

  mesh->vert_count++;
  mesh->dirty = true;
  return idx;
}

bool mesh_add_triangle(Mesh *mesh, int i0, int i1, int i2) {
  if (!mesh)
    return false;
  if (!mesh_ensure_tri_capacity(mesh, 3))
    return false;

  mesh->triangles[mesh->tri_count++] = i0;
  mesh->triangles[mesh->tri_count++] = i1;
  mesh->triangles[mesh->tri_count++] = i2;
  mesh->dirty = true;
  return true;
}

static bool mesh_ensure_gpu_capacity(Mesh *mesh, int needed) {
  if (needed <= mesh->gpu_vert_capacity)
    return true;

  int new_cap = mesh->gpu_vert_capacity > 0 ? mesh->gpu_vert_capacity : 256;
  while (new_cap < needed)
    new_cap *= 2;

  Vertex *new_gpu =
      (Vertex *)realloc(mesh->gpu_vertices, new_cap * sizeof(Vertex));
  if (!new_gpu)
    return false;

  mesh->gpu_vertices = new_gpu;
  mesh->gpu_vert_capacity = new_cap;
  return true;
}

void mesh_upload_to_gpu_with_material(Mesh *mesh, const struct Material *mat) {
  if (!mesh || mesh->vert_count == 0)
    return;
  if (!mesh->dirty && mesh->gpu_valid)
    return;

  if (s_main_thread_id != 0 &&
#if defined(_WIN32)
      GetCurrentThreadId() != s_main_thread_id) {
#else
      !pthread_equal(pthread_self(), s_main_thread_id)) {
#endif
    static int s_warned = 0;
    if (!s_warned) {
      fprintf(stderr,
              "[FATAL] mesh_upload_to_gpu called from WRONG THREAD! "
              "sokol_gfx is NOT thread-safe. "
              "verts=%d, gpu_valid=%d\n",
              mesh->vert_count, mesh->gpu_valid);
      s_warned = 1;
    }
    return;
  }

  if (!mesh_ensure_gpu_capacity(mesh, mesh->vert_count)) {
    fprintf(stderr, "[mesh] Failed to allocate GPU vertex buffer\n");
    return;
  }

  int tex_index = mat ? material_get_tex_index(mat) : 255;

  Vertex *vdata = (Vertex *)mesh->gpu_vertices;
  memset(vdata, 0, mesh->vert_count * sizeof(Vertex));

  for (int i = 0; i < mesh->vert_count; i++) {

    vdata[i].pos[0] = mesh->vertices[i * 3 + 0];
    vdata[i].pos[1] = mesh->vertices[i * 3 + 1];
    vdata[i].pos[2] = mesh->vertices[i * 3 + 2];

    vdata[i].col[0] = mesh->colors[i * 4 + 0];
    vdata[i].col[1] = mesh->colors[i * 4 + 1];
    vdata[i].col[2] = mesh->colors[i * 4 + 2];
    vdata[i].col[3] = mesh->colors[i * 4 + 3];

    vdata[i].uv[0] = mesh->uvs[i * 2 + 0];
    vdata[i].uv[1] = mesh->uvs[i * 2 + 1];

    vdata[i]._pad0 = 0;
    vdata[i].quad_flags = 0;
    vdata[i]._padding[0] = 0;
    vdata[i]._padding[1] = 0;
  }

  size_t vbuf_size = mesh->vert_count * sizeof(Vertex);

  size_t ibuf_size = mesh->tri_count * sizeof(uint32_t);
  uint32_t *idata = (uint32_t *)malloc(ibuf_size);
  if (!idata) {
    fprintf(stderr, "[mesh] Failed to allocate index buffer\n");
    return;
  }

  for (int i = 0; i < mesh->tri_count; i++) {
    idata[i] = (uint32_t)mesh->triangles[i];
  }

  if (!mesh->gpu_valid || mesh->vbuf.id == SG_INVALID_ID) {

    if (mesh->vbuf.id != SG_INVALID_ID) {
      sg_destroy_buffer(mesh->vbuf);
    }
    if (mesh->ibuf.id != SG_INVALID_ID) {
      sg_destroy_buffer(mesh->ibuf);
    }

    mesh->vbuf = sg_make_buffer(&(sg_buffer_desc){
        .usage.dynamic_update = true,
        .size = vbuf_size,
    });

    mesh->ibuf = sg_make_buffer(&(sg_buffer_desc){
        .usage.index_buffer = true,
        .usage.dynamic_update = true,
        .size = ibuf_size,
    });

    mesh->gpu_valid =
        (mesh->vbuf.id != SG_INVALID_ID && mesh->ibuf.id != SG_INVALID_ID);

    if (!mesh->gpu_valid) {
      fprintf(stderr,
              "[mesh] GPU buffer creation failed: vbuf.id=%u, ibuf.id=%u, "
              "verts=%d, tris=%d\n",
              mesh->vbuf.id, mesh->ibuf.id, mesh->vert_count, mesh->tri_count);
    }
  }

  if (mesh->gpu_valid) {
    sg_update_buffer(mesh->vbuf, &(sg_range){.ptr = vdata, .size = vbuf_size});
    sg_update_buffer(mesh->ibuf, &(sg_range){.ptr = idata, .size = ibuf_size});
  }

  free(idata);
  mesh->dirty = false;
}

void mesh_upload_to_gpu(Mesh *mesh) {
  mesh_upload_to_gpu_with_material(mesh, NULL);
}

void mesh_mark_dirty(Mesh *mesh) {
  if (mesh)
    mesh->dirty = true;
}

bool mesh_is_empty(const Mesh *mesh) {
  return !mesh || mesh->vert_count == 0 || mesh->tri_count == 0;
}
