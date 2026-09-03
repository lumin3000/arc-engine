// graphics_bindings.c - Graphics API JS 绑定
//
// 对齐 Unity-style: Graphics.DrawMesh(mesh, loc, quat, material, layer)
//
// 用于每帧重提交的动态 mesh：任何 transform / 旋转 / 材质会变的对象
// （角色、可动门、投射物等）。引擎不区分调用者类型，由游戏决定。
//
// API:
//   graphics.draw_mesh(meshId, x, y, z, qx, qy, qz, qw, textureId, layer)
//   graphics.create_quad_mesh() -> meshId
//   graphics.free_mesh(meshId)

#include "bedrock/utils/utils.h"

// ... existing includes ...

// In js_graphics_create_quad_mesh:
// mesh_upload_to_gpu_with_material(&dm->mesh, (const struct
// Material*)&dm->material);
#include "../log.h"
#include "bedrock/gfx/graphics.h"
#include "bedrock/gfx/material.h"
#include "bedrock/gfx/mesh.h"
#include "bedrock/gfx/render.h"
#include "quickjs.h"
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// 常量
// ============================================================================

#define MAX_DYNAMIC_MESHES 2048

// ============================================================================
// DynamicMesh 结构
// ============================================================================

typedef struct {
  Mesh mesh;
  Material material;
  bool valid;
} DynamicMesh;

static DynamicMesh g_dynamic_meshes[MAX_DYNAMIC_MESHES];
static bool g_initialized = false;

// ============================================================================
// 初始化
// ============================================================================

static void ensure_initialized(void) {
  if (g_initialized)
    return;

  memset(g_dynamic_meshes, 0, sizeof(g_dynamic_meshes));
  g_initialized = true;
  LOG_INFO("Graphics binding initialized. MAX_DYNAMIC_MESHES=%d",
           MAX_DYNAMIC_MESHES);
  LOG_VERBOSE("[graphics_bindings] initialized (%d slots)\n",
              MAX_DYNAMIC_MESHES);
}

// ============================================================================
// 辅助函数：四元数转旋转矩阵
// ============================================================================

static void quat_to_matrix4(float qx, float qy, float qz, float qw,
                            Matrix4 out) {
  // 归一化四元数
  float len = sqrtf(qx * qx + qy * qy + qz * qz + qw * qw);
  if (len > 0.0001f) {
    qx /= len;
    qy /= len;
    qz /= len;
    qw /= len;
  }

  float xx = qx * qx;
  float yy = qy * qy;
  float zz = qz * qz;
  float xy = qx * qy;
  float xz = qx * qz;
  float yz = qy * qz;
  float wx = qw * qx;
  float wy = qw * qy;
  float wz = qw * qz;

  // 列主序 (Column-major)
  out[0] = 1.0f - 2.0f * (yy + zz);
  out[1] = 2.0f * (xy + wz);
  out[2] = 2.0f * (xz - wy);
  out[3] = 0.0f;

  out[4] = 2.0f * (xy - wz);
  out[5] = 1.0f - 2.0f * (xx + zz);
  out[6] = 2.0f * (yz + wx);
  out[7] = 0.0f;

  out[8] = 2.0f * (xz + wy);
  out[9] = 2.0f * (yz - wx);
  out[10] = 1.0f - 2.0f * (xx + yy);
  out[11] = 0.0f;

  out[12] = 0.0f;
  out[13] = 0.0f;
  out[14] = 0.0f;
  out[15] = 1.0f;
}

// static void make_transform_matrix(float x, float y, float z, float qx, float
// qy,
//                                   float qz, float qw, Matrix4 out) {
// ... removed ...
// }

// ============================================================================
// graphics.create_quad_mesh() -> meshId
//
// 创建一个单位四边形 Mesh (1x1，中心在原点)
// 用于动态 mesh 渲染
// ============================================================================

static JSValue js_graphics_create_quad_mesh(JSContext *ctx,
                                            JSValueConst this_val, int argc,
                                            JSValueConst *argv) {
  ensure_initialized();

  // 查找空闲 slot
  int id = -1;
  for (int i = 0; i < MAX_DYNAMIC_MESHES; i++) {
    if (!g_dynamic_meshes[i].valid) {
      id = i;
      break;
    }
  }

  if (id < 0) {
    LOG_ERROR("graphics.create_quad_mesh: No free slots! MAX=%d",
              MAX_DYNAMIC_MESHES);
    return JS_ThrowRangeError(ctx, "graphics: no free mesh slots");
  }

  LOG_INFO("graphics.create_quad_mesh: Allocated mesh id=%d", id);

  DynamicMesh *dm = &g_dynamic_meshes[id];

  // 初始化 Mesh (4 顶点, 6 索引)
  mesh_init(&dm->mesh, 4, 6);

  // 单位四边形顶点 (1x1, XZ 平面, Y=0 高度)
  // [Phase 7] 修复: 从 XY 平面改为 XZ 平面，以适应俯视相机 (Top-Down)
  // 顶点顺序: 西南(0), 西北(1), 东北(2), 东南(3)
  // mesh_add_vertex(mesh, x, y, z, r, g, b, a, u, v)
  mesh_add_vertex(&dm->mesh, -0.5f, 0.0f, -0.5f, 1, 1, 1, 1, 0, 0); // 西南
  mesh_add_vertex(&dm->mesh, -0.5f, 0.0f, 0.5f, 1, 1, 1, 1, 0, 1);  // 西北
  mesh_add_vertex(&dm->mesh, 0.5f, 0.0f, 0.5f, 1, 1, 1, 1, 1, 1);   // 东北
  mesh_add_vertex(&dm->mesh, 0.5f, 0.0f, -0.5f, 1, 1, 1, 1, 1, 0);  // 东南

  // 两个三角形 (CCW Winding for Top-Down View)
  // 0(SW) -> 3(SE) -> 2(NE)
  // 0(SW) -> 2(NE) -> 1(NW)
  mesh_add_triangle(&dm->mesh, 0, 3, 2);
  mesh_add_triangle(&dm->mesh, 0, 2, 1);

  // 初始化默认材质 (必须在 mesh_upload 之前)
  material_init(&dm->material);
  material_set_color(&dm->material, 1.0f, 1.0f, 1.0f, 1.0f);
  material_set_render_queue(&dm->material, RQ_TRANSPARENT);
  // 使用 TEXTURED 模式 (tex_index=0)，因为 Graphics.drawMesh
  // 通常用于带纹理的渲染
  material_set_shader_type(&dm->material, SHADER_TYPE_TEXTURED);

  // 上传到 GPU (使用 Material 的 shader_type)
  mesh_upload_to_gpu_with_material(&dm->mesh, &dm->material);

  dm->valid = true;

  return JS_NewInt32(ctx, id);
}

// ============================================================================
// graphics.create_mesh(positions, colors, uvs, indices, shaderType) -> meshId
// 从 TypedArray 创建任意网格（instanced 物品渲染的网格来源）
//   positions: Float32Array (n*3)            必需
//   colors:    Float32Array (n*4) 或 null    缺省全白
//   uvs:       Float32Array (n*2) 或 null    缺省 0
//   indices:   Int32Array   (tri*3)          必需
//   shaderType: int (ShaderType 枚举值, 如 SHADER_TYPE_NONE=0 纯顶点色)
//   normals:   Float32Array (n*3) 或省略     缺省零法线=不参与光照
// ============================================================================

static bool typed_array_floats(JSContext *ctx, JSValueConst val,
                               const float **out_ptr, size_t *out_bytes) {
  size_t off, len;
  JSValue buf = JS_GetTypedArrayBuffer(ctx, val, &off, &len, NULL);
  if (JS_IsException(buf))
    return false;
  size_t full;
  uint8_t *ptr = JS_GetArrayBuffer(ctx, &full, buf);
  JS_FreeValue(ctx, buf);
  if (!ptr)
    return false;
  *out_ptr = (const float *)(ptr + off);
  *out_bytes = len;
  return true;
}

static JSValue js_graphics_create_mesh(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv) {
  ensure_initialized();

  if (argc < 5) {
    return JS_ThrowTypeError(
        ctx, "graphics.create_mesh requires (positions, colors, uvs, indices, "
             "shaderType)");
  }

  int id = -1;
  for (int i = 0; i < MAX_DYNAMIC_MESHES; i++) {
    if (!g_dynamic_meshes[i].valid) {
      id = i;
      break;
    }
  }
  if (id < 0)
    return JS_ThrowRangeError(ctx, "graphics: no free mesh slots");

  const float *positions;
  size_t pos_bytes;
  if (!typed_array_floats(ctx, argv[0], &positions, &pos_bytes))
    return JS_ThrowTypeError(ctx, "create_mesh: positions must be Float32Array");
  const int vert_count = (int)(pos_bytes / (3 * sizeof(float)));
  if (vert_count <= 0)
    return JS_ThrowRangeError(ctx, "create_mesh: empty positions");

  const float *colors = NULL;
  size_t col_bytes = 0;
  if (!JS_IsNull(argv[1]) && !JS_IsUndefined(argv[1])) {
    if (!typed_array_floats(ctx, argv[1], &colors, &col_bytes) ||
        col_bytes < (size_t)vert_count * 4 * sizeof(float))
      return JS_ThrowTypeError(ctx, "create_mesh: colors must be Float32Array(n*4)");
  }

  const float *uvs = NULL;
  size_t uv_bytes = 0;
  if (!JS_IsNull(argv[2]) && !JS_IsUndefined(argv[2])) {
    if (!typed_array_floats(ctx, argv[2], &uvs, &uv_bytes) ||
        uv_bytes < (size_t)vert_count * 2 * sizeof(float))
      return JS_ThrowTypeError(ctx, "create_mesh: uvs must be Float32Array(n*2)");
  }

  const float *indices_f;
  size_t idx_bytes;
  if (!typed_array_floats(ctx, argv[3], &indices_f, &idx_bytes))
    return JS_ThrowTypeError(ctx, "create_mesh: indices must be Int32Array");
  const int *indices = (const int *)indices_f;
  const int tri_count = (int)(idx_bytes / (3 * sizeof(int)));
  if (tri_count <= 0)
    return JS_ThrowRangeError(ctx, "create_mesh: empty indices");

  int32_t shader_type = 0;
  if (JS_ToInt32(ctx, &shader_type, argv[4]) < 0)
    return JS_EXCEPTION;

  const float *normals = NULL;
  size_t nrm_bytes = 0;
  if (argc >= 6 && !JS_IsNull(argv[5]) && !JS_IsUndefined(argv[5])) {
    if (!typed_array_floats(ctx, argv[5], &normals, &nrm_bytes) ||
        nrm_bytes < (size_t)vert_count * 3 * sizeof(float))
      return JS_ThrowTypeError(ctx, "create_mesh: normals must be Float32Array(n*3)");
  }

  DynamicMesh *dm = &g_dynamic_meshes[id];
  // Mesh.tri_count 语义是"索引数"(graphics.c sg_draw 直接用)——传三角数会三倍截断
  // (架构文档附录·遗漏#3)
  mesh_init(&dm->mesh, vert_count, tri_count * 3);
  mesh_set_vertices(&dm->mesh, positions, vert_count);
  if (colors) {
    mesh_set_colors(&dm->mesh, colors, vert_count);
  } else {
    for (int i = 0; i < vert_count; i++) {
      dm->mesh.colors[i * 4 + 0] = 1.0f;
      dm->mesh.colors[i * 4 + 1] = 1.0f;
      dm->mesh.colors[i * 4 + 2] = 1.0f;
      dm->mesh.colors[i * 4 + 3] = 1.0f;
    }
  }
  if (uvs) {
    mesh_set_uvs(&dm->mesh, uvs, vert_count);
  } else {
    memset(dm->mesh.uvs, 0, (size_t)vert_count * 2 * sizeof(float));
  }
  mesh_set_triangles(&dm->mesh, indices, tri_count * 3);
  if (normals) {
    mesh_set_normals(&dm->mesh, normals, vert_count);
  }

  material_init(&dm->material);
  material_set_color(&dm->material, 1.0f, 1.0f, 1.0f, 1.0f);
  material_set_render_queue(&dm->material, RQ_TRANSPARENT);
  material_set_shader_type(&dm->material, (ShaderType)shader_type);

  mesh_upload_to_gpu_with_material(&dm->mesh, &dm->material);
  dm->valid = true;

  LOG_INFO("graphics.create_mesh: id=%d verts=%d tris=%d shader=%d", id,
           vert_count, tri_count, shader_type);
  return JS_NewInt32(ctx, id);
}

// ============================================================================
// graphics.create_quad_mesh_uv(u0, v0, u1, v1) -> meshId
// 创建自定义 UV 的四边形 mesh（用于 atlas 区域采样）
// ============================================================================

static JSValue js_graphics_create_quad_mesh_uv(JSContext *ctx,
                                               JSValueConst this_val, int argc,
                                               JSValueConst *argv) {
  ensure_initialized();

  if (argc < 4) {
    return JS_ThrowTypeError(ctx,
                             "graphics.create_quad_mesh_uv requires (u0, v0, u1, v1)");
  }

  double u0, v0, u1, v1;
  JS_ToFloat64(ctx, &u0, argv[0]);
  JS_ToFloat64(ctx, &v0, argv[1]);
  JS_ToFloat64(ctx, &u1, argv[2]);
  JS_ToFloat64(ctx, &v1, argv[3]);

  // 查找空闲 slot
  int id = -1;
  for (int i = 0; i < MAX_DYNAMIC_MESHES; i++) {
    if (!g_dynamic_meshes[i].valid) {
      id = i;
      break;
    }
  }

  if (id < 0) {
    LOG_ERROR("graphics.create_quad_mesh_uv: No free slots! MAX=%d",
              MAX_DYNAMIC_MESHES);
    return JS_ThrowRangeError(ctx, "graphics: no free mesh slots");
  }

  DynamicMesh *dm = &g_dynamic_meshes[id];

  mesh_init(&dm->mesh, 4, 6);

  // 单位四边形顶点 (1x1, XZ 平面, Y=0)，UV 使用传入的 atlas 区域
  mesh_add_vertex(&dm->mesh, -0.5f, 0.0f, -0.5f, 1, 1, 1, 1, (float)u0, (float)v0); // SW
  mesh_add_vertex(&dm->mesh, -0.5f, 0.0f,  0.5f, 1, 1, 1, 1, (float)u0, (float)v1); // NW
  mesh_add_vertex(&dm->mesh,  0.5f, 0.0f,  0.5f, 1, 1, 1, 1, (float)u1, (float)v1); // NE
  mesh_add_vertex(&dm->mesh,  0.5f, 0.0f, -0.5f, 1, 1, 1, 1, (float)u1, (float)v0); // SE

  mesh_add_triangle(&dm->mesh, 0, 3, 2);
  mesh_add_triangle(&dm->mesh, 0, 2, 1);

  material_init(&dm->material);
  material_set_color(&dm->material, 1.0f, 1.0f, 1.0f, 1.0f);
  material_set_render_queue(&dm->material, RQ_TRANSPARENT);
  material_set_shader_type(&dm->material, SHADER_TYPE_TEXTURED);

  mesh_upload_to_gpu_with_material(&dm->mesh, &dm->material);

  dm->valid = true;

  return JS_NewInt32(ctx, id);
}

// ============================================================================
// graphics.free_mesh(meshId)
// ============================================================================

static JSValue js_graphics_free_mesh(JSContext *ctx, JSValueConst this_val,
                                     int argc, JSValueConst *argv) {
  ensure_initialized();

  if (argc < 1) {
    return JS_ThrowTypeError(ctx, "graphics.free_mesh requires meshId");
  }

  int32_t id;
  if (JS_ToInt32(ctx, &id, argv[0]) < 0)
    return JS_EXCEPTION;

  if (id < 0 || id >= MAX_DYNAMIC_MESHES) {
    return JS_UNDEFINED;
  }

  DynamicMesh *dm = &g_dynamic_meshes[id];
  if (dm->valid) {
    mesh_free(&dm->mesh);
    dm->valid = false;
  }

  return JS_UNDEFINED;
}

// ============================================================================
// Texture Management
// ============================================================================

#define MAX_GRAPHICS_TEXTURES 256
#define MAX_GRAPHICS_TEXTURE_PATH_LEN 256

typedef struct {
  char path[MAX_GRAPHICS_TEXTURE_PATH_LEN];
  sg_image image;
  sg_view view;
  bool valid;
  // staged 纹理 (texture_create_staged): CPU 侧全图镜像, compose_layers 写入,
  // texture_flush_staged 整图上传。行序与 GPU 纹理一致 (= 图像空间垂直翻转,
  // 同 stbi flip-on-load 约定)。非 staged 纹理恒 NULL。
  uint8_t *staging;
  int width;
  int height;
} GraphicsTextureCache;

static GraphicsTextureCache g_graphics_textures[MAX_GRAPHICS_TEXTURES];

// stb_image externs (if not included via header)
extern void stbi_set_flip_vertically_on_load(int flag);
extern unsigned char *stbi_load_from_memory(const unsigned char *buffer,
                                            int len, int *x, int *y, int *comp,
                                            int req_comp);
extern void stbi_image_free(void *ptr);

// Helper: Read File Content
static void *read_file_content(const char *path, size_t *outLen) {
  FILE *f = fopen(path, "rb");
  if (!f)
    return NULL;
  fseek(f, 0, SEEK_END);
  long len = ftell(f);
  fseek(f, 0, SEEK_SET);
  void *buf = malloc(len + 1);
  if (buf) {
    fread(buf, 1, len, f);
    ((char *)buf)[len] = '\0';
  }
  fclose(f);
  if (outLen)
    *outLen = (size_t)len;
  return buf;
}

// Helper: Get or Load Texture
static int get_or_load_graphics_texture(const char *path) {
  // 1. Search existing
  for (int i = 0; i < MAX_GRAPHICS_TEXTURES; i++) {
    if (g_graphics_textures[i].valid &&
        strcmp(g_graphics_textures[i].path, path) == 0) {
      return i;
    }
  }

  // 2. Find free slot
  int slot = -1;
  for (int i = 0; i < MAX_GRAPHICS_TEXTURES; i++) {
    if (!g_graphics_textures[i].valid) {
      slot = i;
      break;
    }
  }

  if (slot < 0) {
    LOG_ERROR("Graphics Texture Cache Full!");
    return -1;
  }

  // 3. Load Texture
  // This logic mimics thing_mesh.c loading
  size_t fileLen;
  unsigned char *fileContent =
      (unsigned char *)read_file_content(path, &fileLen);
  if (!fileContent) {
    // 纹理文件不存在是正常情况（Graphic_Random 探测变体时）
    // 改为 VERBOSE 级别，避免刷屏
    LOG_VERBOSE("Texture file not found: %s\n", path);
    return -1;
  }

  int w, h, comp;
  stbi_set_flip_vertically_on_load(1);
  unsigned char *pixels =
      stbi_load_from_memory(fileContent, (int)fileLen, &w, &h, &comp, 4);
  free(fileContent);

  if (!pixels) {
    LOG_ERROR("Failed to decode texture: %s", path);
    return -1;
  }

  sg_image_desc img_desc = {
      .width = w,
      .height = h,
      .pixel_format = SG_PIXELFORMAT_RGBA8,
      .data.mip_levels[0] =
          {
              .ptr = pixels,
              .size = (size_t)(w * h * 4),
          },
  };

  sg_image img = sg_make_image(&img_desc);
  stbi_image_free(pixels);

  if (sg_query_image_state(img) != SG_RESOURCESTATE_VALID) {
    LOG_ERROR("Failed to create sg_image for: %s", path);
    return -1;
  }

  sg_view_desc view_desc = {
      .texture.image = img,
  };
  sg_view view = sg_make_view(&view_desc);

  // Store in cache
  strncpy(g_graphics_textures[slot].path, path,
          MAX_GRAPHICS_TEXTURE_PATH_LEN - 1);
  g_graphics_textures[slot].image = img;
  g_graphics_textures[slot].view = view;
  g_graphics_textures[slot].valid = true;

  LOG_VERBOSE("Loaded graphics texture: %s (id=%d)", path, slot);

  return slot;
}

// ============================================================================
// graphics.texture_from_pixels(rgbaBuf, w, h) -> textureId（大地图 B6b 替身）
// 从内存 RGBA 像素创建动态纹理（dynamic_update, 供 texture_update 重传）
// graphics.texture_update(textureId, rgbaBuf) — 整张重传（sokol 无局部更新）
// ============================================================================

static JSValue js_graphics_texture_from_pixels(JSContext *ctx, JSValueConst this_val,
                                               int argc, JSValueConst *argv) {
  if (argc < 3)
    return JS_ThrowTypeError(ctx, "texture_from_pixels(rgbaBuf, w, h)");

  int32_t w = 0, h = 0;
  JS_ToInt32(ctx, &w, argv[1]);
  JS_ToInt32(ctx, &h, argv[2]);
  if (w <= 0 || h <= 0 || w > 4096 || h > 4096)
    return JS_ThrowRangeError(ctx, "texture_from_pixels: bad dims %dx%d", w, h);

  size_t off, len, bpe;
  JSValue buf = JS_GetTypedArrayBuffer(ctx, argv[0], &off, &len, &bpe);
  if (JS_IsException(buf)) return buf;
  size_t ab_len;
  uint8_t *base = JS_GetArrayBuffer(ctx, &ab_len, buf);
  JS_FreeValue(ctx, buf);
  if (!base || len < (size_t)(w * h * 4))
    return JS_ThrowRangeError(ctx, "texture_from_pixels: buffer too small");

  int slot = -1;
  for (int i = 0; i < MAX_GRAPHICS_TEXTURES; i++) {
    if (!g_graphics_textures[i].valid) { slot = i; break; }
  }
  if (slot < 0)
    return JS_ThrowInternalError(ctx, "texture_from_pixels: texture cache full");

  sg_image img = sg_make_image(&(sg_image_desc){
      .width = w,
      .height = h,
      .pixel_format = SG_PIXELFORMAT_RGBA8,
      .usage.dynamic_update = true,
  });
  if (sg_query_image_state(img) != SG_RESOURCESTATE_VALID)
    return JS_ThrowInternalError(ctx, "texture_from_pixels: sg_make_image failed");
  sg_update_image(img, &(sg_image_data){
      .mip_levels[0] = { .ptr = base + off, .size = (size_t)(w * h * 4) },
  });

  sg_view view = sg_make_view(&(sg_view_desc){ .texture.image = img });
  snprintf(g_graphics_textures[slot].path, MAX_GRAPHICS_TEXTURE_PATH_LEN,
           "__pixels_%d_%dx%d__", slot, w, h);
  g_graphics_textures[slot].image = img;
  g_graphics_textures[slot].view = view;
  g_graphics_textures[slot].valid = true;
  return JS_NewInt32(ctx, slot);
}

static JSValue js_graphics_texture_update(JSContext *ctx, JSValueConst this_val,
                                          int argc, JSValueConst *argv) {
  if (argc < 2)
    return JS_ThrowTypeError(ctx, "texture_update(textureId, rgbaBuf)");
  int32_t id = 0;
  JS_ToInt32(ctx, &id, argv[0]);
  if (id < 0 || id >= MAX_GRAPHICS_TEXTURES || !g_graphics_textures[id].valid)
    return JS_ThrowRangeError(ctx, "texture_update: invalid texture %d", id);

  sg_image_desc desc = sg_query_image_desc(g_graphics_textures[id].image);
  size_t need = (size_t)(desc.width * desc.height * 4);

  size_t off, len, bpe;
  JSValue buf = JS_GetTypedArrayBuffer(ctx, argv[1], &off, &len, &bpe);
  if (JS_IsException(buf)) return buf;
  size_t ab_len;
  uint8_t *base = JS_GetArrayBuffer(ctx, &ab_len, buf);
  JS_FreeValue(ctx, buf);
  if (!base || len < need)
    return JS_ThrowRangeError(ctx, "texture_update: buffer too small (%d < %d)",
                              (int)len, (int)need);

  sg_update_image(g_graphics_textures[id].image, &(sg_image_data){
      .mip_levels[0] = { .ptr = base + off, .size = need },
  });
  return JS_UNDEFINED;
}

// ============================================================================
// 分层像素合成 (通用图像设施, 调用方定义层语义):
//   graphics.texture_create_staged(w, h) -> textureId
//   graphics.compose_layers(texId, slotX, slotY, slotW, slotH, layers, outlineR)
//   graphics.texture_flush_staged(texId)
//   graphics.texture_staged_read(texId, x, y, w, h) -> Uint8Array (图像空间, 行顶朝下)
//
// layers 每项: { path, dx, dy, flip?, scale?, tint?[4], fx?[4], op?: "erase" }
//   path  源图 (磁盘 PNG, 解码结果按 path 缓存)
//   dx/dy 槽内整数偏移 (图像空间, 左上原点)
//   flip  水平镜像
//   scale 绕图心等比缩放 (双线性)
//   tint  RGBA 乘子
//   fx    颜色变换 [mode, a, b, c], 数学与消费者 shader 侧染色逐式对齐:
//         mode 1: a=hue 角度(deg) W3C hue-rotate 阵, b=saturate 插值, c=明度标量
//         mode 2: a=hue, b=sat, c=明度增益 — 保明度重着色 + 暗部保护
// 合成: 槽先清零 → 逐层 (scale → fx → tint → 量化 8bit) alpha-over →
//       outlineR>0 时对整体 alpha 做半径 R 圆盘膨胀垫黑底 → 写入 staging。
// ============================================================================

#define MAX_COMPOSE_SRC_CACHE 512

typedef struct {
  char path[MAX_GRAPHICS_TEXTURE_PATH_LEN];
  int w, h;
  uint8_t *pixels; // RGBA, 图像空间 (行顶朝下, 未做 stbi 垂直翻转)
  bool valid;
} ComposeSrcCache;

static ComposeSrcCache g_compose_src[MAX_COMPOSE_SRC_CACHE];

static const ComposeSrcCache *compose_src_get(const char *path) {
  for (int i = 0; i < MAX_COMPOSE_SRC_CACHE; i++) {
    if (g_compose_src[i].valid && strcmp(g_compose_src[i].path, path) == 0)
      return &g_compose_src[i];
  }
  int slot = -1;
  for (int i = 0; i < MAX_COMPOSE_SRC_CACHE; i++) {
    if (!g_compose_src[i].valid) { slot = i; break; }
  }
  if (slot < 0) {
    LOG_ERROR("compose_layers: source cache full (%d)", MAX_COMPOSE_SRC_CACHE);
    return NULL;
  }
  size_t fileLen;
  unsigned char *fileContent = (unsigned char *)read_file_content(path, &fileLen);
  if (!fileContent) {
    LOG_ERROR("compose_layers: source not found: %s", path);
    return NULL;
  }
  int w, h, comp;
  stbi_set_flip_vertically_on_load(0); // 合成在图像空间, 不翻
  unsigned char *pixels =
      stbi_load_from_memory(fileContent, (int)fileLen, &w, &h, &comp, 4);
  stbi_set_flip_vertically_on_load(1); // 恢复纹理加载约定
  free(fileContent);
  if (!pixels) {
    LOG_ERROR("compose_layers: decode failed: %s", path);
    return NULL;
  }
  strncpy(g_compose_src[slot].path, path, MAX_GRAPHICS_TEXTURE_PATH_LEN - 1);
  g_compose_src[slot].w = w;
  g_compose_src[slot].h = h;
  g_compose_src[slot].pixels = (uint8_t *)pixels; // stbi malloc, 常驻缓存
  g_compose_src[slot].valid = true;
  return &g_compose_src[slot];
}

static float compose_smoothstep(float lo, float hi, float x) {
  float t = (x - lo) / (hi - lo);
  t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
  return t * t * (3.0f - 2.0f * t);
}

static void compose_hsl_to_rgb(float h, float s, float l, float out[3]) {
  float c = (1.0f - fabsf(2.0f * l - 1.0f)) * s;
  float hp = fmodf(h, 360.0f) / 60.0f;
  if (hp < 0.0f) hp += 6.0f;
  float x = c * (1.0f - fabsf(fmodf(hp, 2.0f) - 1.0f));
  float r1, g1, b1;
  if (hp < 1.0f)      { r1 = c; g1 = x; b1 = 0; }
  else if (hp < 2.0f) { r1 = x; g1 = c; b1 = 0; }
  else if (hp < 3.0f) { r1 = 0; g1 = c; b1 = x; }
  else if (hp < 4.0f) { r1 = 0; g1 = x; b1 = c; }
  else if (hp < 5.0f) { r1 = x; g1 = 0; b1 = c; }
  else                { r1 = c; g1 = 0; b1 = x; }
  float m = l - 0.5f * c;
  out[0] = r1 + m; out[1] = g1 + m; out[2] = b1 + m;
}

// 与消费者 shader 染色逐式对齐 (mode1=hue-rotate·saturate·light, mode2=保L重着色);
// 暗部保护窗与 shader 常量同值
#define COMPOSE_FX_DARK_LO 0.10f
#define COMPOSE_FX_DARK_HI 0.24f

static void compose_fx_apply(float rgb[3], const float fx[4]) {
  float mode = fx[0];
  if (mode < 0.5f) return;
  if (mode < 1.5f) {
    float rad = fx[1] * (float)M_PI / 180.0f;
    float c = cosf(rad), s = sinf(rad);
    float r = rgb[0], g = rgb[1], b = rgb[2];
    float or_ = (0.213f + c * 0.787f - s * 0.213f) * r +
                (0.715f - c * 0.715f - s * 0.715f) * g +
                (0.072f - c * 0.072f + s * 0.928f) * b;
    float og = (0.213f - c * 0.213f + s * 0.143f) * r +
               (0.715f + c * 0.285f + s * 0.140f) * g +
               (0.072f - c * 0.072f - s * 0.283f) * b;
    float ob = (0.213f - c * 0.213f - s * 0.787f) * r +
               (0.715f - c * 0.715f + s * 0.715f) * g +
               (0.072f + c * 0.928f + s * 0.072f) * b;
    float lum = or_ * 0.213f + og * 0.715f + ob * 0.072f;
    float sat = fx[2];
    or_ = lum + (or_ - lum) * sat;
    og = lum + (og - lum) * sat;
    ob = lum + (ob - lum) * sat;
    float light = fx[3];
    rgb[0] = fminf(fmaxf(or_ * light, 0.0f), 1.0f);
    rgb[1] = fminf(fmaxf(og * light, 0.0f), 1.0f);
    rgb[2] = fminf(fmaxf(ob * light, 0.0f), 1.0f);
    return;
  }
  float mx = fmaxf(rgb[0], fmaxf(rgb[1], rgb[2]));
  float mn = fminf(rgb[0], fminf(rgb[1], rgb[2]));
  float l_orig = (mx + mn) * 0.5f;
  float protect = compose_smoothstep(COMPOSE_FX_DARK_LO, COMPOSE_FX_DARK_HI, l_orig);
  float l_dyed = fminf(fmaxf(l_orig * fx[3], 0.0f), 1.0f);
  float dyed[3];
  compose_hsl_to_rgb(fx[1], fx[2], l_dyed, dyed);
  rgb[0] = rgb[0] + (dyed[0] - rgb[0]) * protect;
  rgb[1] = rgb[1] + (dyed[1] - rgb[1]) * protect;
  rgb[2] = rgb[2] + (dyed[2] - rgb[2]) * protect;
}

static float compose_get_num(JSContext *ctx, JSValueConst obj, const char *key,
                             float dflt) {
  JSValue v = JS_GetPropertyStr(ctx, obj, key);
  double d = dflt;
  if (!JS_IsUndefined(v) && !JS_IsNull(v))
    JS_ToFloat64(ctx, &d, v);
  JS_FreeValue(ctx, v);
  return (float)d;
}

// key 为长度 n 的数组时读入 out, 返回 true; 缺省/非法返回 false
static bool compose_get_vec(JSContext *ctx, JSValueConst obj, const char *key,
                            float *out, int n) {
  JSValue v = JS_GetPropertyStr(ctx, obj, key);
  if (JS_IsUndefined(v) || JS_IsNull(v)) { JS_FreeValue(ctx, v); return false; }
  bool ok = true;
  for (int i = 0; i < n; i++) {
    JSValue e = JS_GetPropertyUint32(ctx, v, i);
    double d = 0;
    if (JS_ToFloat64(ctx, &d, e) < 0) ok = false;
    JS_FreeValue(ctx, e);
    out[i] = (float)d;
  }
  JS_FreeValue(ctx, v);
  return ok;
}

static JSValue js_graphics_texture_create_staged(JSContext *ctx,
                                                 JSValueConst this_val,
                                                 int argc, JSValueConst *argv) {
  if (argc < 2)
    return JS_ThrowTypeError(ctx, "texture_create_staged(w, h)");
  int32_t w = 0, h = 0;
  JS_ToInt32(ctx, &w, argv[0]);
  JS_ToInt32(ctx, &h, argv[1]);
  if (w <= 0 || h <= 0 || w > 4096 || h > 4096)
    return JS_ThrowRangeError(ctx, "texture_create_staged: bad dims %dx%d", w, h);

  int slot = -1;
  for (int i = 0; i < MAX_GRAPHICS_TEXTURES; i++) {
    if (!g_graphics_textures[i].valid) { slot = i; break; }
  }
  if (slot < 0)
    return JS_ThrowInternalError(ctx, "texture_create_staged: texture cache full");

  uint8_t *staging = (uint8_t *)calloc((size_t)w * h, 4);
  if (!staging)
    return JS_ThrowInternalError(ctx, "texture_create_staged: out of memory");

  sg_image img = sg_make_image(&(sg_image_desc){
      .width = w,
      .height = h,
      .pixel_format = SG_PIXELFORMAT_RGBA8,
      .usage.dynamic_update = true,
  });
  if (sg_query_image_state(img) != SG_RESOURCESTATE_VALID) {
    free(staging);
    return JS_ThrowInternalError(ctx, "texture_create_staged: sg_make_image failed");
  }
  // 创建期不上传 (sokol 限每 image 每帧一次 update, 首次 flush 必先于首次采样)
  sg_view view = sg_make_view(&(sg_view_desc){ .texture.image = img });
  snprintf(g_graphics_textures[slot].path, MAX_GRAPHICS_TEXTURE_PATH_LEN,
           "__staged_%d_%dx%d__", slot, w, h);
  g_graphics_textures[slot].image = img;
  g_graphics_textures[slot].view = view;
  g_graphics_textures[slot].valid = true;
  g_graphics_textures[slot].staging = staging;
  g_graphics_textures[slot].width = w;
  g_graphics_textures[slot].height = h;
  return JS_NewInt32(ctx, slot);
}

static GraphicsTextureCache *compose_staged_tex(JSContext *ctx, JSValueConst v,
                                                JSValue *err) {
  int32_t id = 0;
  JS_ToInt32(ctx, &id, v);
  if (id < 0 || id >= MAX_GRAPHICS_TEXTURES || !g_graphics_textures[id].valid) {
    *err = JS_ThrowRangeError(ctx, "invalid texture %d", id);
    return NULL;
  }
  if (!g_graphics_textures[id].staging) {
    *err = JS_ThrowTypeError(ctx, "texture %d is not staged", id);
    return NULL;
  }
  return &g_graphics_textures[id];
}

static JSValue js_graphics_compose_layers(JSContext *ctx, JSValueConst this_val,
                                          int argc, JSValueConst *argv) {
  if (argc < 6)
    return JS_ThrowTypeError(
        ctx, "compose_layers(texId, slotX, slotY, slotW, slotH, layers[, outlineR])");
  JSValue err = JS_UNDEFINED;
  GraphicsTextureCache *tex = compose_staged_tex(ctx, argv[0], &err);
  if (!tex) return err;

  int32_t sx = 0, sy = 0, sw = 0, sh = 0, outlineR = 0;
  JS_ToInt32(ctx, &sx, argv[1]);
  JS_ToInt32(ctx, &sy, argv[2]);
  JS_ToInt32(ctx, &sw, argv[3]);
  JS_ToInt32(ctx, &sh, argv[4]);
  if (argc >= 7) JS_ToInt32(ctx, &outlineR, argv[6]);
  if (sw <= 0 || sh <= 0 || sx < 0 || sy < 0 || sx + sw > tex->width ||
      sy + sh > tex->height)
    return JS_ThrowRangeError(ctx, "compose_layers: slot %d,%d %dx%d out of %dx%d",
                              sx, sy, sw, sh, tex->width, tex->height);
  if (outlineR < 0 || outlineR > 16)
    return JS_ThrowRangeError(ctx, "compose_layers: outlineR %d out of [0,16]",
                              outlineR);

  JSValue lenV = JS_GetPropertyStr(ctx, argv[5], "length");
  int32_t layerCount = 0;
  JS_ToInt32(ctx, &layerCount, lenV);
  JS_FreeValue(ctx, lenV);
  if (layerCount < 0 || layerCount > 64)
    return JS_ThrowRangeError(ctx, "compose_layers: bad layer count %d", layerCount);

  const size_t slotPx = (size_t)sw * sh;
  float *acc = (float *)calloc(slotPx, 4 * sizeof(float)); // 槽累积 (straight alpha)
  float *lay = (float *)malloc(slotPx * 4 * sizeof(float)); // 单层工作区(槽大小上限)
  if (!acc || !lay) {
    free(acc); free(lay);
    return JS_ThrowInternalError(ctx, "compose_layers: out of memory");
  }

  for (int li = 0; li < layerCount; li++) {
    JSValue lv = JS_GetPropertyUint32(ctx, argv[5], (uint32_t)li);
    JSValue pathV = JS_GetPropertyStr(ctx, lv, "path");
    const char *path = JS_ToCString(ctx, pathV);
    JS_FreeValue(ctx, pathV);
    if (!path) { JS_FreeValue(ctx, lv); continue; }
    const ComposeSrcCache *src = compose_src_get(path);
    JS_FreeCString(ctx, path);
    if (!src) { JS_FreeValue(ctx, lv); continue; } // 解码失败已 LOG_ERROR, 跳层

    int dx = (int)compose_get_num(ctx, lv, "dx", 0);
    int dy = (int)compose_get_num(ctx, lv, "dy", 0);
    float scale = compose_get_num(ctx, lv, "scale", 1.0f);
    JSValue flipV = JS_GetPropertyStr(ctx, lv, "flip");
    bool flip = JS_ToBool(ctx, flipV) > 0;
    JS_FreeValue(ctx, flipV);
    float tint[4], fx[4];
    bool hasTint = compose_get_vec(ctx, lv, "tint", tint, 4);
    bool hasFx = compose_get_vec(ctx, lv, "fx", fx, 4) && fx[0] >= 0.5f;
    // op "erase" (plan_actor_mask_layer §2.3): 擦除层——槽内已合成像素的 alpha 乘以本层 alpha
    // (straight alpha 只动 A 不动 RGB), 层图幅外不动; 不做 fx/tint。兜帽帽口 mask 用。
    bool erase = false;
    {
      JSValue opV = JS_GetPropertyStr(ctx, lv, "op");
      if (!JS_IsUndefined(opV) && !JS_IsNull(opV)) {
        const char *opS = JS_ToCString(ctx, opV);
        if (opS) {
          if (strcmp(opS, "erase") == 0) erase = true;
          else LOG_ERROR("compose_layers: unknown op '%s' (layer %d), treated as normal", opS, li);
          JS_FreeCString(ctx, opS);
        }
      }
      JS_FreeValue(ctx, opV);
    }
    JS_FreeValue(ctx, lv);

    int lw = src->w, lh = src->h;
    if (lw > sw || lh > sh) {
      LOG_ERROR("compose_layers: layer %dx%d exceeds slot %dx%d, skipped",
                lw, lh, sw, sh);
      continue;
    }
    // 1. 取样 (scale 绕图心双线性, flip 水平镜像) → 图幅不变
    float cx = (lw - 1) * 0.5f, cy = (lh - 1) * 0.5f;
    for (int y = 0; y < lh; y++) {
      for (int x = 0; x < lw; x++) {
        int xi = flip ? (lw - 1 - x) : x;
        float *dst = lay + ((size_t)y * lw + x) * 4;
        if (scale != 1.0f) {
          float sxf = (xi - cx) / scale + cx;
          float syf = (y - cy) / scale + cy;
          if (sxf < 0 || syf < 0 || sxf > lw - 1 || syf > lh - 1) {
            dst[0] = dst[1] = dst[2] = dst[3] = 0.0f;
            continue;
          }
          int x0 = (int)sxf, y0 = (int)syf;
          int x1 = x0 + 1 < lw ? x0 + 1 : x0;
          int y1 = y0 + 1 < lh ? y0 + 1 : y0;
          float fxw = sxf - x0, fyw = syf - y0;
          for (int c = 0; c < 4; c++) {
            float p00 = src->pixels[((size_t)y0 * lw + x0) * 4 + c] / 255.0f;
            float p10 = src->pixels[((size_t)y0 * lw + x1) * 4 + c] / 255.0f;
            float p01 = src->pixels[((size_t)y1 * lw + x0) * 4 + c] / 255.0f;
            float p11 = src->pixels[((size_t)y1 * lw + x1) * 4 + c] / 255.0f;
            dst[c] = (p00 * (1 - fxw) + p10 * fxw) * (1 - fyw) +
                     (p01 * (1 - fxw) + p11 * fxw) * fyw;
          }
        } else {
          const uint8_t *sp = src->pixels + ((size_t)y * lw + xi) * 4;
          dst[0] = sp[0] / 255.0f;
          dst[1] = sp[1] / 255.0f;
          dst[2] = sp[2] / 255.0f;
          dst[3] = sp[3] / 255.0f;
        }
      }
    }
    // 1b. 擦除层: 只量化 alpha 后乘进槽, 跳过 fx/tint/alpha-over
    if (erase) {
      for (int y = 0; y < lh; y++) {
        int ty = dy + y;
        if (ty < 0 || ty >= sh) continue;
        for (int x = 0; x < lw; x++) {
          int tx = dx + x;
          if (tx < 0 || tx >= sw) continue;
          float la = lay[((size_t)y * lw + x) * 4 + 3];
          la = floorf(fminf(fmaxf(la, 0.0f), 1.0f) * 255.0f + 0.5f) / 255.0f;
          float *d = acc + ((size_t)ty * sw + tx) * 4;
          d[3] = floorf(d[3] * la * 255.0f + 0.5f) / 255.0f;
        }
      }
      continue;
    }
    // 2. fx → tint → 量化 8bit (对齐预览工具逐层 8bit 中间态)
    for (size_t p = 0; p < (size_t)lw * lh; p++) {
      float *px = lay + p * 4;
      if (px[3] <= 0.0f && !hasFx) continue;
      if (hasFx) compose_fx_apply(px, fx);
      if (hasTint) {
        px[0] *= tint[0]; px[1] *= tint[1]; px[2] *= tint[2]; px[3] *= tint[3];
      }
      for (int c = 0; c < 4; c++) {
        float q = floorf(fminf(fmaxf(px[c], 0.0f), 1.0f) * 255.0f + 0.5f);
        px[c] = q / 255.0f;
      }
    }
    // 3. alpha-over 落槽 (straight alpha)
    for (int y = 0; y < lh; y++) {
      int ty = dy + y;
      if (ty < 0 || ty >= sh) continue;
      for (int x = 0; x < lw; x++) {
        int tx = dx + x;
        if (tx < 0 || tx >= sw) continue;
        const float *s = lay + ((size_t)y * lw + x) * 4;
        float sa = s[3];
        if (sa <= 0.0f) continue;
        float *d = acc + ((size_t)ty * sw + tx) * 4;
        float da = d[3];
        float oa = sa + da * (1.0f - sa);
        if (oa > 0.0f) {
          for (int c = 0; c < 3; c++)
            d[c] = (s[c] * sa + d[c] * da * (1.0f - sa)) / oa;
        }
        d[3] = oa;
      }
    }
  }
  free(lay);

  // 4. 描边: 整体 alpha 半径 R 圆盘膨胀, 新增像素垫黑于剪影之下
  if (outlineR > 0) {
    uint8_t *mask = (uint8_t *)malloc(slotPx);
    if (mask) {
      for (size_t p = 0; p < slotPx; p++)
        mask[p] = acc[p * 4 + 3] > (1.0f / 255.0f) ? 1 : 0;
      const int r2 = outlineR * outlineR;
      for (int y = 0; y < sh; y++) {
        for (int x = 0; x < sw; x++) {
          float *d = acc + ((size_t)y * sw + x) * 4;
          if (d[3] >= 1.0f) continue;
          bool hit = mask[(size_t)y * sw + x];
          for (int oy = -outlineR; oy <= outlineR && !hit; oy++) {
            int yy = y + oy;
            if (yy < 0 || yy >= sh) continue;
            for (int ox = -outlineR; ox <= outlineR; ox++) {
              if (ox * ox + oy * oy > r2) continue;
              int xx = x + ox;
              if (xx < 0 || xx >= sw) continue;
              if (mask[(size_t)yy * sw + xx]) { hit = true; break; }
            }
          }
          if (!hit) continue;
          // 黑底在下: composed OVER black
          float da = d[3];
          for (int c = 0; c < 3; c++) d[c] = d[c] * da; // + 0 * (1-da)
          d[3] = 1.0f;
        }
      }
      free(mask);
    }
  }

  // 5. 写 staging: 槽内行序垂直翻转到 GPU 约定 (行 t = 图像行 H-1-t)
  for (int y = 0; y < sh; y++) {
    uint8_t *row = tex->staging +
                   ((size_t)(tex->height - 1 - (sy + y)) * tex->width + sx) * 4;
    const float *srow = acc + (size_t)y * sw * 4;
    for (int x = 0; x < sw; x++) {
      for (int c = 0; c < 4; c++) {
        float v = srow[x * 4 + c];
        row[x * 4 + c] =
            (uint8_t)floorf(fminf(fmaxf(v, 0.0f), 1.0f) * 255.0f + 0.5f);
      }
    }
  }
  free(acc);
  return JS_UNDEFINED;
}

static JSValue js_graphics_texture_flush_staged(JSContext *ctx,
                                                JSValueConst this_val, int argc,
                                                JSValueConst *argv) {
  if (argc < 1)
    return JS_ThrowTypeError(ctx, "texture_flush_staged(texId)");
  JSValue err = JS_UNDEFINED;
  GraphicsTextureCache *tex = compose_staged_tex(ctx, argv[0], &err);
  if (!tex) return err;
  sg_update_image(tex->image, &(sg_image_data){
      .mip_levels[0] = { .ptr = tex->staging,
                         .size = (size_t)tex->width * tex->height * 4 },
  });
  return JS_UNDEFINED;
}

static void compose_ab_free(JSRuntime *rt, void *opaque, void *ptr) {
  (void)opaque;
  js_free_rt(rt, ptr);
}

static JSValue js_graphics_texture_staged_read(JSContext *ctx,
                                               JSValueConst this_val, int argc,
                                               JSValueConst *argv) {
  if (argc < 5)
    return JS_ThrowTypeError(ctx, "texture_staged_read(texId, x, y, w, h)");
  JSValue err = JS_UNDEFINED;
  GraphicsTextureCache *tex = compose_staged_tex(ctx, argv[0], &err);
  if (!tex) return err;
  int32_t x = 0, y = 0, w = 0, h = 0;
  JS_ToInt32(ctx, &x, argv[1]);
  JS_ToInt32(ctx, &y, argv[2]);
  JS_ToInt32(ctx, &w, argv[3]);
  JS_ToInt32(ctx, &h, argv[4]);
  if (w <= 0 || h <= 0 || x < 0 || y < 0 || x + w > tex->width ||
      y + h > tex->height)
    return JS_ThrowRangeError(ctx, "texture_staged_read: bad region");
  uint8_t *out = (uint8_t *)js_malloc(ctx, (size_t)w * h * 4);
  if (!out)
    return JS_ThrowInternalError(ctx, "texture_staged_read: out of memory");
  for (int r = 0; r < h; r++) {
    // 图像空间读回: 结果行 r = staging 行 H-1-(y+r)
    memcpy(out + (size_t)r * w * 4,
           tex->staging +
               ((size_t)(tex->height - 1 - (y + r)) * tex->width + x) * 4,
           (size_t)w * 4);
  }
  JSValue ab = JS_NewArrayBuffer(ctx, out, (size_t)w * h * 4, compose_ab_free,
                                 NULL, false);
  JSValue global = JS_GetGlobalObject(ctx);
  JSValue u8ctor = JS_GetPropertyStr(ctx, global, "Uint8Array");
  JSValue result = JS_CallConstructor(ctx, u8ctor, 1, &ab);
  JS_FreeValue(ctx, u8ctor);
  JS_FreeValue(ctx, global);
  JS_FreeValue(ctx, ab);
  return result;
}

// JS Binding: graphics.load_texture(path) -> textureId
static JSValue js_graphics_load_texture(JSContext *ctx, JSValueConst this_val,
                                        int argc, JSValueConst *argv) {
  if (argc < 1)
    return JS_ThrowTypeError(ctx, "path required");
  const char *path = JS_ToCString(ctx, argv[0]);
  if (!path)
    return JS_EXCEPTION;

  int id = get_or_load_graphics_texture(path);
  JS_FreeCString(ctx, path);
  return JS_NewInt32(ctx, id);
}

// ============================================================================
// graphics.draw_mesh(meshId, x, y, z, qx, qy, qz, qw, textureId, layer)
//
// 对齐: Reference Graphics.DrawMesh(mesh, loc, quat, material, layer)
// ============================================================================

// ============================================================================
// graphics.draw_mesh(meshId, x, y, z, qx, qy, qz, qw, sx, sy, sz, textureId,
// layer)
// ============================================================================

static JSValue js_graphics_draw_mesh(JSContext *ctx, JSValueConst this_val,
                                     int argc, JSValueConst *argv) {
  ensure_initialized();

  if (argc < 13) {
    return JS_ThrowTypeError(
        ctx, "graphics.draw_mesh requires 13 arguments (meshId, x, y, z, qx, "
             "qy, qz, qw, sx, sy, sz, tex, layer, [props])");
  }

  int32_t meshId, textureId, layer;
  double x, y, z, qx, qy, qz, qw, sx, sy, sz;

  if (JS_ToInt32(ctx, &meshId, argv[0]) < 0)
    return JS_EXCEPTION;
  if (JS_ToFloat64(ctx, &x, argv[1]) < 0)
    return JS_EXCEPTION;
  if (JS_ToFloat64(ctx, &y, argv[2]) < 0)
    return JS_EXCEPTION;
  if (JS_ToFloat64(ctx, &z, argv[3]) < 0)
    return JS_EXCEPTION;
  if (JS_ToFloat64(ctx, &qx, argv[4]) < 0)
    return JS_EXCEPTION;
  if (JS_ToFloat64(ctx, &qy, argv[5]) < 0)
    return JS_EXCEPTION;
  if (JS_ToFloat64(ctx, &qz, argv[6]) < 0)
    return JS_EXCEPTION;
  if (JS_ToFloat64(ctx, &qw, argv[7]) < 0)
    return JS_EXCEPTION;
  if (JS_ToFloat64(ctx, &sx, argv[8]) < 0)
    return JS_EXCEPTION;
  if (JS_ToFloat64(ctx, &sy, argv[9]) < 0)
    return JS_EXCEPTION;
  if (JS_ToFloat64(ctx, &sz, argv[10]) < 0)
    return JS_EXCEPTION;
  if (JS_ToInt32(ctx, &textureId, argv[11]) < 0)
    return JS_EXCEPTION;
  if (JS_ToInt32(ctx, &layer, argv[12]) < 0)
    return JS_EXCEPTION;

  if (meshId < 0 || meshId >= MAX_DYNAMIC_MESHES) {
    return JS_UNDEFINED;
  }

  DynamicMesh *dm = &g_dynamic_meshes[meshId];
  if (!dm->valid) {
    return JS_UNDEFINED;
  }

  // 构建 TRS 变换矩阵
  Matrix4 transform;
  Matrix4 rotate;
  Matrix4 scale;

  // 1. Rotation
  quat_to_matrix4((float)qx, (float)qy, (float)qz, (float)qw, rotate);

  // 2. Scale
  // Identity with diagonal scaling
  memset(scale, 0, sizeof(Matrix4));
  scale[0] = (float)sx;
  scale[5] = (float)sy;
  scale[10] = (float)sz;
  scale[15] = 1.0f;

  // 3. Combine: M = T * R * S
  // Note: matrix4_mul(A, B, C) -> C = A * B
  // Sokol matrix layout is usually column-major?
  // Our matrix4_mul implementation in helpers/math utils?
  // Assuming standard OpenGL: M = Translate * Rotate * Scale

  Matrix4 rs;
  matrix4_mul(rotate, scale, rs);

  // COORDINATE SYSTEM FIX (Phase 7):
  // [Changed] Removal of Legacy Permutation.
  // We now trust the Input Coordinates strictly as World Units.
  // Transform:
  //   X -> World X (East)
  //   Y -> World Y (Altitude)
  //   Z -> World Z (North)

  memcpy(transform, rs, sizeof(Matrix4));
  transform[12] = (float)x; // World X
  transform[13] = (float)y; // World Y (Altitude)
  transform[14] = (float)z; // World Z (North)

  // Apply Texture - 直接设置 texture 和 view (不要创建新 view)
  // 注意: 多个 mesh request 共享同一个 DynamicMesh 时，使用
  // material_set_texture 会导致 view 被反复销毁/创建——此后 submit
  // 拿到的旧 Material 复制里的 view handle 会指向已销毁的资源。
  // textureId >= 10000: atlas 纹理池 (ATLAS_TEX_ID_OFFSET, 同 draw_mesh_instanced)
  extern bool atlas_texture_get(int texture_id, sg_image *out_image,
                                sg_view *out_view);
  sg_image atlas_img;
  sg_view atlas_view;
  if (atlas_texture_get(textureId, &atlas_img, &atlas_view)) {
    dm->material.texture = atlas_img;
    dm->material.texture_view = atlas_view;
  } else if (textureId >= 0 && textureId < MAX_GRAPHICS_TEXTURES &&
             g_graphics_textures[textureId].valid) {
    // 直接设置，避免创建/销毁 view
    dm->material.texture = g_graphics_textures[textureId].image;
    dm->material.texture_view = g_graphics_textures[textureId].view;
  }

  // Parse MaterialBlock properties
  MaterialBlock block;
  memset(&block, 0, sizeof(MaterialBlock));
  MaterialBlock *pBlock = NULL;

  if (argc > 13 && !JS_IsUndefined(argv[13]) && !JS_IsNull(argv[13])) {
    JSValue props = argv[13];

    // color: [r, g, b, a]
    JSValue colorVal = JS_GetPropertyStr(ctx, props, "color");
    if (!JS_IsUndefined(colorVal)) {
      double r, g, b, a;
      JS_ToFloat64(ctx, &r, JS_GetPropertyUint32(ctx, colorVal, 0));
      JS_ToFloat64(ctx, &g, JS_GetPropertyUint32(ctx, colorVal, 1));
      JS_ToFloat64(ctx, &b, JS_GetPropertyUint32(ctx, colorVal, 2));
      JS_ToFloat64(ctx, &a, JS_GetPropertyUint32(ctx, colorVal, 3));
      block.color[0] = (float)r;
      block.color[1] = (float)g;
      block.color[2] = (float)b;
      block.color[3] = (float)a;
      block.use_color = true;
      JS_FreeValue(ctx, colorVal);
    }

    // colorTwo: [r, g, b, a]
    JSValue colorTwoVal = JS_GetPropertyStr(ctx, props, "colorTwo");
    if (!JS_IsUndefined(colorTwoVal)) {
      double r, g, b, a;
      JS_ToFloat64(ctx, &r, JS_GetPropertyUint32(ctx, colorTwoVal, 0));
      JS_ToFloat64(ctx, &g, JS_GetPropertyUint32(ctx, colorTwoVal, 1));
      JS_ToFloat64(ctx, &b, JS_GetPropertyUint32(ctx, colorTwoVal, 2));
      JS_ToFloat64(ctx, &a, JS_GetPropertyUint32(ctx, colorTwoVal, 3));
      block.color_two[0] = (float)r;
      block.color_two[1] = (float)g;
      block.color_two[2] = (float)b;
      block.color_two[3] = (float)a;
      block.use_color_two = true;
      JS_FreeValue(ctx, colorTwoVal);
    }
    pBlock = &block;
  }

  // 调用 C 层绘制
  graphics_draw_mesh(&dm->mesh, transform, &dm->material, pBlock, layer);

  return JS_UNDEFINED;
}

// ============================================================================
// graphics.draw_mesh_at(meshId, x, y, z, textureId, layer)
//
// 简化版: 只有位置，无旋转 (identity quaternion)
// ============================================================================

static JSValue js_graphics_draw_mesh_at(JSContext *ctx, JSValueConst this_val,
                                        int argc, JSValueConst *argv) {
  ensure_initialized();

  if (argc < 6) {
    return JS_ThrowTypeError(ctx, "graphics.draw_mesh_at requires 6 arguments");
  }

  int32_t meshId, textureId, layer;
  double x, y, z;

  if (JS_ToInt32(ctx, &meshId, argv[0]) < 0)
    return JS_EXCEPTION;
  if (JS_ToFloat64(ctx, &x, argv[1]) < 0)
    return JS_EXCEPTION;
  if (JS_ToFloat64(ctx, &y, argv[2]) < 0)
    return JS_EXCEPTION;
  if (JS_ToFloat64(ctx, &z, argv[3]) < 0)
    return JS_EXCEPTION;
  if (JS_ToInt32(ctx, &textureId, argv[4]) < 0)
    return JS_EXCEPTION;
  if (JS_ToInt32(ctx, &layer, argv[5]) < 0)
    return JS_EXCEPTION;

  if (meshId < 0 || meshId >= MAX_DYNAMIC_MESHES) {
    return JS_UNDEFINED;
  }

  DynamicMesh *dm = &g_dynamic_meshes[meshId];
  if (!dm->valid) {
    return JS_UNDEFINED;
  }

  // 构建平移矩阵 (无旋转)
  // [Phase 7] Direct World Mapping
  // X -> X, Y -> Altitude, Z -> North
  Matrix4 transform = {1, 0, 0, 0, 0,        1,        0,        0,
                       0, 0, 1, 0, (float)x, (float)y, (float)z, 1};

  // Apply Texture - 直接设置 (同 js_graphics_draw_mesh)
  if (textureId >= 0 && textureId < MAX_GRAPHICS_TEXTURES &&
      g_graphics_textures[textureId].valid) {
    dm->material.texture = g_graphics_textures[textureId].image;
    dm->material.texture_view = g_graphics_textures[textureId].view;
  }

  graphics_draw_mesh(&dm->mesh, transform, &dm->material, NULL, layer);

  return JS_UNDEFINED;
}

// ============================================================================
// graphics.set_mesh_color(meshId, r, g, b, a)
// ============================================================================

static JSValue js_graphics_set_mesh_color(JSContext *ctx, JSValueConst this_val,
                                          int argc, JSValueConst *argv) {
  ensure_initialized();

  if (argc < 5) {
    return JS_ThrowTypeError(ctx,
                             "graphics.set_mesh_color requires 5 arguments");
  }

  int32_t meshId;
  double r, g, b, a;

  if (JS_ToInt32(ctx, &meshId, argv[0]) < 0)
    return JS_EXCEPTION;
  if (JS_ToFloat64(ctx, &r, argv[1]) < 0)
    return JS_EXCEPTION;
  if (JS_ToFloat64(ctx, &g, argv[2]) < 0)
    return JS_EXCEPTION;
  if (JS_ToFloat64(ctx, &b, argv[3]) < 0)
    return JS_EXCEPTION;
  if (JS_ToFloat64(ctx, &a, argv[4]) < 0)
    return JS_EXCEPTION;

  if (meshId < 0 || meshId >= MAX_DYNAMIC_MESHES) {
    return JS_UNDEFINED;
  }

  DynamicMesh *dm = &g_dynamic_meshes[meshId];
  if (dm->valid) {
    material_set_color(&dm->material, (float)r, (float)g, (float)b, (float)a);
  }

  return JS_UNDEFINED;
}

// ============================================================================
// graphics.stats()
// ============================================================================

static JSValue js_graphics_stats(JSContext *ctx, JSValueConst this_val,
                                 int argc, JSValueConst *argv) {
  ensure_initialized();

  int valid_count = 0;
  for (int i = 0; i < MAX_DYNAMIC_MESHES; i++) {
    if (g_dynamic_meshes[i].valid) {
      valid_count++;
    }
  }

  JSValue ret = JS_NewObject(ctx);
  JS_SetPropertyStr(ctx, ret, "valid_count", JS_NewInt32(ctx, valid_count));
  JS_SetPropertyStr(ctx, ret, "max_meshes",
                    JS_NewInt32(ctx, MAX_DYNAMIC_MESHES));
  JS_SetPropertyStr(ctx, ret, "draw_calls",
                    JS_NewInt32(ctx, graphics_get_draw_calls()));
  JS_SetPropertyStr(ctx, ret, "triangles",
                    JS_NewInt32(ctx, graphics_get_triangle_count()));

  return ret;
}

// ============================================================================
// graphics.create_shader(vs, fs) -> shaderId
// ============================================================================

#include "bedrock/gfx/generic_shader.h"
#include "bedrock/gfx/pipeline_cache.h"

static JSValue js_graphics_create_shader(JSContext *ctx, JSValueConst this_val,
                                         int argc, JSValueConst *argv) {
  if (argc < 2) {
    return JS_ThrowTypeError(
        ctx, "graphics.create_shader requires 2 arguments (vs, fs)");
  }

  const char *vs = JS_ToCString(ctx, argv[0]);
  if (!vs)
    return JS_EXCEPTION;
  const char *fs = JS_ToCString(ctx, argv[1]);
  if (!fs) {
    JS_FreeCString(ctx, vs);
    return JS_EXCEPTION;
  }

  sg_shader shd = generic_shader_create(vs, fs);

  JS_FreeCString(ctx, vs);
  JS_FreeCString(ctx, fs);

  if (shd.id == SG_INVALID_ID) {
    return JS_ThrowInternalError(ctx, "Failed to create shader");
  }

  return JS_NewInt32(ctx, (int32_t)shd.id);
}

// ============================================================================
// graphics.set_mesh_shader(meshId, shaderId)
// ============================================================================

static JSValue js_graphics_set_mesh_shader(JSContext *ctx,
                                           JSValueConst this_val, int argc,
                                           JSValueConst *argv) {
  ensure_initialized();

  if (argc < 2) {
    return JS_ThrowTypeError(ctx,
                             "graphics.set_mesh_shader requires 2 arguments");
  }

  int32_t meshId, shaderId;

  if (JS_ToInt32(ctx, &meshId, argv[0]) < 0)
    return JS_EXCEPTION;
  if (JS_ToInt32(ctx, &shaderId, argv[1]) < 0)
    return JS_EXCEPTION;

  if (meshId < 0 || meshId >= MAX_DYNAMIC_MESHES)
    return JS_UNDEFINED;
  DynamicMesh *dm = &g_dynamic_meshes[meshId];
  if (!dm->valid)
    return JS_UNDEFINED;

  sg_shader shd = {.id = (uint32_t)shaderId};
  material_set_shader(&dm->material, shd);

  return JS_UNDEFINED;
}

// ============================================================================
// graphics.set_mesh_blend(meshId, blendMode)
// ============================================================================

static JSValue js_graphics_set_mesh_blend(JSContext *ctx, JSValueConst this_val,
                                          int argc, JSValueConst *argv) {
  ensure_initialized();

  if (argc < 2) {
    return JS_ThrowTypeError(ctx,
                             "graphics.set_mesh_blend requires 2 arguments");
  }

  int32_t meshId, blendMode;

  if (JS_ToInt32(ctx, &meshId, argv[0]) < 0)
    return JS_EXCEPTION;
  if (JS_ToInt32(ctx, &blendMode, argv[1]) < 0)
    return JS_EXCEPTION;

  if (meshId < 0 || meshId >= MAX_DYNAMIC_MESHES)
    return JS_UNDEFINED;
  DynamicMesh *dm = &g_dynamic_meshes[meshId];
  if (!dm->valid)
    return JS_UNDEFINED;

  material_set_blend_mode(&dm->material, (BlendMode)blendMode);

  return JS_UNDEFINED;
}

// ============================================================================
// graphics.set_mesh_shader_type(meshId, shaderType)
// 设置 DynamicMesh 的 ShaderType (tex_index 分支)
// ============================================================================

static JSValue js_graphics_set_mesh_shader_type(JSContext *ctx,
                                                JSValueConst this_val,
                                                int argc, JSValueConst *argv) {
  ensure_initialized();

  if (argc < 2) {
    return JS_ThrowTypeError(
        ctx, "graphics.set_mesh_shader_type requires 2 arguments");
  }

  int32_t meshId, shaderType;

  if (JS_ToInt32(ctx, &meshId, argv[0]) < 0)
    return JS_EXCEPTION;
  if (JS_ToInt32(ctx, &shaderType, argv[1]) < 0)
    return JS_EXCEPTION;

  if (meshId < 0 || meshId >= MAX_DYNAMIC_MESHES)
    return JS_UNDEFINED;
  DynamicMesh *dm = &g_dynamic_meshes[meshId];
  if (!dm->valid)
    return JS_UNDEFINED;

  material_set_shader_type(&dm->material, (ShaderType)shaderType);

  return JS_UNDEFINED;
}

// ============================================================================
// graphics.set_mesh_color_two(meshId, r, g, b, a)
// ============================================================================

static JSValue js_graphics_set_mesh_color_two(JSContext *ctx,
                                              JSValueConst this_val, int argc,
                                              JSValueConst *argv) {
  ensure_initialized();

  if (argc < 5) {
    return JS_ThrowTypeError(
        ctx, "graphics.set_mesh_color_two requires 5 arguments");
  }

  int32_t meshId;
  double r, g, b, a;

  if (JS_ToInt32(ctx, &meshId, argv[0]) < 0)
    return JS_EXCEPTION;
  if (JS_ToFloat64(ctx, &r, argv[1]) < 0)
    return JS_EXCEPTION;
  if (JS_ToFloat64(ctx, &g, argv[2]) < 0)
    return JS_EXCEPTION;
  if (JS_ToFloat64(ctx, &b, argv[3]) < 0)
    return JS_EXCEPTION;
  if (JS_ToFloat64(ctx, &a, argv[4]) < 0)
    return JS_EXCEPTION;

  if (meshId < 0 || meshId >= MAX_DYNAMIC_MESHES)
    return JS_UNDEFINED;
  DynamicMesh *dm = &g_dynamic_meshes[meshId];
  if (dm->valid) {
    dm->material.color_two[0] = (float)r;
    dm->material.color_two[1] = (float)g;
    dm->material.color_two[2] = (float)b;
    dm->material.color_two[3] = (float)a;
  }
  return JS_UNDEFINED;
}

// ============================================================================
// graphics.set_mesh_mask_texture(meshId, textureId)
// ============================================================================

static JSValue js_graphics_set_mesh_mask_texture(JSContext *ctx,
                                                 JSValueConst this_val,
                                                 int argc, JSValueConst *argv) {
  ensure_initialized();
  if (argc < 2)
    return JS_ThrowTypeError(ctx, "requires 2 args");

  int32_t meshId, textureId;
  if (JS_ToInt32(ctx, &meshId, argv[0]) < 0)
    return JS_EXCEPTION;
  if (JS_ToInt32(ctx, &textureId, argv[1]) < 0)
    return JS_EXCEPTION;

  if (meshId < 0 || meshId >= MAX_DYNAMIC_MESHES)
    return JS_UNDEFINED;
  DynamicMesh *dm = &g_dynamic_meshes[meshId];
  if (!dm->valid)
    return JS_UNDEFINED;

  if (textureId >= 0 && textureId < MAX_GRAPHICS_TEXTURES &&
      g_graphics_textures[textureId].valid) {
    dm->material.mask_texture = g_graphics_textures[textureId].image;
  } else {
    dm->material.mask_texture.id = SG_INVALID_ID;
  }
  return JS_UNDEFINED;
}

// ============================================================================
// graphics.set_mesh_param(meshId, index, value)
// ============================================================================

static JSValue js_graphics_set_mesh_param(JSContext *ctx, JSValueConst this_val,
                                          int argc, JSValueConst *argv) {
  ensure_initialized();
  if (argc < 3)
    return JS_ThrowTypeError(ctx, "requires 3 args");

  int32_t meshId, index;
  double val;
  if (JS_ToInt32(ctx, &meshId, argv[0]) < 0)
    return JS_EXCEPTION;
  if (JS_ToInt32(ctx, &index, argv[1]) < 0)
    return JS_EXCEPTION;
  if (JS_ToFloat64(ctx, &val, argv[2]) < 0)
    return JS_EXCEPTION;

  if (meshId < 0 || meshId >= MAX_DYNAMIC_MESHES)
    return JS_UNDEFINED;
  DynamicMesh *dm = &g_dynamic_meshes[meshId];
  if (!dm->valid)
    return JS_UNDEFINED;

  if (index >= 0 && index < 4) {
    dm->material.params[index] = (float)val;
  }
  return JS_UNDEFINED;
}

// ============================================================================
// graphics.set_mesh_params(meshId, p0, p1, p2, p3)
// ============================================================================

static JSValue js_graphics_set_mesh_params(JSContext *ctx,
                                           JSValueConst this_val, int argc,
                                           JSValueConst *argv) {
  ensure_initialized();
  if (argc < 5)
    return JS_ThrowTypeError(ctx, "requires 5 args");

  int32_t meshId;
  double p[4];
  if (JS_ToInt32(ctx, &meshId, argv[0]) < 0)
    return JS_EXCEPTION;
  for (int i = 0; i < 4; i++) {
    if (JS_ToFloat64(ctx, &p[i], argv[1 + i]) < 0)
      return JS_EXCEPTION;
  }

  if (meshId < 0 || meshId >= MAX_DYNAMIC_MESHES)
    return JS_UNDEFINED;
  DynamicMesh *dm = &g_dynamic_meshes[meshId];
  if (!dm->valid)
    return JS_UNDEFINED;

  for (int i = 0; i < 4; i++) {
    dm->material.params[i] = (float)p[i];
  }
  return JS_UNDEFINED;
}

// ============================================================================
// graphics.draw_mesh_instanced(meshId, textureId, layer, transforms,
// colors, count[, uvRects])
// uvRects: Float32Array 每实例 [u0,v0,u1,v1] 采样子矩形; 省略 = 全幅 (旧行为)
// ============================================================================

static JSValue js_graphics_draw_mesh_instanced(JSContext *ctx,
                                               JSValueConst this_val, int argc,
                                               JSValueConst *argv) {
  ensure_initialized();

  if (argc < 6) {
    return JS_ThrowTypeError(
        ctx, "graphics.draw_mesh_instanced requires 6 arguments");
  }

  int32_t meshId, textureId, layer, count;

  if (JS_ToInt32(ctx, &meshId, argv[0]) < 0)
    return JS_EXCEPTION;
  if (JS_ToInt32(ctx, &textureId, argv[1]) < 0)
    return JS_EXCEPTION;
  if (JS_ToInt32(ctx, &layer, argv[2]) < 0)
    return JS_EXCEPTION;

  // argv[3] = transforms (Float32Array)
  // argv[4] = colors (Float32Array or null)

  if (JS_ToInt32(ctx, &count, argv[5]) < 0)
    return JS_EXCEPTION;

  if (meshId < 0 || meshId >= MAX_DYNAMIC_MESHES)
    return JS_UNDEFINED;
  DynamicMesh *dm = &g_dynamic_meshes[meshId];
  if (!dm->valid)
    return JS_UNDEFINED;

  // Verify TypedArrays
  size_t trans_offset, trans_len;
  JSValue trans_buf =
      JS_GetTypedArrayBuffer(ctx, argv[3], &trans_offset, &trans_len, NULL);
  if (JS_IsException(trans_buf))
    return JS_ThrowTypeError(ctx, "transforms must be Float32Array");

  // Matrix4 = 16 floats = 64 bytes
  if (trans_len < (size_t)(count * 64)) {
    JS_FreeValue(ctx, trans_buf);
    return JS_ThrowRangeError(ctx, "transforms array too small");
  }

  uint8_t *trans_ptr = JS_GetArrayBuffer(ctx, &trans_len, trans_buf);
  if (!trans_ptr) {
    JS_FreeValue(ctx, trans_buf);
    return JS_EXCEPTION;
  }

  const Matrix4 *transforms = (const Matrix4 *)(trans_ptr + trans_offset);

  // Optional Colors
  const Vec4 *colors = NULL;
  JSValue col_buf = JS_UNDEFINED;

  if (!JS_IsNull(argv[4]) && !JS_IsUndefined(argv[4])) {
    size_t col_offset, col_len;
    col_buf = JS_GetTypedArrayBuffer(ctx, argv[4], &col_offset, &col_len, NULL);
    if (JS_IsException(col_buf)) {
      JS_FreeValue(ctx, trans_buf);
      return JS_ThrowTypeError(ctx, "colors must be Float32Array or null");
    }

    // Vec4 = 4 floats = 16 bytes
    if (col_len < (size_t)(count * 16)) {
      JS_FreeValue(ctx, trans_buf);
      JS_FreeValue(ctx, col_buf);
      return JS_ThrowRangeError(ctx, "colors array too small");
    }

    uint8_t *col_ptr = JS_GetArrayBuffer(ctx, &col_len, col_buf);
    if (col_ptr) {
      colors = (const Vec4 *)(col_ptr + col_offset);
    }
  }

  // Optional per-instance uv rects (argv[6])
  const Vec4 *uv_rects = NULL;
  JSValue uv_buf = JS_UNDEFINED;

  if (argc >= 7 && !JS_IsNull(argv[6]) && !JS_IsUndefined(argv[6])) {
    size_t uv_offset, uv_len;
    uv_buf = JS_GetTypedArrayBuffer(ctx, argv[6], &uv_offset, &uv_len, NULL);
    if (JS_IsException(uv_buf)) {
      JS_FreeValue(ctx, trans_buf);
      if (!JS_IsUndefined(col_buf)) JS_FreeValue(ctx, col_buf);
      return JS_ThrowTypeError(ctx, "uvRects must be Float32Array or null");
    }
    if (uv_len < (size_t)(count * 16)) {
      JS_FreeValue(ctx, trans_buf);
      if (!JS_IsUndefined(col_buf)) JS_FreeValue(ctx, col_buf);
      JS_FreeValue(ctx, uv_buf);
      return JS_ThrowRangeError(ctx, "uvRects array too small");
    }
    uint8_t *uv_ptr = JS_GetArrayBuffer(ctx, &uv_len, uv_buf);
    if (uv_ptr) {
      uv_rects = (const Vec4 *)(uv_ptr + uv_offset);
    }
  }

  // Material setup (Reuse dm->material but update texture)
  // textureId >= 10000: atlas 纹理池 (ATLAS_TEX_ID_OFFSET)
  // textureId 0-255: graphics.load_texture 纹理池
  // atlas_texture_get() 由 unified_mesh.c 提供

  sg_image old_tex = dm->material.texture;
  sg_view old_view = dm->material.texture_view;

  extern bool atlas_texture_get(int texture_id, sg_image *out_image,
                                sg_view *out_view);
  sg_image tex_img;
  sg_view tex_view;

  if (atlas_texture_get(textureId, &tex_img, &tex_view)) {
    // atlas 纹理池 (Fleck 粒子等使用 atlas page textureId)
    dm->material.texture = tex_img;
    dm->material.texture_view = tex_view;
  } else if (textureId >= 0 && textureId < MAX_GRAPHICS_TEXTURES &&
             g_graphics_textures[textureId].valid) {
    // load_texture 纹理池
    dm->material.texture = g_graphics_textures[textureId].image;
    dm->material.texture_view = g_graphics_textures[textureId].view;
  }

  graphics_draw_mesh_instanced(&dm->mesh, &dm->material, transforms, colors,
                               uv_rects, count, layer);

  // Restore material (optional, but good practice)
  dm->material.texture = old_tex;
  dm->material.texture_view = old_view;

  JS_FreeValue(ctx, trans_buf);
  if (!JS_IsUndefined(col_buf)) {
    JS_FreeValue(ctx, col_buf);
  }
  if (!JS_IsUndefined(uv_buf)) {
    JS_FreeValue(ctx, uv_buf);
  }

  return JS_UNDEFINED;
}

// ============================================================================
// 模块初始化
// ============================================================================

int js_init_graphics_module(JSContext *ctx) {
  JSValue global = JS_GetGlobalObject(ctx);
  JSValue obj = JS_NewObject(ctx);

  JS_SetPropertyStr(ctx, obj, "create_quad_mesh",
                    JS_NewCFunction(ctx, js_graphics_create_quad_mesh,
                                    "create_quad_mesh", 0));
  JS_SetPropertyStr(ctx, obj, "create_mesh",
                    JS_NewCFunction(ctx, js_graphics_create_mesh,
                                    "create_mesh", 5));
  JS_SetPropertyStr(ctx, obj, "create_quad_mesh_uv",
                    JS_NewCFunction(ctx, js_graphics_create_quad_mesh_uv,
                                    "create_quad_mesh_uv", 4));
  JS_SetPropertyStr(
      ctx, obj, "free_mesh",
      JS_NewCFunction(ctx, js_graphics_free_mesh, "free_mesh", 1));
  JS_SetPropertyStr(
      ctx, obj, "draw_mesh",
      JS_NewCFunction(ctx, js_graphics_draw_mesh, "draw_mesh", 13));
  JS_SetPropertyStr(
      ctx, obj, "texture_from_pixels",
      JS_NewCFunction(ctx, js_graphics_texture_from_pixels, "texture_from_pixels", 3));
  JS_SetPropertyStr(
      ctx, obj, "texture_update",
      JS_NewCFunction(ctx, js_graphics_texture_update, "texture_update", 2));
  JS_SetPropertyStr(ctx, obj, "texture_create_staged",
                    JS_NewCFunction(ctx, js_graphics_texture_create_staged,
                                    "texture_create_staged", 2));
  JS_SetPropertyStr(ctx, obj, "compose_layers",
                    JS_NewCFunction(ctx, js_graphics_compose_layers,
                                    "compose_layers", 7));
  JS_SetPropertyStr(ctx, obj, "texture_flush_staged",
                    JS_NewCFunction(ctx, js_graphics_texture_flush_staged,
                                    "texture_flush_staged", 1));
  JS_SetPropertyStr(ctx, obj, "texture_staged_read",
                    JS_NewCFunction(ctx, js_graphics_texture_staged_read,
                                    "texture_staged_read", 5));
  JS_SetPropertyStr(
      ctx, obj, "draw_mesh_at",
      JS_NewCFunction(ctx, js_graphics_draw_mesh_at, "draw_mesh_at", 6));
  JS_SetPropertyStr(
      ctx, obj, "set_mesh_color",
      JS_NewCFunction(ctx, js_graphics_set_mesh_color, "set_mesh_color", 5));
  JS_SetPropertyStr(ctx, obj, "stats",
                    JS_NewCFunction(ctx, js_graphics_stats, "stats", 0));

  // New Bindings
  JS_SetPropertyStr(
      ctx, obj, "create_shader",
      JS_NewCFunction(ctx, js_graphics_create_shader, "create_shader", 2));
  JS_SetPropertyStr(
      ctx, obj, "set_mesh_shader",
      JS_NewCFunction(ctx, js_graphics_set_mesh_shader, "set_mesh_shader", 2));
  JS_SetPropertyStr(
      ctx, obj, "set_mesh_blend",
      JS_NewCFunction(ctx, js_graphics_set_mesh_blend, "set_mesh_blend", 2));
  JS_SetPropertyStr(
      ctx, obj, "set_mesh_shader_type",
      JS_NewCFunction(ctx, js_graphics_set_mesh_shader_type,
                      "set_mesh_shader_type", 2));
  JS_SetPropertyStr(ctx, obj, "set_mesh_color_two",
                    JS_NewCFunction(ctx, js_graphics_set_mesh_color_two,
                                    "set_mesh_color_two", 5));
  JS_SetPropertyStr(ctx, obj, "set_mesh_mask_texture",
                    JS_NewCFunction(ctx, js_graphics_set_mesh_mask_texture,
                                    "set_mesh_mask_texture", 2));
  JS_SetPropertyStr(
      ctx, obj, "set_mesh_param",
      JS_NewCFunction(ctx, js_graphics_set_mesh_param, "set_mesh_param", 3));
  JS_SetPropertyStr(
      ctx, obj, "set_mesh_params",
      JS_NewCFunction(ctx, js_graphics_set_mesh_params, "set_mesh_params", 5));
  JS_SetPropertyStr(
      ctx, obj, "load_texture",
      JS_NewCFunction(ctx, js_graphics_load_texture, "load_texture", 1));
  JS_SetPropertyStr(ctx, obj, "draw_mesh_instanced",
                    JS_NewCFunction(ctx, js_graphics_draw_mesh_instanced,
                                    "draw_mesh_instanced", 6));

  // 渲染队列带注册表(material.h RENDER_QUEUE_BANDS)暴露给 JS——单一事实源,
  // JS 侧禁止抄写数字。graphics.RQ.GEOMETRY / graphics.RQ.GEOMETRY_END 等。
  {
    JSValue rq = JS_NewObject(ctx);
#define X(name, start, end, desc)                                              \
  JS_SetPropertyStr(ctx, rq, #name + 3, JS_NewInt32(ctx, (start)));           \
  JS_SetPropertyStr(ctx, rq, #name "_END" + 3, JS_NewInt32(ctx, (end)));
    RENDER_QUEUE_BANDS(X)
#undef X
    JS_SetPropertyStr(ctx, obj, "RQ", rq);
  }

  JS_SetPropertyStr(ctx, global, "graphics", obj);
  JS_FreeValue(ctx, global);

  LOG_VERBOSE("[graphics_bindings] module loaded\n");
  return 0;
}
