
#include "../bedrock/platform.h"
#include "../../external/sokol/c/sokol_gfx.h"
#include "../log.h"
#include "../types.h"
#include "bedrock/engine_asset.h"
#include "bedrock/gfx/generated_shader.h"
#include "bedrock/gfx/graphics.h"
#include "bedrock/gfx/material.h"
#include "bedrock/gfx/mesh.h"
#include "bedrock/gfx/render.h"
#include "quickjs.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

extern void stbi_set_flip_vertically_on_load(int flag);
extern unsigned char *stbi_load_from_memory(const unsigned char *buffer,
                                            int len, int *x, int *y, int *comp,
                                            int req_comp);
extern unsigned char *stbi_load(const char *filename, int *x, int *y, int *comp,
                                int req_comp);
extern void stbi_image_free(void *ptr);

#include "../../external/stb/stb_rect_pack.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../../external/stb/stb_image_write.h"

#define MAX_UNIFIED_MESHES 1024

#define DEFAULT_MAX_VERTICES 524288
#define DEFAULT_MAX_INDICES 1048576

#define MAX_UNIFIED_VERTICES DEFAULT_MAX_VERTICES
#define MAX_UNIFIED_INDICES DEFAULT_MAX_INDICES

#define MAX_ATLAS_TEXTURES 16

#define ATLAS_TEX_ID_OFFSET 10000

typedef struct {
  sg_image image;
  sg_view view;
  int width;
  int height;
  bool valid;
  uint8_t *pixels;
} AtlasTexture;

static AtlasTexture g_atlas_textures[MAX_ATLAS_TEXTURES];
static int g_atlas_texture_count = 0;

typedef struct {

  sg_buffer vbuf;
  sg_buffer ibuf;

  int vertex_count;
  int index_count;

  int max_vertices;
  int max_indices;

  int atlas_texture_id;

  Material material;

  Mesh persistent_mesh;

  bool valid;
  bool uploaded;
} UnifiedMesh;

static UnifiedMesh g_unified_meshes[MAX_UNIFIED_MESHES];
static bool g_initialized = false;
static sg_shader g_shader = {0};

static void ensure_shader(void) {
  if (g_shader.id != 0)
    return;
  g_shader = sg_make_shader(quad_shader_desc(sg_query_backend()));
}

static void ensure_initialized(void) {
  if (g_initialized)
    return;

  memset(g_unified_meshes, 0, sizeof(g_unified_meshes));
  memset(g_atlas_textures, 0, sizeof(g_atlas_textures));

  g_initialized = true;
  ensure_shader();

  LOG_INFO("[unified_mesh] Initialized (%d slots, %dK max verts)\n",
           MAX_UNIFIED_MESHES, MAX_UNIFIED_VERTICES / 1024);
}

typedef struct {
  float *pos;
  float *uv;
  float *uv_rect;
  uint8_t *color;
  uint8_t *sway;
  uint32_t *indices;
  int vert_count;
  int index_count;
  bool active;
} CollectBuffer;

static CollectBuffer g_collect_buffers[MAX_UNIFIED_MESHES];
static bool g_collect_buffers_initialized = false;

static void ensure_collect_buffers(void) {
  if (g_collect_buffers_initialized)
    return;
  memset(g_collect_buffers, 0, sizeof(g_collect_buffers));
  g_collect_buffers_initialized = true;
}

int unified_mesh_alloc_slot(void) {
  ensure_initialized();
  ensure_shader();
  ensure_collect_buffers();

  for (int i = 0; i < MAX_UNIFIED_MESHES; i++) {
    if (!g_unified_meshes[i].valid) {
      UnifiedMesh *um = &g_unified_meshes[i];

      um->max_vertices = DEFAULT_MAX_VERTICES;
      um->max_indices = DEFAULT_MAX_INDICES;

      um->vbuf = sg_make_buffer(
          &(sg_buffer_desc){.size = DEFAULT_MAX_VERTICES * sizeof(Vertex),
                            .usage.dynamic_update = true,
                            .label = "unified_mesh_vbuf"});

      um->ibuf = sg_make_buffer(
          &(sg_buffer_desc){.size = DEFAULT_MAX_INDICES * sizeof(uint32_t),
                            .usage.index_buffer = true,
                            .usage.dynamic_update = true,
                            .label = "unified_mesh_ibuf"});

      um->vertex_count = 0;
      um->index_count = 0;
      um->atlas_texture_id = -1;
      um->valid = true;
      um->uploaded = false;

      memset(&um->persistent_mesh, 0, sizeof(Mesh));

      material_init(&um->material);
      material_set_shader(&um->material, g_shader);
      material_set_shader_type(&um->material, SHADER_TYPE_TEXTURED);

      LOG_INFO("[unified_mesh] C API: Allocated slot %d\n", i);
      return i;
    }
  }
  LOG_ERROR("[unified_mesh] C API: No free slots!\n");
  return -1;
}

void unified_mesh_free_slot(int slot_id) {
  if (slot_id < 0 || slot_id >= MAX_UNIFIED_MESHES)
    return;

  UnifiedMesh *um = &g_unified_meshes[slot_id];
  if (!um->valid)
    return;

  sg_destroy_buffer(um->vbuf);
  sg_destroy_buffer(um->ibuf);
  memset(um, 0, sizeof(UnifiedMesh));

  CollectBuffer *cb = &g_collect_buffers[slot_id];
  if (cb->pos) {
    free(cb->pos);
    cb->pos = NULL;
  }
  if (cb->uv) {
    free(cb->uv);
    cb->uv = NULL;
  }
  if (cb->uv_rect) {
    free(cb->uv_rect);
    cb->uv_rect = NULL;
  }
  if (cb->color) {
    free(cb->color);
    cb->color = NULL;
  }
  if (cb->sway) {
    free(cb->sway);
    cb->sway = NULL;
  }
  if (cb->indices) {
    free(cb->indices);
    cb->indices = NULL;
  }
  cb->active = false;

  LOG_INFO("[unified_mesh] C API: Freed slot %d\n", slot_id);
}

bool unified_mesh_begin_collect(int slot_id) {
  ensure_initialized();
  ensure_collect_buffers();

  if (slot_id < 0 || slot_id >= MAX_UNIFIED_MESHES)
    return false;
  if (!g_unified_meshes[slot_id].valid)
    return false;

  CollectBuffer *cb = &g_collect_buffers[slot_id];

  if (!cb->pos) {
    cb->pos = (float *)malloc(MAX_UNIFIED_VERTICES * 3 * sizeof(float));
    cb->uv = (float *)malloc(MAX_UNIFIED_VERTICES * 2 * sizeof(float));
    cb->uv_rect = (float *)malloc(MAX_UNIFIED_VERTICES * 4 * sizeof(float));
    cb->color = (uint8_t *)malloc(MAX_UNIFIED_VERTICES * 4);
    cb->sway = (uint8_t *)malloc(MAX_UNIFIED_VERTICES);
    cb->indices = (uint32_t *)malloc(MAX_UNIFIED_INDICES * sizeof(uint32_t));

    if (!cb->pos || !cb->uv || !cb->uv_rect || !cb->color || !cb->sway || !cb->indices) {
      LOG_ERROR("[unified_mesh] C API: Failed to allocate collect buffer\n");
      return false;
    }
  }

  cb->vert_count = 0;
  cb->index_count = 0;
  cb->active = true;

  return true;
}

bool unified_mesh_append_vertices(int slot_id, const float *pos, int vert_count,
                                  const float *uv, const float *uv_rect_in,
                                  const unsigned char *color,
                                  const unsigned int *indices,
                                  int index_count,
                                  const unsigned char *sway) {
  if (slot_id < 0 || slot_id >= MAX_UNIFIED_MESHES)
    return false;

  CollectBuffer *cb = &g_collect_buffers[slot_id];
  if (!cb->active)
    return false;

  if (cb->vert_count + vert_count > MAX_UNIFIED_VERTICES) {
    LOG_ERROR("[unified_mesh] C API: Vertex overflow (%d + %d > %d)\n",
              cb->vert_count, vert_count, MAX_UNIFIED_VERTICES);
    return false;
  }
  if (cb->index_count + index_count > MAX_UNIFIED_INDICES) {
    LOG_ERROR("[unified_mesh] C API: Index overflow (%d + %d > %d)\n",
              cb->index_count, index_count, MAX_UNIFIED_INDICES);
    return false;
  }

  memcpy(cb->pos + cb->vert_count * 3, pos, vert_count * 3 * sizeof(float));

  memcpy(cb->uv + cb->vert_count * 2, uv, vert_count * 2 * sizeof(float));

  float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
  if (uv_rect_in) {
    u0 = uv_rect_in[0];
    v0 = uv_rect_in[1];
    u1 = uv_rect_in[2];
    v1 = uv_rect_in[3];
  }
  for (int i = 0; i < vert_count; i++) {
    int idx = (cb->vert_count + i) * 4;
    cb->uv_rect[idx + 0] = u0;
    cb->uv_rect[idx + 1] = v0;
    cb->uv_rect[idx + 2] = u1;
    cb->uv_rect[idx + 3] = v1;
  }

  memcpy(cb->color + cb->vert_count * 4, color, vert_count * 4);

  if (sway && cb->sway) {
    memcpy(cb->sway + cb->vert_count, sway, vert_count);
  } else if (cb->sway) {
    memset(cb->sway + cb->vert_count, 0, vert_count);
  }

  int base_vertex = cb->vert_count;
  for (int i = 0; i < index_count; i++) {
    cb->indices[cb->index_count + i] = indices[i] + base_vertex;
  }

  cb->vert_count += vert_count;
  cb->index_count += index_count;

  return true;
}

bool unified_mesh_end_collect(int slot_id, int atlas_texture_id, int shader_type) {
  if (slot_id < 0 || slot_id >= MAX_UNIFIED_MESHES)
    return false;

  CollectBuffer *cb = &g_collect_buffers[slot_id];
  if (!cb->active)
    return false;

  cb->active = false;

  UnifiedMesh *um = &g_unified_meshes[slot_id];
  if (!um->valid)
    return false;

  if (cb->vert_count == 0) {
    um->vertex_count = 0;
    um->index_count = 0;
    um->uploaded = false;
    return true;
  }

  Vertex *gpu_verts = (Vertex *)malloc(cb->vert_count * sizeof(Vertex));
  if (!gpu_verts)
    return false;

  memset(gpu_verts, 0, cb->vert_count * sizeof(Vertex));

  for (int i = 0; i < cb->vert_count; i++) {
    gpu_verts[i].pos[0] = cb->pos[i * 3 + 0];
    gpu_verts[i].pos[1] = cb->pos[i * 3 + 1];
    gpu_verts[i].pos[2] = cb->pos[i * 3 + 2];

    gpu_verts[i].uv[0] = cb->uv[i * 2 + 0];
    gpu_verts[i].uv[1] = cb->uv[i * 2 + 1];

    gpu_verts[i].uv_rect[0] = cb->uv_rect[i * 4 + 0];
    gpu_verts[i].uv_rect[1] = cb->uv_rect[i * 4 + 1];
    gpu_verts[i].uv_rect[2] = cb->uv_rect[i * 4 + 2];
    gpu_verts[i].uv_rect[3] = cb->uv_rect[i * 4 + 3];

    gpu_verts[i].col[0] = cb->color[i * 4 + 0] / 255.0f;
    gpu_verts[i].col[1] = cb->color[i * 4 + 1] / 255.0f;
    gpu_verts[i].col[2] = cb->color[i * 4 + 2] / 255.0f;
    gpu_verts[i].col[3] = cb->color[i * 4 + 3] / 255.0f;

    gpu_verts[i]._padding[0] = cb->sway ? cb->sway[i] : 0;
  }

  sg_update_buffer(um->vbuf,
                   &(sg_range){gpu_verts, cb->vert_count * sizeof(Vertex)});
  sg_update_buffer(
      um->ibuf, &(sg_range){cb->indices, cb->index_count * sizeof(uint32_t)});

  free(gpu_verts);

  um->vertex_count = cb->vert_count;
  um->index_count = cb->index_count;
  um->atlas_texture_id = atlas_texture_id;
  um->uploaded = true;

  if (atlas_texture_id >= 0 && atlas_texture_id < g_atlas_texture_count) {
    um->material.texture_view = g_atlas_textures[atlas_texture_id].view;
  }

  material_set_shader_type(&um->material, (ShaderType)shader_type);

  LOG_VERBOSE("[unified_mesh] C API: end_collect slot=%d verts=%d tris=%d shader=%d\n",
              slot_id, um->vertex_count, um->index_count / 3, shader_type);

  return true;
}

void unified_mesh_draw(int slot_id) {
  if (slot_id < 0 || slot_id >= MAX_UNIFIED_MESHES)
    return;

  UnifiedMesh *um = &g_unified_meshes[slot_id];
  if (!um->valid || !um->uploaded || um->index_count == 0)
    return;

  um->persistent_mesh.vbuf = um->vbuf;
  um->persistent_mesh.ibuf = um->ibuf;
  um->persistent_mesh.vert_count = um->vertex_count;
  um->persistent_mesh.tri_count = um->index_count;
  um->persistent_mesh.gpu_valid = true;
  um->persistent_mesh.dirty = false;

  graphics_draw_mesh_identity(&um->persistent_mesh, &um->material, 0);
}

int unified_mesh_get_atlas_texture_id(int index) {
  if (index < 0 || index >= g_atlas_texture_count)
    return -1;
  return index;
}

const float* unified_mesh_get_collect_uv_rect(int slot_id, int *out_vert_count) {
  if (slot_id < 0 || slot_id >= MAX_UNIFIED_MESHES) {
    if (out_vert_count) *out_vert_count = 0;
    return NULL;
  }
  CollectBuffer *cb = &g_collect_buffers[slot_id];
  if (out_vert_count) *out_vert_count = cb->vert_count;
  return cb->uv_rect;
}

static JSValue js_upload_atlas(JSContext *ctx, JSValueConst this_val, int argc,
                               JSValueConst *argv) {
  ensure_initialized();

  if (argc < 3) {
    return JS_ThrowTypeError(ctx, "upload_atlas requires 3 arguments");
  }

  int32_t width, height;
  if (JS_ToInt32(ctx, &width, argv[0]) < 0 ||
      JS_ToInt32(ctx, &height, argv[1]) < 0) {
    return JS_EXCEPTION;
  }

  size_t pixels_len;
  size_t offset, size;
  JSValue buf =
      JS_GetTypedArrayBuffer(ctx, argv[2], &offset, &size, &pixels_len);
  uint8_t *pixels = (uint8_t *)JS_GetArrayBuffer(ctx, &pixels_len, buf);
  JS_FreeValue(ctx, buf);

  if (!pixels) {
    return JS_ThrowTypeError(ctx, "Invalid pixel data");
  }

  size_t expected_len = (size_t)width * height * 4;
  if (size < expected_len) {
    return JS_ThrowTypeError(ctx, "Pixel data too small");
  }

  if (g_atlas_texture_count >= MAX_ATLAS_TEXTURES) {
    return JS_ThrowRangeError(ctx, "Atlas texture limit reached");
  }

  int slot_id = g_atlas_texture_count++;
  AtlasTexture *at = &g_atlas_textures[slot_id];

  if (at->valid) {
    if (at->image.id != SG_INVALID_ID) {
      sg_destroy_image(at->image);
    }
    if (at->pixels) {
      free(at->pixels);
      at->pixels = NULL;
    }
  }

  at->pixels = (uint8_t *)malloc(expected_len);
  if (!at->pixels) {
    return JS_ThrowOutOfMemory(ctx);
  }
  memcpy(at->pixels, pixels, expected_len);

  at->image = sg_make_image(&(sg_image_desc){
      .width = width,
      .height = height,
      .pixel_format = SG_PIXELFORMAT_RGBA8,
      .data.mip_levels[0] = {.ptr = pixels, .size = expected_len}});

  if (at->image.id == SG_INVALID_ID) {
    return JS_ThrowInternalError(ctx, "Failed to create atlas texture");
  }

  at->view = sg_make_view(&(sg_view_desc){
      .texture.image = at->image,
  });

  at->width = width;
  at->height = height;
  at->valid = true;

  LOG_INFO("[unified_mesh] Atlas texture %d created (%dx%d) → extId=%d\n",
           slot_id, width, height, slot_id + ATLAS_TEX_ID_OFFSET);

  return JS_NewInt32(ctx, slot_id + ATLAS_TEX_ID_OFFSET);
}

static JSValue js_load_prebaked_atlas(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv) {
  ensure_initialized();

  if (argc < 1) {
    return JS_ThrowTypeError(ctx, "load_prebaked_atlas requires 1 argument");
  }

  const char *path = JS_ToCString(ctx, argv[0]);
  if (!path) {
    return JS_EXCEPTION;
  }

  FILE *f = fopen(path, "rb");
  if (!f) {
    JS_FreeCString(ctx, path);
    return JS_ThrowInternalError(ctx, "load_prebaked_atlas: cannot open file: %s", path);
  }

  uint32_t header[2];
  if (fread(header, 4, 2, f) != 2) {
    fclose(f);
    JS_FreeCString(ctx, path);
    return JS_ThrowInternalError(ctx, "load_prebaked_atlas: invalid header");
  }
  int width = (int)header[0];
  int height = (int)header[1];
  size_t pixel_size = (size_t)width * height * 4;

  struct timespec ts0, ts1;
  clock_gettime(CLOCK_MONOTONIC, &ts0);

  uint8_t *pixels = (uint8_t *)malloc(pixel_size);
  if (!pixels) {
    fclose(f);
    JS_FreeCString(ctx, path);
    return JS_ThrowOutOfMemory(ctx);
  }
  size_t read_bytes = fread(pixels, 1, pixel_size, f);
  fclose(f);

  clock_gettime(CLOCK_MONOTONIC, &ts1);
  double fread_ms = (ts1.tv_sec - ts0.tv_sec) * 1000.0 +
                    (ts1.tv_nsec - ts0.tv_nsec) / 1e6;
  LOG_INFO("[unified_mesh] prebaked fread: %.1f ms (%zu bytes)\n",
           fread_ms, read_bytes);

  if (read_bytes != pixel_size) {
    free(pixels);
    JS_FreeCString(ctx, path);
    return JS_ThrowInternalError(ctx,
        "load_prebaked_atlas: expected %zu bytes, got %zu", pixel_size, read_bytes);
  }

  if (g_atlas_texture_count >= MAX_ATLAS_TEXTURES) {
    free(pixels);
    JS_FreeCString(ctx, path);
    return JS_ThrowRangeError(ctx, "Atlas texture limit reached");
  }

  int slot_id = g_atlas_texture_count++;
  AtlasTexture *at = &g_atlas_textures[slot_id];

  at->pixels = NULL;

  clock_gettime(CLOCK_MONOTONIC, &ts0);

  at->image = sg_make_image(&(sg_image_desc){
      .width = width,
      .height = height,
      .pixel_format = SG_PIXELFORMAT_RGBA8,
      .data.mip_levels[0] = {.ptr = pixels, .size = pixel_size}});

  clock_gettime(CLOCK_MONOTONIC, &ts1);
  double gpu_ms = (ts1.tv_sec - ts0.tv_sec) * 1000.0 +
                  (ts1.tv_nsec - ts0.tv_nsec) / 1e6;
  LOG_INFO("[unified_mesh] prebaked GPU upload: %.1f ms\n", gpu_ms);

  free(pixels);

  if (at->image.id == SG_INVALID_ID) {
    JS_FreeCString(ctx, path);
    return JS_ThrowInternalError(ctx,
                                 "load_prebaked_atlas: GPU texture creation failed");
  }

  at->view = sg_make_view(&(sg_view_desc){
      .texture.image = at->image,
  });

  at->width = width;
  at->height = height;
  at->valid = true;

  LOG_INFO("[unified_mesh] Prebaked atlas %d loaded from %s (%dx%d)\n",
           slot_id, path, width, height);

  JS_FreeCString(ctx, path);
  return JS_NewInt32(ctx, slot_id);
}

static JSValue js_read_prebaked_rgba(JSContext *ctx, JSValueConst this_val,
                                     int argc, JSValueConst *argv) {
  ensure_initialized();

  if (argc < 1) {
    return JS_ThrowTypeError(ctx, "read_prebaked_rgba requires 1 argument");
  }

  const char *path = JS_ToCString(ctx, argv[0]);
  if (!path) {
    return JS_EXCEPTION;
  }

  FILE *f = fopen(path, "rb");
  if (!f) {
    JS_FreeCString(ctx, path);
    return JS_ThrowInternalError(ctx, "read_prebaked_rgba: cannot open file: %s", path);
  }

  uint32_t header[2];
  if (fread(header, 4, 2, f) != 2) {
    fclose(f);
    JS_FreeCString(ctx, path);
    return JS_ThrowInternalError(ctx, "read_prebaked_rgba: invalid header");
  }
  int width = (int)header[0];
  int height = (int)header[1];
  size_t pixel_size = (size_t)width * height * 4;

  struct timespec ts0, ts1;
  clock_gettime(CLOCK_MONOTONIC, &ts0);

  uint8_t *pixels = (uint8_t *)malloc(pixel_size);
  if (!pixels) {
    fclose(f);
    JS_FreeCString(ctx, path);
    return JS_ThrowOutOfMemory(ctx);
  }
  size_t read_bytes = fread(pixels, 1, pixel_size, f);
  fclose(f);

  clock_gettime(CLOCK_MONOTONIC, &ts1);
  double fread_ms = (ts1.tv_sec - ts0.tv_sec) * 1000.0 +
                    (ts1.tv_nsec - ts0.tv_nsec) / 1e6;
  LOG_INFO("[unified_mesh] read_prebaked_rgba fread: %.1f ms (%zu bytes)\n",
           fread_ms, read_bytes);

  if (read_bytes != pixel_size) {
    free(pixels);
    JS_FreeCString(ctx, path);
    return JS_ThrowInternalError(ctx,
        "read_prebaked_rgba: expected %zu bytes, got %zu", pixel_size, read_bytes);
  }

  if (g_atlas_texture_count >= MAX_ATLAS_TEXTURES) {
    free(pixels);
    JS_FreeCString(ctx, path);
    return JS_ThrowRangeError(ctx, "Atlas texture limit reached");
  }

  int slot_id = g_atlas_texture_count++;
  AtlasTexture *at = &g_atlas_textures[slot_id];
  at->pixels = pixels;
  at->width = width;
  at->height = height;
  at->valid = false;
  at->image.id = SG_INVALID_ID;

  LOG_INFO("[unified_mesh] read_prebaked_rgba: slot %d ready (%dx%d) from %s\n",
           slot_id, width, height, path);

  JS_FreeCString(ctx, path);
  return JS_NewInt32(ctx, slot_id);
}

static JSValue js_upload_prebaked_rgba(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv) {
  ensure_initialized();

  if (argc < 1) {
    return JS_ThrowTypeError(ctx, "upload_prebaked_rgba requires 1 argument");
  }

  int slot_id;
  if (JS_ToInt32(ctx, &slot_id, argv[0])) {
    return JS_EXCEPTION;
  }

  if (slot_id < 0 || slot_id >= g_atlas_texture_count) {
    return JS_ThrowRangeError(ctx, "upload_prebaked_rgba: invalid slot %d", slot_id);
  }

  AtlasTexture *at = &g_atlas_textures[slot_id];
  if (!at->pixels) {
    return JS_ThrowInternalError(ctx,
        "upload_prebaked_rgba: slot %d has no pixel data", slot_id);
  }
  if (at->valid) {

    return JS_NewInt32(ctx, slot_id + ATLAS_TEX_ID_OFFSET);
  }

  size_t pixel_size = (size_t)at->width * at->height * 4;

  struct timespec ts0, ts1;
  clock_gettime(CLOCK_MONOTONIC, &ts0);

  at->image = sg_make_image(&(sg_image_desc){
      .width = at->width,
      .height = at->height,
      .pixel_format = SG_PIXELFORMAT_RGBA8,
      .data.mip_levels[0] = {.ptr = at->pixels, .size = pixel_size}});

  clock_gettime(CLOCK_MONOTONIC, &ts1);
  double gpu_ms = (ts1.tv_sec - ts0.tv_sec) * 1000.0 +
                  (ts1.tv_nsec - ts0.tv_nsec) / 1e6;
  LOG_INFO("[unified_mesh] upload_prebaked_rgba GPU upload: %.1f ms\n", gpu_ms);

  free(at->pixels);
  at->pixels = NULL;

  if (at->image.id == SG_INVALID_ID) {
    return JS_ThrowInternalError(ctx,
        "upload_prebaked_rgba: GPU texture creation failed for slot %d", slot_id);
  }

  at->view = sg_make_view(&(sg_view_desc){
      .texture.image = at->image,
  });
  at->valid = true;

  LOG_INFO("[unified_mesh] upload_prebaked_rgba: slot %d uploaded (%dx%d) → extId=%d\n",
           slot_id, at->width, at->height, slot_id + ATLAS_TEX_ID_OFFSET);

  return JS_NewInt32(ctx, slot_id + ATLAS_TEX_ID_OFFSET);
}

static JSValue js_mod_pack_apply(JSContext *ctx, JSValueConst this_val,
                                 int argc, JSValueConst *argv) {
  if (argc < 3) {
    return JS_ThrowTypeError(ctx, "mod_pack_apply requires 3 arguments");
  }

  int slot_id;
  if (JS_ToInt32(ctx, &slot_id, argv[0])) return JS_EXCEPTION;

  int core_max_y;
  if (JS_ToInt32(ctx, &core_max_y, argv[1])) return JS_EXCEPTION;

  if (slot_id < 0 || slot_id >= g_atlas_texture_count) {
    return JS_ThrowRangeError(ctx, "mod_pack_apply: invalid slot %d", slot_id);
  }

  AtlasTexture *at = &g_atlas_textures[slot_id];
  if (!at->pixels) {
    return JS_ThrowInternalError(ctx,
        "mod_pack_apply: slot %d has no pixel data (already uploaded?)",
        slot_id);
  }
  if (at->valid) {
    return JS_ThrowInternalError(ctx,
        "mod_pack_apply: slot %d already uploaded", slot_id);
  }

  int page_size = at->width;
  if (at->height != page_size) {
    return JS_ThrowInternalError(ctx,
        "mod_pack_apply: non-square page %dx%d", at->width, at->height);
  }
  if (core_max_y < 0 || core_max_y >= page_size) {
    return JS_ThrowRangeError(ctx,
        "mod_pack_apply: invalid core_max_y %d (page_size %d)",
        core_max_y, page_size);
  }

  int mod_region_height = page_size - core_max_y;

  uint32_t num_mods;
  {
    JSValue len_val = JS_GetPropertyStr(ctx, argv[2], "length");
    if (JS_ToUint32(ctx, &num_mods, len_val)) {
      JS_FreeValue(ctx, len_val);
      return JS_EXCEPTION;
    }
    JS_FreeValue(ctx, len_val);
  }

  typedef struct {
    char *mod_id;
    char *tex_key;
    char *abs_path;
    int w, h;
    uint8_t *pixels;
  } PendingTex;

  int max_tex = 0;
  for (uint32_t i = 0; i < num_mods; i++) {
    JSValue mod_obj = JS_GetPropertyUint32(ctx, argv[2], i);
    JSValue tex_arr = JS_GetPropertyStr(ctx, mod_obj, "textures");
    uint32_t n;
    JSValue len_val = JS_GetPropertyStr(ctx, tex_arr, "length");
    JS_ToUint32(ctx, &n, len_val);
    JS_FreeValue(ctx, len_val);
    max_tex += n;
    JS_FreeValue(ctx, tex_arr);
    JS_FreeValue(ctx, mod_obj);
  }

  if (max_tex == 0) {

    JSValue result = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, result, "success", JS_NewBool(ctx, true));
    JS_SetPropertyStr(ctx, result, "rects", JS_NewArray(ctx));
    return result;
  }

  PendingTex *pending = calloc(max_tex, sizeof(PendingTex));
  if (!pending) {
    return JS_ThrowOutOfMemory(ctx);
  }
  int pending_count = 0;

  int failed = 0;
  for (uint32_t i = 0; i < num_mods && !failed; i++) {
    JSValue mod_obj = JS_GetPropertyUint32(ctx, argv[2], i);

    JSValue mod_id_val = JS_GetPropertyStr(ctx, mod_obj, "modId");
    const char *mod_id_c = JS_ToCString(ctx, mod_id_val);

    JSValue tex_arr = JS_GetPropertyStr(ctx, mod_obj, "textures");
    uint32_t n;
    JSValue len_val = JS_GetPropertyStr(ctx, tex_arr, "length");
    JS_ToUint32(ctx, &n, len_val);
    JS_FreeValue(ctx, len_val);

    for (uint32_t j = 0; j < n; j++) {
      JSValue tex_obj = JS_GetPropertyUint32(ctx, tex_arr, j);
      JSValue abs_path_val = JS_GetPropertyStr(ctx, tex_obj, "absPath");
      JSValue tex_key_val = JS_GetPropertyStr(ctx, tex_obj, "texKey");
      const char *abs_path_c = JS_ToCString(ctx, abs_path_val);
      const char *tex_key_c = JS_ToCString(ctx, tex_key_val);

      int w, h, comp;
      stbi_set_flip_vertically_on_load(0);
      uint8_t *px = stbi_load(abs_path_c, &w, &h, &comp, 4);
      if (!px) {
        LOG_ERROR("[mod_pack] failed to decode %s\n", abs_path_c);
        failed = 1;
      } else {
        pending[pending_count].mod_id = strdup(mod_id_c ? mod_id_c : "");
        pending[pending_count].tex_key = strdup(tex_key_c ? tex_key_c : "");
        pending[pending_count].abs_path = strdup(abs_path_c ? abs_path_c : "");
        pending[pending_count].w = w;
        pending[pending_count].h = h;
        pending[pending_count].pixels = px;
        pending_count++;
      }

      if (abs_path_c) JS_FreeCString(ctx, abs_path_c);
      if (tex_key_c) JS_FreeCString(ctx, tex_key_c);
      JS_FreeValue(ctx, abs_path_val);
      JS_FreeValue(ctx, tex_key_val);
      JS_FreeValue(ctx, tex_obj);
    }

    if (mod_id_c) JS_FreeCString(ctx, mod_id_c);
    JS_FreeValue(ctx, tex_arr);
    JS_FreeValue(ctx, mod_id_val);
    JS_FreeValue(ctx, mod_obj);
  }

  if (failed) {
    for (int i = 0; i < pending_count; i++) {
      free(pending[i].mod_id);
      free(pending[i].tex_key);
      free(pending[i].abs_path);
      if (pending[i].pixels) stbi_image_free(pending[i].pixels);
    }
    free(pending);
    return JS_ThrowInternalError(ctx, "mod_pack_apply: PNG decode failed");
  }

  stbrp_rect *rects = calloc(pending_count, sizeof(stbrp_rect));
  stbrp_node *nodes = calloc(page_size, sizeof(stbrp_node));
  stbrp_context rp_ctx;

  if (!rects || !nodes) {
    free(rects);
    free(nodes);
    for (int i = 0; i < pending_count; i++) {
      free(pending[i].mod_id);
      free(pending[i].tex_key);
      free(pending[i].abs_path);
      stbi_image_free(pending[i].pixels);
    }
    free(pending);
    return JS_ThrowOutOfMemory(ctx);
  }

  stbrp_init_target(&rp_ctx, page_size, mod_region_height, nodes, page_size);

  for (int i = 0; i < pending_count; i++) {
    rects[i].id = i;
    rects[i].w = pending[i].w;
    rects[i].h = pending[i].h;
  }

  int pack_result = stbrp_pack_rects(&rp_ctx, rects, pending_count);

  if (!pack_result) {

    int unpacked = 0;
    for (int i = 0; i < pending_count; i++) {
      if (!rects[i].was_packed) unpacked++;
    }
    LOG_ERROR("[mod_pack] %d/%d rects failed to pack (mod region full)\n",
              unpacked, pending_count);

    for (int i = 0; i < pending_count; i++) {
      free(pending[i].mod_id);
      free(pending[i].tex_key);
      free(pending[i].abs_path);
      stbi_image_free(pending[i].pixels);
    }
    free(pending);
    free(rects);
    free(nodes);
    return JS_ThrowInternalError(ctx,
        "mod_pack_apply: %d rects cannot fit in mod region", unpacked);
  }

  for (int i = 0; i < pending_count; i++) {
    int dst_x = rects[i].x;
    int dst_y = rects[i].y + core_max_y;
    int w = pending[i].w;
    int h = pending[i].h;
    uint8_t *src = pending[i].pixels;

    for (int row = 0; row < h; row++) {
      uint8_t *dst_row = at->pixels + ((dst_y + row) * page_size + dst_x) * 4;
      uint8_t *src_row = src + row * w * 4;
      memcpy(dst_row, src_row, (size_t)w * 4);
    }
  }

  JSValue result = JS_NewObject(ctx);
  JS_SetPropertyStr(ctx, result, "success", JS_NewBool(ctx, true));
  JSValue result_rects = JS_NewArray(ctx);

  for (int i = 0; i < pending_count; i++) {
    int x = rects[i].x;
    int y = rects[i].y + core_max_y;
    int w = pending[i].w;
    int h = pending[i].h;

    JSValue r = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, r, "modId", JS_NewString(ctx, pending[i].mod_id));
    JS_SetPropertyStr(ctx, r, "texKey", JS_NewString(ctx, pending[i].tex_key));
    JS_SetPropertyStr(ctx, r, "x", JS_NewInt32(ctx, x));
    JS_SetPropertyStr(ctx, r, "y", JS_NewInt32(ctx, y));
    JS_SetPropertyStr(ctx, r, "w", JS_NewInt32(ctx, w));
    JS_SetPropertyStr(ctx, r, "h", JS_NewInt32(ctx, h));

    double shrink = (w > 2 && h > 2) ? 0.5 : 0.0;
    JS_SetPropertyStr(ctx, r, "u0",
        JS_NewFloat64(ctx, (x + shrink) / (double)page_size));
    JS_SetPropertyStr(ctx, r, "v0",
        JS_NewFloat64(ctx, (y + shrink) / (double)page_size));
    JS_SetPropertyStr(ctx, r, "u1",
        JS_NewFloat64(ctx, (x + w - shrink) / (double)page_size));
    JS_SetPropertyStr(ctx, r, "v1",
        JS_NewFloat64(ctx, (y + h - shrink) / (double)page_size));

    JS_SetPropertyUint32(ctx, result_rects, i, r);
  }

  JS_SetPropertyStr(ctx, result, "rects", result_rects);

  LOG_INFO("[mod_pack] applied %d textures to slot %d (mod region y=[%d, %d))\n",
           pending_count, slot_id, core_max_y, page_size);

  for (int i = 0; i < pending_count; i++) {
    free(pending[i].mod_id);
    free(pending[i].tex_key);
    free(pending[i].abs_path);
    stbi_image_free(pending[i].pixels);
  }
  free(pending);
  free(rects);
  free(nodes);

  return result;
}

static JSValue js_load_texture_pixels(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv) {
  if (argc < 1) {
    return JS_ThrowTypeError(ctx, "load_texture_pixels requires 1 argument");
  }

  const char *tex_path = JS_ToCString(ctx, argv[0]);
  if (!tex_path) {
    return JS_EXCEPTION;
  }

  char path[512];
  FILE *f = NULL;

  if (tex_path[0] == '/') {

    strncpy(path, tex_path, sizeof(path) - 1);
    path[sizeof(path) - 1] = '\0';
    f = fopen(path, "rb");

    if (!f && strstr(tex_path, ".png") == NULL) {
      snprintf(path, sizeof(path), "%s.png", tex_path);
      f = fopen(path, "rb");
    }
  }

  if (!f) {
    int path_count = engine_texture_search_path_count();
    for (int i = 0; i < path_count && !f; i++) {
      const char *root = engine_texture_search_path(i);
      if (!root) continue;
      snprintf(path, sizeof(path), "%s/%s.png", root, tex_path);
      f = fopen(path, "rb");
      if (!f) {
        const char *last_slash = strrchr(tex_path, '/');
        const char *filename = last_slash ? last_slash + 1 : tex_path;
        snprintf(path, sizeof(path), "%s/%s.png", root, filename);
        f = fopen(path, "rb");
      }
    }
  }

  if (!f) {
    LOG_VERBOSE("[unified_mesh] Failed to load texture: %s\n", tex_path);
    int path_count = engine_texture_search_path_count();
    if (path_count == 0) {
      LOG_VERBOSE("[unified_mesh]   No texture search paths registered. "
                  "Call engine_register_texture_search_paths() in on_init.\n");
    } else {
      for (int i = 0; i < path_count; i++) {
        LOG_VERBOSE("[unified_mesh]   Tried root: %s\n",
                    engine_texture_search_path(i));
      }
    }
    JS_FreeCString(ctx, tex_path);
    return JS_NULL;
  }

  LOG_VERBOSE("[unified_mesh] Loaded texture: %s\n", path);
  JS_FreeCString(ctx, tex_path);

  fseek(f, 0, SEEK_END);
  long file_size = ftell(f);
  fseek(f, 0, SEEK_SET);

  uint8_t *file_data = malloc(file_size);
  fread(file_data, 1, file_size, f);
  fclose(f);

  stbi_set_flip_vertically_on_load(1);
  int w, h, channels;
  uint8_t *pixels =
      stbi_load_from_memory(file_data, (int)file_size, &w, &h, &channels, 4);
  free(file_data);

  if (!pixels) {
    return JS_NULL;
  }

  JSValue result = JS_NewObject(ctx);
  JS_SetPropertyStr(ctx, result, "width", JS_NewInt32(ctx, w));
  JS_SetPropertyStr(ctx, result, "height", JS_NewInt32(ctx, h));

  size_t pixel_size = (size_t)w * h * 4;
  JSValue pixel_buf = JS_NewArrayBufferCopy(ctx, pixels, pixel_size);

  JS_SetPropertyStr(ctx, result, "pixels", pixel_buf);

  stbi_image_free(pixels);

  return result;
}

static JSValue js_alloc(JSContext *ctx, JSValueConst this_val, int argc,
                        JSValueConst *argv) {
  ensure_initialized();

  int max_verts = DEFAULT_MAX_VERTICES;
  if (argc >= 1 && !JS_IsUndefined(argv[0])) {
    JS_ToInt32(ctx, &max_verts, argv[0]);
    if (max_verts <= 0) max_verts = DEFAULT_MAX_VERTICES;
  }
  int max_indices = max_verts * 2;

  for (int i = 0; i < MAX_UNIFIED_MESHES; i++) {
    if (!g_unified_meshes[i].valid) {
      UnifiedMesh *um = &g_unified_meshes[i];

      um->max_vertices = max_verts;
      um->max_indices = max_indices;

      um->vbuf = sg_make_buffer(&(sg_buffer_desc){
          .size = max_verts * sizeof(Vertex),
          .usage.dynamic_update = true,
          .label = "unified_mesh_vbuf",
      });

      um->ibuf = sg_make_buffer(
          &(sg_buffer_desc){.size = max_indices * sizeof(uint32_t),
                            .usage.index_buffer = true,
                            .usage.dynamic_update = true,
                            .label = "unified_mesh_ibuf"});

      um->vertex_count = 0;
      um->index_count = 0;
      um->atlas_texture_id = -1;
      um->valid = true;
      um->uploaded = false;

      memset(&um->persistent_mesh, 0, sizeof(Mesh));

      material_init(&um->material);
      material_set_shader(&um->material, g_shader);
      material_set_shader_type(&um->material, SHADER_TYPE_TEXTURED);

      LOG_INFO("[unified_mesh] Allocated slot %d (maxVerts=%d)\n", i, max_verts);
      return JS_NewInt32(ctx, i);
    }
  }

  LOG_ERROR("[unified_mesh] No free slots! (MAX=%d)\n", MAX_UNIFIED_MESHES);
  return JS_NewInt32(ctx, -1);
}

static JSValue js_free_slot(JSContext *ctx, JSValueConst this_val, int argc,
                            JSValueConst *argv) {
  if (argc < 1)
    return JS_UNDEFINED;

  int32_t id;
  if (JS_ToInt32(ctx, &id, argv[0]) < 0)
    return JS_EXCEPTION;
  if (id < 0 || id >= MAX_UNIFIED_MESHES)
    return JS_UNDEFINED;

  UnifiedMesh *um = &g_unified_meshes[id];
  if (um->valid) {
    if (um->vbuf.id != SG_INVALID_ID) {
      sg_destroy_buffer(um->vbuf);
    }
    if (um->ibuf.id != SG_INVALID_ID) {
      sg_destroy_buffer(um->ibuf);
    }
    um->valid = false;
    LOG_INFO("[unified_mesh] Freed slot %d\n", id);
  }

  return JS_UNDEFINED;
}

static JSValue js_upload(JSContext *ctx, JSValueConst this_val, int argc,
                         JSValueConst *argv) {
  ensure_initialized();

  if (argc < 7) {
    return JS_ThrowTypeError(ctx, "upload requires at least 7 arguments");
  }

  int32_t slot_id, atlas_tex_id;
  if (JS_ToInt32(ctx, &slot_id, argv[0]) < 0 ||
      JS_ToInt32(ctx, &atlas_tex_id, argv[1]) < 0) {
    return JS_EXCEPTION;
  }

  int32_t shader_type = SHADER_TYPE_TEXTURED;
  if (argc >= 8 && !JS_IsUndefined(argv[7])) {
    JS_ToInt32(ctx, &shader_type, argv[7]);
  }

  int32_t blend_mode = -1;
  if (argc >= 9 && !JS_IsUndefined(argv[8])) {
    JS_ToInt32(ctx, &blend_mode, argv[8]);
  }

  if (slot_id < 0 || slot_id >= MAX_UNIFIED_MESHES) {
    return JS_ThrowRangeError(ctx, "Invalid slot ID");
  }

  UnifiedMesh *um = &g_unified_meshes[slot_id];
  if (!um->valid) {
    return JS_ThrowInternalError(ctx, "Slot not allocated");
  }

  size_t vert_off, uv_off, color_off, tri_off, uv_rect_off;
  size_t vert_len, uv_len, color_len, tri_len, uv_rect_len;
  size_t bpe;

  JSValue vert_buf =
      JS_GetTypedArrayBuffer(ctx, argv[2], &vert_off, &vert_len, &bpe);
  JSValue uv_buf =
      JS_GetTypedArrayBuffer(ctx, argv[3], &uv_off, &uv_len, &bpe);
  JSValue color_buf =
      JS_GetTypedArrayBuffer(ctx, argv[4], &color_off, &color_len, &bpe);
  JSValue tri_buf =
      JS_GetTypedArrayBuffer(ctx, argv[5], &tri_off, &tri_len, &bpe);
  JSValue uv_rect_buf =
      JS_GetTypedArrayBuffer(ctx, argv[6], &uv_rect_off, &uv_rect_len, &bpe);

  size_t ab_len;
  uint8_t *vert_base = (uint8_t *)JS_GetArrayBuffer(ctx, &ab_len, vert_buf);
  uint8_t *uv_base = (uint8_t *)JS_GetArrayBuffer(ctx, &ab_len, uv_buf);
  uint8_t *color_base = (uint8_t *)JS_GetArrayBuffer(ctx, &ab_len, color_buf);
  uint8_t *tri_base = (uint8_t *)JS_GetArrayBuffer(ctx, &ab_len, tri_buf);
  uint8_t *uv_rect_base = (uint8_t *)JS_GetArrayBuffer(ctx, &ab_len, uv_rect_buf);

  float *verts = vert_base ? (float *)(vert_base + vert_off) : NULL;
  float *uvs = uv_base ? (float *)(uv_base + uv_off) : NULL;
  uint8_t *colors = color_base ? (uint8_t *)(color_base + color_off) : NULL;
  uint32_t *tris = tri_base ? (uint32_t *)(tri_base + tri_off) : NULL;
  float *uv_rects = uv_rect_base ? (float *)(uv_rect_base + uv_rect_off) : NULL;

  JS_FreeValue(ctx, vert_buf);
  JS_FreeValue(ctx, uv_buf);
  JS_FreeValue(ctx, color_buf);
  JS_FreeValue(ctx, tri_buf);
  JS_FreeValue(ctx, uv_rect_buf);

  if (!verts || !uvs || !colors || !tris || !uv_rects) {
    return JS_ThrowTypeError(ctx, "Invalid array data");
  }

  uint8_t *sway_data = NULL;
  JSValue sway_buf = JS_UNDEFINED;
  if (argc >= 10 && !JS_IsUndefined(argv[9]) && !JS_IsNull(argv[9])) {
    size_t sway_off, sway_len;
    sway_buf = JS_GetTypedArrayBuffer(ctx, argv[9], &sway_off, &sway_len, &bpe);
    uint8_t *sway_base = (uint8_t *)JS_GetArrayBuffer(ctx, &ab_len, sway_buf);
    sway_data = sway_base ? (uint8_t *)(sway_base + sway_off) : NULL;
    JS_FreeValue(ctx, sway_buf);
  }

  int vert_count = vert_len / sizeof(float) / 3;
  int index_count = tri_len / sizeof(uint32_t);

  if (vert_count > um->max_vertices) {
    return JS_ThrowRangeError(ctx, "Too many vertices (%d > slot max %d)",
                              vert_count, um->max_vertices);
  }
  if (index_count > um->max_indices) {
    return JS_ThrowRangeError(ctx, "Too many indices (%d > slot max %d)",
                              index_count, um->max_indices);
  }

  Vertex *gpu_verts = (Vertex *)malloc(vert_count * sizeof(Vertex));
  if (!gpu_verts) {
    return JS_ThrowOutOfMemory(ctx);
  }

  memset(gpu_verts, 0, vert_count * sizeof(Vertex));

  for (int i = 0; i < vert_count; i++) {

    gpu_verts[i].pos[0] = verts[i * 3 + 0];
    gpu_verts[i].pos[1] = verts[i * 3 + 1];
    gpu_verts[i].pos[2] = verts[i * 3 + 2];

    gpu_verts[i].uv[0] = uvs[i * 2 + 0];
    gpu_verts[i].uv[1] = uvs[i * 2 + 1];

    gpu_verts[i].uv_rect[0] = uv_rects[i * 4 + 0];
    gpu_verts[i].uv_rect[1] = uv_rects[i * 4 + 1];
    gpu_verts[i].uv_rect[2] = uv_rects[i * 4 + 2];
    gpu_verts[i].uv_rect[3] = uv_rects[i * 4 + 3];

    gpu_verts[i].col[0] = colors[i * 4 + 0] / 255.0f;
    gpu_verts[i].col[1] = colors[i * 4 + 1] / 255.0f;
    gpu_verts[i].col[2] = colors[i * 4 + 2] / 255.0f;
    gpu_verts[i].col[3] = colors[i * 4 + 3] / 255.0f;

    gpu_verts[i]._pad0 = 0;
    gpu_verts[i].quad_flags = 0;

    gpu_verts[i]._padding[0] = sway_data ? sway_data[i] : 0;
  }

  uint32_t *indices32 = (uint32_t *)malloc(index_count * sizeof(uint32_t));
  if (!indices32) {
    free(gpu_verts);
    return JS_ThrowOutOfMemory(ctx);
  }

  for (int i = 0; i < index_count; i++) {
    indices32[i] = (uint32_t)tris[i];
  }

  sg_update_buffer(um->vbuf, &(sg_range){.ptr = gpu_verts,
                                         .size = vert_count * sizeof(Vertex)});

  sg_update_buffer(
      um->ibuf,
      &(sg_range){.ptr = indices32, .size = index_count * sizeof(uint32_t)});

  free(gpu_verts);
  free(indices32);

  um->vertex_count = vert_count;
  um->index_count = index_count;
  um->atlas_texture_id = atlas_tex_id;
  um->uploaded = true;

  {
    int atlas_slot = atlas_tex_id;
    if (atlas_tex_id >= ATLAS_TEX_ID_OFFSET) {
      atlas_slot = atlas_tex_id - ATLAS_TEX_ID_OFFSET;
    }
    if (atlas_slot >= 0 && atlas_slot < MAX_ATLAS_TEXTURES &&
        g_atlas_textures[atlas_slot].valid) {
      um->material.texture = g_atlas_textures[atlas_slot].image;
      um->material.texture_view = g_atlas_textures[atlas_slot].view;
    }
  }

  material_set_shader_type(&um->material, (ShaderType)shader_type);

  if (blend_mode >= 0) {
    material_set_blend_mode(&um->material, (BlendMode)blend_mode);

    if (blend_mode == BLEND_MODE_NO_DEPTH_TEST) {
      material_set_render_queue(&um->material, RQ_OVERLAY);
    }
  }

  LOG_VERBOSE(
      "[unified_mesh] Uploaded slot %d: %d verts, %d indices, atlas=%d, shader=%d, blend=%d\n",
      slot_id, vert_count, index_count, atlas_tex_id, shader_type, blend_mode);

  return JS_UNDEFINED;
}

static JSValue js_draw(JSContext *ctx, JSValueConst this_val, int argc,
                       JSValueConst *argv) {
  if (argc < 1)
    return JS_UNDEFINED;

  int32_t slot_id;
  if (JS_ToInt32(ctx, &slot_id, argv[0]) < 0)
    return JS_EXCEPTION;
  if (slot_id < 0 || slot_id >= MAX_UNIFIED_MESHES)
    return JS_UNDEFINED;

  UnifiedMesh *um = &g_unified_meshes[slot_id];
  if (!um->valid || !um->uploaded || um->index_count == 0) {

    if (!um->valid) return JS_NewInt32(ctx, -1);
    if (!um->uploaded) return JS_NewInt32(ctx, -2);
    return JS_NewInt32(ctx, -3);
  }

  um->persistent_mesh.vbuf = um->vbuf;
  um->persistent_mesh.ibuf = um->ibuf;
  um->persistent_mesh.vert_count = um->vertex_count;
  um->persistent_mesh.tri_count = um->index_count;
  um->persistent_mesh.gpu_valid = true;
  um->persistent_mesh.dirty = false;

  graphics_draw_mesh_identity(&um->persistent_mesh, &um->material, 0);

  return JS_NewInt32(ctx, 1);
}

static JSValue js_save_atlas_png(JSContext *ctx, JSValueConst this_val,
                                 int argc, JSValueConst *argv) {
  if (argc < 4) {
    return JS_ThrowTypeError(ctx, "save_atlas_png requires 4 arguments");
  }

  const char *path = JS_ToCString(ctx, argv[0]);
  if (!path) {
    return JS_EXCEPTION;
  }

  int32_t width, height;
  if (JS_ToInt32(ctx, &width, argv[1]) < 0 ||
      JS_ToInt32(ctx, &height, argv[2]) < 0) {
    JS_FreeCString(ctx, path);
    return JS_EXCEPTION;
  }

  size_t pixels_len;
  size_t offset, size;
  JSValue buf =
      JS_GetTypedArrayBuffer(ctx, argv[3], &offset, &size, &pixels_len);
  uint8_t *pixels = (uint8_t *)JS_GetArrayBuffer(ctx, &pixels_len, buf);
  JS_FreeValue(ctx, buf);

  if (!pixels) {
    JS_FreeCString(ctx, path);
    return JS_ThrowTypeError(ctx, "Invalid pixel data");
  }

  size_t expected_len = (size_t)width * height * 4;
  if (size < expected_len) {
    JS_FreeCString(ctx, path);
    return JS_ThrowTypeError(ctx, "Pixel data too small");
  }

  int stride = width * 4;
  int result = stbi_write_png(path, width, height, 4, pixels, stride);

  LOG_INFO("[unified_mesh] save_atlas_png: %s (%dx%d) -> %s\n", path, width,
           height, result ? "OK" : "FAILED");

  JS_FreeCString(ctx, path);

  return JS_NewBool(ctx, result != 0);
}

static JSValue js_stats(JSContext *ctx, JSValueConst this_val, int argc,
                        JSValueConst *argv) {
  int valid_count = 0;
  int total_verts = 0;
  int total_tris = 0;

  for (int i = 0; i < MAX_UNIFIED_MESHES; i++) {
    if (g_unified_meshes[i].valid) {
      valid_count++;
      total_verts += g_unified_meshes[i].vertex_count;
      total_tris += g_unified_meshes[i].index_count / 3;
    }
  }

  JSValue result = JS_NewObject(ctx);
  JS_SetPropertyStr(ctx, result, "slot_count", JS_NewInt32(ctx, valid_count));
  JS_SetPropertyStr(ctx, result, "vertex_count", JS_NewInt32(ctx, total_verts));
  JS_SetPropertyStr(ctx, result, "triangle_count",
                    JS_NewInt32(ctx, total_tris));
  JS_SetPropertyStr(ctx, result, "atlas_count",
                    JS_NewInt32(ctx, g_atlas_texture_count));
  JS_SetPropertyStr(ctx, result, "max_slots",
                    JS_NewInt32(ctx, MAX_UNIFIED_MESHES));
  JS_SetPropertyStr(ctx, result, "max_vertices",
                    JS_NewInt32(ctx, MAX_UNIFIED_VERTICES));

  return result;
}

void unified_mesh_dump_atlas(void) {
  fprintf(stderr, "[unified_mesh] dump_atlas: count=%d\n", g_atlas_texture_count);

  if (g_atlas_texture_count == 0) {
    fprintf(stderr, "[unified_mesh] dump_atlas: no atlas textures\n");
    return;
  }

  for (int i = 0; i < g_atlas_texture_count; i++) {
    AtlasTexture *at = &g_atlas_textures[i];
    fprintf(stderr, "[unified_mesh] dump_atlas[%d]: valid=%d pixels=%p w=%d h=%d\n",
            i, at->valid, (void*)at->pixels, at->width, at->height);
    if (!at->valid || !at->pixels) {
      continue;
    }

    char path[256];
    snprintf(path, sizeof(path), "/tmp/atlas_%d.png", i);

    int stride = at->width * 4;
    int result = stbi_write_png(path, at->width, at->height, 4, at->pixels, stride);

    if (result) {
      LOG_INFO("[unified_mesh] dump_atlas: saved %s (%dx%d)\n", path, at->width,
               at->height);
    } else {
      LOG_ERROR("[unified_mesh] dump_atlas: FAILED to save %s\n", path);
    }
  }
}

int js_init_unified_mesh_module(JSContext *ctx) {
  JSValue global = JS_GetGlobalObject(ctx);
  JSValue obj = JS_NewObject(ctx);

  JS_SetPropertyStr(ctx, obj, "alloc",
                    JS_NewCFunction(ctx, js_alloc, "alloc", 1));
  JS_SetPropertyStr(ctx, obj, "free",
                    JS_NewCFunction(ctx, js_free_slot, "free", 1));
  JS_SetPropertyStr(ctx, obj, "upload",
                    JS_NewCFunction(ctx, js_upload, "upload", 6));
  JS_SetPropertyStr(ctx, obj, "draw", JS_NewCFunction(ctx, js_draw, "draw", 1));

  JS_SetPropertyStr(ctx, obj, "upload_atlas",
                    JS_NewCFunction(ctx, js_upload_atlas, "upload_atlas", 3));
  JS_SetPropertyStr(ctx, obj, "load_prebaked_atlas",
                    JS_NewCFunction(ctx, js_load_prebaked_atlas,
                                    "load_prebaked_atlas", 1));
  JS_SetPropertyStr(ctx, obj, "read_prebaked_rgba",
                    JS_NewCFunction(ctx, js_read_prebaked_rgba,
                                    "read_prebaked_rgba", 1));
  JS_SetPropertyStr(ctx, obj, "upload_prebaked_rgba",
                    JS_NewCFunction(ctx, js_upload_prebaked_rgba,
                                    "upload_prebaked_rgba", 1));
  JS_SetPropertyStr(ctx, obj, "mod_pack_apply",
                    JS_NewCFunction(ctx, js_mod_pack_apply,
                                    "mod_pack_apply", 3));
  JS_SetPropertyStr(
      ctx, obj, "load_texture_pixels",
      JS_NewCFunction(ctx, js_load_texture_pixels, "load_texture_pixels", 1));
  JS_SetPropertyStr(
      ctx, obj, "save_atlas_png",
      JS_NewCFunction(ctx, js_save_atlas_png, "save_atlas_png", 4));

  JS_SetPropertyStr(ctx, obj, "stats",
                    JS_NewCFunction(ctx, js_stats, "stats", 0));

  JS_SetPropertyStr(ctx, global, "unified_mesh", obj);
  JS_FreeValue(ctx, global);

  LOG_INFO("[unified_mesh] Module initialized\n");

  return 0;
}

bool atlas_texture_get(int texture_id, sg_image *out_image,
                       sg_view *out_view) {

  int slot = texture_id - ATLAS_TEX_ID_OFFSET;
  if (slot < 0 || slot >= MAX_ATLAS_TEXTURES)
    return false;
  AtlasTexture *at = &g_atlas_textures[slot];
  if (!at->valid)
    return false;
  *out_image = at->image;
  *out_view = at->view;
  return true;
}

bool atlas_texture_get_size(int texture_id, int *out_width, int *out_height) {
  int slot = texture_id - ATLAS_TEX_ID_OFFSET;
  if (slot < 0 || slot >= MAX_ATLAS_TEXTURES)
    return false;
  AtlasTexture *at = &g_atlas_textures[slot];
  if (!at->valid)
    return false;
  *out_width = at->width;
  *out_height = at->height;
  return true;
}
