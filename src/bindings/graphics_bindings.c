// graphics_bindings.c - Graphics API JS 绑定
//
// 对齐 Reference: Graphics.DrawMesh(mesh, loc, quat, material, layer)
//
// 用于动态渲染 (RealtimeOnly/MapMeshAndRealTime):
//   - Pawn 动态绘制
//   - Door 开关动画
//   - 其他需要每帧更新位置/旋转的 Thing
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
// 用于动态渲染 Thing
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
  material_set_render_queue(&dm->material, 3000);
  // 使用 TEXTURED 模式 (tex_index=0)，因为 Graphics.drawMesh
  // 通常用于带纹理的渲染
  material_set_shader_type(&dm->material, SHADER_TYPE_TEXTURED);

  // 上传到 GPU (使用 Material 的 shader_type)
  mesh_upload_to_gpu_with_material(&dm->mesh, &dm->material);

  dm->valid = true;

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
  material_set_render_queue(&dm->material, 3000);
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
  // 注意: 由于 3 个 pawn 共享 DynamicMesh，使用 material_set_texture 会导致
  // view 被反复销毁/创建，当 submit 时之前复制的 Material 中的 view handle
  if (textureId >= 0 && textureId < MAX_GRAPHICS_TEXTURES &&
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
// graphics.draw_mesh_instanced(meshId, textureId, layer, transforms,
// colors, count)
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
                               count, layer);

  // Restore material (optional, but good practice)
  dm->material.texture = old_tex;
  dm->material.texture_view = old_view;

  JS_FreeValue(ctx, trans_buf);
  if (!JS_IsUndefined(col_buf)) {
    JS_FreeValue(ctx, col_buf);
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
      ctx, obj, "load_texture",
      JS_NewCFunction(ctx, js_graphics_load_texture, "load_texture", 1));
  JS_SetPropertyStr(ctx, obj, "draw_mesh_instanced",
                    JS_NewCFunction(ctx, js_graphics_draw_mesh_instanced,
                                    "draw_mesh_instanced", 6));

  JS_SetPropertyStr(ctx, global, "graphics", obj);
  JS_FreeValue(ctx, global);

  LOG_VERBOSE("[graphics_bindings] module loaded\n");
  return 0;
}
