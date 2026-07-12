
#ifndef BALD_GFX_GRAPHICS_H
#define BALD_GFX_GRAPHICS_H

#include "camera.h"
#include "material.h"
#include "mesh.h"
#include <stdbool.h>

typedef struct MaterialBlock MaterialBlock;

void graphics_draw_mesh(Mesh *mesh, const Matrix4 transform, Material *material,
                        MaterialBlock *block, int layer);

void graphics_draw_mesh_identity(Mesh *mesh, Material *material, int layer);

// uv_rects: 每实例 [u0,v0,u1,v1] 采样子矩形 (NULL = 全幅 [0,0,1,1], 与旧行为一致)
void graphics_draw_mesh_instanced(Mesh *mesh, Material *material,
                                  const Matrix4 *transforms, const Vec4 *colors,
                                  const Vec4 *uv_rects, int count, int layer);

void graphics_init(void);

void graphics_shutdown(void);

void graphics_frame_begin(void);

void graphics_frame_end(void);

void graphics_set_camera(Camera *cam);

Camera *graphics_get_camera(void);

void graphics_submit_meshes(void);

void graphics_submit_meshes_range(int rq_min, int rq_max, bool clear_after,
                                  const Vec4 debug_color);

int graphics_submit_meshes_range_count(int rq_min, int rq_max, bool clear_after,
                                       const Vec4 debug_color);

void graphics_submit_instanced(void);

int graphics_submit_instanced_count(void);

// 分带冲刷: 只提交有效 rq (material.renderQueue+layer) ∈ [rq_min, rq_max) 的
// instanced 请求并标记消费, 队列计数不清 (帧末全量 submit 兜底剩余请求)
int graphics_submit_instanced_range_count(int rq_min, int rq_max);

int graphics_get_draw_calls(void);
int graphics_get_triangle_count(void);

extern const Matrix4 MATRIX4_IDENTITY_CONST;

#endif
