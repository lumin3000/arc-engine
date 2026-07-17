
#include <stdlib.h>
#include "graphics.h"
#include "../../../external/sokol/c/sokol_gfx.h"
#include "../../../external/sokol/c/sokol_glue.h"
#include "../utils/utils.h"
#include "generated_shader.h"
#include "render.h"
#include "render_diagnostics.h"
#include "../engine_asset.h"
#include <stdio.h>
#include <string.h>


const Matrix4 MATRIX4_IDENTITY_CONST = {1, 0, 0, 0, 0, 1, 0, 0,
                                        0, 0, 1, 0, 0, 0, 0, 1};

#define MAX_MESH_QUEUE 16384

int g_graphics_frame_counter = 0;

typedef struct {
  Mesh *mesh;
  Material material;
  MaterialBlock block;
  bool has_block;
  Matrix4 transform;
  Matrix4 vp_matrix;
  int layer;
  bool valid;
  int queue_order;
} MeshDrawRequest;

// 每实例 24 floats: Matrix4(16) + color(4) + uv_rect(4), 与 render.c
// instance buffer stride / vs_inst 顶点布局同步
#define INSTANCE_STRIDE_BYTES (sizeof(Matrix4) + sizeof(Vec4) + sizeof(Vec4))
#define MAX_INSTANCE_BATCH 8192

typedef struct {
  Mesh *mesh;
  Material material;
  int pack_offset; // 帧内打包池字节偏移 (入队即拷贝 — 调用方 buffer 可复用,
                   // 禁止存 JS 侧裸指针: 帧末异线程提交时已是悬空/复写数据)
  Matrix4 vp_matrix;
  int count;
  int layer;
  bool valid;
} InstancedDrawRequest;

static struct {
  bool initialized;
  Camera *current_camera;

  MeshDrawRequest mesh_queue[MAX_MESH_QUEUE];
  int mesh_queue_count;

  int draw_calls_this_frame;
  int triangles_this_frame;

// 1024: 排序实例流按纹理 run 切分后请求数随交错换页数走 (非固定页数),
// 128 时 300+ run 被静默丢弃 (排序尾段整批不画)
#define MAX_INSTANCED_QUEUE 1024
  InstancedDrawRequest instanced_queue[MAX_INSTANCED_QUEUE];
  int instanced_queue_count;

  // 实例数据帧内打包池 (总量与 GPU instance buffer 同为 8192 实例)
  uint8_t inst_pack[MAX_INSTANCE_BATCH * INSTANCE_STRIDE_BYTES];
  int inst_pack_used;
} g_graphics = {0};

void graphics_init(void) {
  if (g_graphics.initialized)
    return;

  memset(&g_graphics, 0, sizeof(g_graphics));

  g_graphics.current_camera = camera_get_main();

  g_graphics.initialized = true;

  fprintf(stderr,
          "[graphics] Initialized (GPU MVP mode, reusing existing pipeline)\n");
}

void graphics_shutdown(void) {
  if (!g_graphics.initialized)
    return;

  memset(&g_graphics, 0, sizeof(g_graphics));
}

void graphics_frame_begin(void) {
  g_graphics_frame_counter++;
  g_graphics.draw_calls_this_frame = 0;
  g_graphics.triangles_this_frame = 0;
  g_graphics.mesh_queue_count = 0;
  g_graphics.inst_pack_used = 0;

  for (int i = 0; i < MAX_MESH_QUEUE; i++) {
    g_graphics.mesh_queue[i].valid = false;
  }

  for (int i = 0; i < MAX_INSTANCED_QUEUE; i++) {
    g_graphics.instanced_queue[i].valid = false;
  }
  g_graphics.instanced_queue_count = 0;

  render_diag_begin_frame();
}

void graphics_frame_end(void) {

  render_diag_end_frame();
}

void graphics_set_camera(Camera *cam) {
  g_graphics.current_camera = cam ? cam : camera_get_main();
}

Camera *graphics_get_camera(void) { return g_graphics.current_camera; }

void graphics_draw_mesh(Mesh *mesh, const Matrix4 transform, Material *material,
                        MaterialBlock *block, int layer) {
  if (!mesh || mesh_is_empty(mesh)) {
    return;
  }
  if (!material || !material_is_valid(material)) {
    return;
  }
  if (!g_graphics.initialized) {
    return;
  }

  if (mesh->dirty) {
    mesh_upload_to_gpu_with_material(mesh, material);
  }

  if (!mesh->gpu_valid) {
    return;
  }

  if (g_graphics.mesh_queue_count >= MAX_MESH_QUEUE) {
    static int s_overflow_log = 0;
    if (s_overflow_log < 5) {
      s_overflow_log++;
      fprintf(stderr, "[graphics] QUEUE OVERFLOW! count=%d max=%d\n",
              g_graphics.mesh_queue_count, MAX_MESH_QUEUE);
    }
    return;
  }

  extern Draw_Frame draw_frame;
  Matrix4 view, vp;
  matrix4_inverse(draw_frame.coord_space.camera, view);
  matrix4_mul(draw_frame.coord_space.proj, view, vp);

  int queue_idx = g_graphics.mesh_queue_count++;
  MeshDrawRequest *req = &g_graphics.mesh_queue[queue_idx];
  req->queue_order = queue_idx;
  req->mesh = mesh;

  memcpy(&req->material, material, sizeof(Material));
  if (block) {
    memcpy(&req->block, block, sizeof(MaterialBlock));
    req->has_block = true;
  } else {
    req->has_block = false;
  }
  memcpy(req->transform, transform, sizeof(Matrix4));
  memcpy(req->vp_matrix, vp, sizeof(Matrix4));
  req->layer = layer;
  req->valid = true;

  // layer 偏移不得把队列号顶出注册带（渲染契约门 2）
  material_set_render_queue(&req->material, req->material.renderQueue + layer);

  {

    float wx = transform[12], wy = transform[13], wz = transform[14];
    float clip_x = vp[0] * wx + vp[4] * wy + vp[8] * wz + vp[12];
    float clip_y = vp[1] * wx + vp[5] * wy + vp[9] * wz + vp[13];
    float clip_z = vp[2] * wx + vp[6] * wy + vp[10] * wz + vp[14];
    float clip_w = vp[3] * wx + vp[7] * wy + vp[11] * wz + vp[15];

    float ndc_x = (clip_w != 0.0f) ? clip_x / clip_w : 0.0f;
    float ndc_y = (clip_w != 0.0f) ? clip_y / clip_w : 0.0f;
    float ndc_z = (clip_w != 0.0f) ? clip_z / clip_w : 0.0f;

    uint8_t issues = ISSUE_NONE;

    if (ndc_x < -1.5f || ndc_x > 1.5f || ndc_y < -1.5f || ndc_y > 1.5f) {
      issues |= ISSUE_OFFSCREEN;
    }

    if (clip_w < 0.0f) {
      issues |= ISSUE_BEHIND_CAMERA;
    }

    if (material->texture_view.id == SG_INVALID_ID) {
      issues |= ISSUE_NO_TEXTURE;
    }

    if (!mesh->gpu_valid) {
      issues |= ISSUE_INVALID_MESH;
    }

    if (mesh->tri_count <= 0) {
      issues |= ISSUE_ZERO_TRIS;
    }

    float scale_x = transform[0], scale_y = transform[5],
          scale_z = transform[10];
    if (scale_x == 0.0f && scale_y == 0.0f && scale_z == 0.0f) {
      issues |= ISSUE_ZERO_SIZE;
    }

    RenderDiagEntry diag_entry = {
        .id = queue_idx,
        .world_x = wx,
        .world_y = wy,
        .world_z = wz,
        .ndc_x = ndc_x,
        .ndc_y = ndc_y,
        .ndc_z = ndc_z,
        .in_viewport = !(issues & ISSUE_OFFSCREEN),
        .altitude = wy,
        .render_queue = material->renderQueue,
        .texture_id = (material->texture_view.id != SG_INVALID_ID)
                          ? (int)material->texture_view.id
                          : -1,
        .tri_count = mesh->tri_count,
        .gpu_valid = mesh->gpu_valid,
        .scale_x = scale_x,
        .scale_y = scale_y,
        .scale_z = scale_z,
        .issue_flags = issues,
    };

    if (wx != 0.0f || wz != 0.0f) {
      snprintf(diag_entry.name, DIAG_MAX_NAME_LEN, "Mesh");
    } else {
      snprintf(diag_entry.name, DIAG_MAX_NAME_LEN, "Origin");
    }

    render_diag_record(&diag_entry);
  }
}

void graphics_draw_mesh_identity(Mesh *mesh, Material *material, int layer) {
  graphics_draw_mesh(mesh, MATRIX4_IDENTITY_CONST, material, NULL, layer);
}

void graphics_draw_mesh_instanced(Mesh *mesh, Material *material,
                                  const Matrix4 *transforms, const Vec4 *colors,
                                  const Vec4 *uv_rects, int count, int layer) {
  if (!mesh || !material || count <= 0)
    return;
  if (g_graphics.instanced_queue_count >= MAX_INSTANCED_QUEUE) {
    // 队列满禁止静默丢弃 (整批实例不画 = 排序流尾段消失), 限频报错可见
    static int s_queue_overflow_log = 0;
    if (s_queue_overflow_log < 5) {
      s_queue_overflow_log++;
      fprintf(stderr, "[graphics] instanced queue overflow: %d requests dropped batch of %d\n",
              g_graphics.instanced_queue_count, count);
    }
    return;
  }

  // 打包池余量截断 (总量 = GPU instance buffer 容量)
  int room = (MAX_INSTANCE_BATCH * (int)INSTANCE_STRIDE_BYTES -
              g_graphics.inst_pack_used) / (int)INSTANCE_STRIDE_BYTES;
  if (count > room) {
    static int s_pack_overflow_log = 0;
    if (s_pack_overflow_log < 5) {
      s_pack_overflow_log++;
      fprintf(stderr, "[graphics] instance pack overflow: want %d room %d\n",
              count, room);
    }
    count = room;
  }
  if (count <= 0)
    return;

  extern Draw_Frame draw_frame;
  Matrix4 view, vp;
  matrix4_inverse(draw_frame.coord_space.camera, view);
  matrix4_mul(draw_frame.coord_space.proj, view, vp);

  // 入队即打包拷贝 — 调用方 buffer (含 JS TypedArray) 帧末已不可信
  int pack_offset = g_graphics.inst_pack_used;
  float *dst = (float *)(g_graphics.inst_pack + pack_offset);
  for (int k = 0; k < count; k++) {
    memcpy(dst, &transforms[k], sizeof(Matrix4));
    dst += 16;
    if (colors) {
      memcpy(dst, &colors[k], sizeof(Vec4));
    } else {
      dst[0] = 1.0f; dst[1] = 1.0f; dst[2] = 1.0f; dst[3] = 1.0f;
    }
    dst += 4;
    if (uv_rects) {
      memcpy(dst, &uv_rects[k], sizeof(Vec4));
    } else {
      dst[0] = 0.0f; dst[1] = 0.0f; dst[2] = 1.0f; dst[3] = 1.0f;
    }
    dst += 4;
  }
  g_graphics.inst_pack_used += count * (int)INSTANCE_STRIDE_BYTES;

  int idx = g_graphics.instanced_queue_count++;
  InstancedDrawRequest *req = &g_graphics.instanced_queue[idx];
  req->mesh = mesh;
  memcpy(&req->material, material, sizeof(Material));
  req->pack_offset = pack_offset;
  memcpy(req->vp_matrix, vp, sizeof(Matrix4));
  req->count = count;
  req->layer = layer;
  req->valid = true;

  // 分带语义与 mesh 路一致: layer 折进 renderQueue (带外值门禁 FATAL)
  material_set_render_queue(&req->material, req->material.renderQueue + layer);
}

static int compare_mesh_request_by_renderqueue(const void *a, const void *b) {
  const MeshDrawRequest *ra = (const MeshDrawRequest *)a;
  const MeshDrawRequest *rb = (const MeshDrawRequest *)b;
  int qa = ra->material.renderQueue;
  int qb = rb->material.renderQueue;
  if (qa != qb) return qa - qb;
  return ra->queue_order - rb->queue_order;
}

void graphics_submit_meshes(void) {

  static int s_submit_debug = 0;
  s_submit_debug++;

  // Count non-origin mesh requests (transform[12]/[13] != 0). Used for
  // diagnostic logging only — these are the meshes that actually need
  // a per-frame transform, as opposed to baked geometry that draws at
  // origin via a single batched call.
  int transformed_count = 0;
  for (int i = 0; i < g_graphics.mesh_queue_count; i++) {
    MeshDrawRequest *r = &g_graphics.mesh_queue[i];
    if (r->valid && (r->transform[12] != 0.0f || r->transform[13] != 0.0f)) {
      transformed_count++;
    }
  }

  static int s_submit_diag_count = 0;
  if (transformed_count > 0 && s_submit_diag_count < 10) {
    s_submit_diag_count++;
    fprintf(stderr,
            "[graphics] submit_meshes #%d: queue_count=%d, transformed=%d\n",
            s_submit_diag_count, g_graphics.mesh_queue_count, transformed_count);
  }

  if (g_graphics.mesh_queue_count == 0 &&
      g_graphics.instanced_queue_count == 0) {
    return;
  }

  extern Draw_Frame draw_frame;

  extern Render_State render_state;
  extern Atlas atlas;
  extern Font font;

  sg_pipeline current_pip = render_state.pip;
  sg_apply_pipeline(current_pip);

  qsort(g_graphics.mesh_queue, g_graphics.mesh_queue_count,
        sizeof(MeshDrawRequest), compare_mesh_request_by_renderqueue);

  static int s_qsort_diag_count = 0;

  // Re-count after sort, log a few times for sanity checks.
  int post_sort_transformed = 0;
  for (int ii = 0; ii < g_graphics.mesh_queue_count; ii++) {
    MeshDrawRequest *r = &g_graphics.mesh_queue[ii];
    if (r->valid && (r->transform[12] != 0.0f || r->transform[13] != 0.0f)) {
      post_sort_transformed++;
    }
  }
  if (post_sort_transformed > 0 && s_qsort_diag_count < 3) {
    s_qsort_diag_count++;
    fprintf(stderr,
            "[graphics] F%d after qsort: transformed=%d, queue_count=%d\n",
            g_graphics_frame_counter, post_sort_transformed,
            g_graphics.mesh_queue_count);
    for (int i = 0; i < g_graphics.mesh_queue_count; i++) {
      MeshDrawRequest *r = &g_graphics.mesh_queue[i];
      if (r->transform[12] != 0.0f || r->transform[13] != 0.0f) {
        fprintf(stderr,
                "  mesh idx=%d, valid=%d, x=%.1f, y=%.1f, tex_view=%u, rq=%d\n",
                i, r->valid, r->transform[12], r->transform[13],
                r->material.texture_view.id, r->material.renderQueue);
      }
    }
  }

  for (int i = 0; i < g_graphics.mesh_queue_count; i++) {
    MeshDrawRequest *req = &g_graphics.mesh_queue[i];
    if (!req->valid)
      continue;

    Mesh *mesh = req->mesh;
    Material *mat = &req->material;

    sg_pipeline pip_to_use = (mat->pipeline.id != SG_INVALID_ID)
        ? mat->pipeline
        : render_state.pip;
    if (pip_to_use.id != current_pip.id) {
      sg_apply_pipeline(pip_to_use);
      current_pip = pip_to_use;
    }

    Matrix4 mvp;
    matrix4_mul(req->vp_matrix, req->transform, mvp);

    sg_bindings bind = {
        .vertex_buffers[0] = mesh->vbuf,
        .index_buffer = mesh->ibuf,
    };

    if (mat->texture_view.id != SG_INVALID_ID) {
      bind.views[VIEW_tex0] = mat->texture_view;
    } else {
      bind.views[VIEW_tex0] =
          render_state.bind.views[VIEW_tex0];
    }
    bind.views[VIEW_font_tex] = render_state.bind.views[VIEW_font_tex];

    bind.views[VIEW_flow_map] = render_state.bind.views[VIEW_flow_map];

    bind.views[VIEW_ripple_tex] = render_state.bind.views[VIEW_ripple_tex];
    bind.views[VIEW_noise_tex]  = render_state.bind.views[VIEW_noise_tex];
    bind.samplers[SMP_default_sampler] =
        render_state.bind.samplers[SMP_default_sampler];

    sg_apply_bindings(&bind);

    VS_MVP_t vs_mvp = {0};
    memcpy(vs_mvp.mvp, mvp, sizeof(Matrix4));
    vs_mvp.use_mvp = 1.0f;

    vs_mvp.sway_head = render_get_sway_head();

    sg_apply_uniforms(UB_VS_MVP,
                      &(sg_range){.ptr = &vs_mvp, .size = sizeof(VS_MVP_t)});

    int tex_index = material_get_tex_index(mat);

    Shader_Data sd = draw_frame.shader_data;
    sd.batch_props[0] = (float)tex_index;

    memcpy(sd.col_override, mat->color, sizeof(Vec4));

    memcpy(sd.col_override_2, mat->color_two, sizeof(Vec4));

    memset(sd.params, 0, sizeof(Vec4));

    sd.params[0] = (float)seconds_since_init();

    sd.params[1] = engine_get_world_inv_size();

    memcpy(sd.params_2, mat->params, sizeof(Vec4));

    if (mat->shader_type == SHADER_TYPE_EDGE_DETECT &&
        mat->texture.id != SG_INVALID_ID) {
      int tw = sg_query_image_width(mat->texture);
      int th = sg_query_image_height(mat->texture);
      if (tw > 0 && th > 0) {
        sd.params[2] = 1.0f / (float)tw;
        sd.params[3] = 1.0f / (float)th;
      }
    }

    if (req->has_block) {
      if (req->block.use_color) {
        memcpy(sd.col_override, req->block.color, sizeof(Vec4));
      }
      if (req->block.use_color_two) {
        memcpy(sd.col_override_2, req->block.color_two, sizeof(Vec4));
      }
    }

    sg_apply_uniforms(UB_Shader_Data,
                      &(sg_range){.ptr = &sd, .size = sizeof(Shader_Data)});

    sg_draw(0, mesh->tri_count, 1);

    g_graphics.draw_calls_this_frame++;
    g_graphics.triangles_this_frame += mesh->tri_count / 3;
  }

  g_graphics.mesh_queue_count = 0;

}

void graphics_submit_meshes_range(int rq_min, int rq_max, bool clear_after,
                                  const Vec4 debug_color) {
  graphics_submit_meshes_range_count(rq_min, rq_max, clear_after, debug_color);
}

// 渲染契约 P3: 按注册带统计每帧 mesh dc（diag.rqBands() 数据源）。
// 末位槽计未注册带（理论上被门禁挡住, 此处兜底观测）。
static int g_band_dc[RQ_BAND_COUNT + 1];
static int g_band_dc_prev[RQ_BAND_COUNT + 1];

const int *graphics_band_dc_prev(void) { return g_band_dc_prev; }

int graphics_submit_meshes_range_count(int rq_min, int rq_max, bool clear_after,
                                       const Vec4 debug_color) {
  if (g_graphics.mesh_queue_count == 0) {
    if (clear_after) {
      memcpy(g_band_dc_prev, g_band_dc, sizeof(g_band_dc));
      memset(g_band_dc, 0, sizeof(g_band_dc));
    }
    return 0;
  }
  int dc_count = 0;

  extern Draw_Frame draw_frame;
  extern Render_State render_state;

  qsort(g_graphics.mesh_queue, g_graphics.mesh_queue_count,
        sizeof(MeshDrawRequest), compare_mesh_request_by_renderqueue);

  sg_pipeline current_pip = render_state.pip;
  sg_apply_pipeline(current_pip);

  for (int i = 0; i < g_graphics.mesh_queue_count; i++) {
    MeshDrawRequest *req = &g_graphics.mesh_queue[i];
    if (!req->valid)
      continue;

    int rq = req->material.renderQueue;
    if (rq < rq_min || rq >= rq_max)
      continue;

    Mesh *mesh = req->mesh;
    Material *mat = &req->material;

    sg_pipeline pip_to_use = (mat->pipeline.id != SG_INVALID_ID)
        ? mat->pipeline
        : render_state.pip;
    if (pip_to_use.id != current_pip.id) {
      sg_apply_pipeline(pip_to_use);
      current_pip = pip_to_use;
    }

    Matrix4 mvp;
    matrix4_mul(req->vp_matrix, req->transform, mvp);

    sg_bindings bind = {
        .vertex_buffers[0] = mesh->vbuf,
        .index_buffer = mesh->ibuf,
    };
    if (mat->texture_view.id != SG_INVALID_ID) {
      bind.views[VIEW_tex0] = mat->texture_view;
    } else {
      bind.views[VIEW_tex0] = render_state.bind.views[VIEW_tex0];
    }
    bind.views[VIEW_font_tex] = render_state.bind.views[VIEW_font_tex];
    bind.views[VIEW_flow_map] = render_state.bind.views[VIEW_flow_map];
    bind.views[VIEW_ripple_tex] = render_state.bind.views[VIEW_ripple_tex];
    bind.views[VIEW_noise_tex]  = render_state.bind.views[VIEW_noise_tex];
    bind.samplers[SMP_default_sampler] =
        render_state.bind.samplers[SMP_default_sampler];
    sg_apply_bindings(&bind);

    VS_MVP_t vs_mvp = {0};
    memcpy(vs_mvp.mvp, mvp, sizeof(Matrix4));
    vs_mvp.use_mvp = 1.0f;

    vs_mvp.sway_head = render_get_sway_head();
    sg_apply_uniforms(UB_VS_MVP,
                      &(sg_range){.ptr = &vs_mvp, .size = sizeof(VS_MVP_t)});

    int tex_index = material_get_tex_index(mat);
    Shader_Data sd = draw_frame.shader_data;
    sd.batch_props[0] = (float)tex_index;
    memcpy(sd.col_override, mat->color, sizeof(Vec4));
    memcpy(sd.col_override_2, mat->color_two, sizeof(Vec4));
    memset(sd.params, 0, sizeof(Vec4));

    sd.params[0] = (float)seconds_since_init();

    sd.params[1] = engine_get_world_inv_size();
    memcpy(sd.params_2, mat->params, sizeof(Vec4));
    if (req->has_block) {
      if (req->block.use_color)
        memcpy(sd.col_override, req->block.color, sizeof(Vec4));
      if (req->block.use_color_two)
        memcpy(sd.col_override_2, req->block.color_two, sizeof(Vec4));
    }

    if (debug_color && debug_color[3] > 0.0f) {
      memcpy(sd.col_override, debug_color, sizeof(Vec4));
    }
    sg_apply_uniforms(UB_Shader_Data,
                      &(sg_range){.ptr = &sd, .size = sizeof(Shader_Data)});

    sg_draw(0, mesh->tri_count, 1);
    g_graphics.draw_calls_this_frame++;
    g_graphics.triangles_this_frame += mesh->tri_count / 3;
    dc_count++;

    {
      int bi = render_queue_band_index(rq);
      g_band_dc[bi >= 0 ? bi : RQ_BAND_COUNT]++;
    }
  }

  if (clear_after) {
    g_graphics.mesh_queue_count = 0;
    memcpy(g_band_dc_prev, g_band_dc, sizeof(g_band_dc));
    memset(g_band_dc, 0, sizeof(g_band_dc));
  }
  return dc_count;
}

void graphics_submit_instanced(void) {
  graphics_submit_instanced_count();
}

static void submit_one_instanced(InstancedDrawRequest *req) {
  extern Render_State render_state;
  extern Draw_Frame draw_frame;

  int count = req->count;
  size_t data_size = count * INSTANCE_STRIDE_BYTES;
  int append_offset = sg_append_buffer(
      render_state.bind.vertex_buffers[1],
      &(sg_range){.ptr = g_graphics.inst_pack + req->pack_offset,
                  .size = data_size});

  sg_pipeline pip_to_use = render_state.inst_pip;
  if (req->material.blend_mode == BLEND_MODE_ADDITIVE) {
    if (sg_query_pipeline_state(render_state.inst_pip_additive) ==
        SG_RESOURCESTATE_VALID) {
      pip_to_use = render_state.inst_pip_additive;
    }
  } else if (req->material.blend_mode == BLEND_MODE_ALPHA ||
             req->material.blend_mode == BLEND_MODE_PREMULTIPLIED) {
    if (sg_query_pipeline_state(render_state.inst_pip_alpha) ==
        SG_RESOURCESTATE_VALID) {
      pip_to_use = render_state.inst_pip_alpha;
    }
  }
  sg_apply_pipeline(pip_to_use);

  sg_bindings bind = {
      .vertex_buffers[0] = req->mesh->vbuf,
      .vertex_buffers[1] = render_state.bind.vertex_buffers[1],
      .vertex_buffer_offsets[1] = append_offset,
      .index_buffer = req->mesh->ibuf};

  if (req->material.texture_view.id != SG_INVALID_ID) {
    bind.views[VIEW_tex0] = req->material.texture_view;
  } else {
    bind.views[VIEW_tex0] = render_state.bind.views[VIEW_tex0];
  }
  bind.views[VIEW_font_tex] = render_state.bind.views[VIEW_font_tex];
  bind.views[VIEW_flow_map] = render_state.bind.views[VIEW_flow_map];
  bind.views[VIEW_ripple_tex] = render_state.bind.views[VIEW_ripple_tex];
  bind.views[VIEW_noise_tex] = render_state.bind.views[VIEW_noise_tex];
  bind.samplers[SMP_default_sampler] =
      render_state.bind.samplers[SMP_default_sampler];

  sg_apply_bindings(&bind);

  VS_MVP_t vs_mvp = {0};
  memcpy(vs_mvp.mvp, req->vp_matrix, sizeof(Matrix4));
  vs_mvp.use_mvp = 1.0f;
  vs_mvp.sway_head = render_get_sway_head();
  sg_apply_uniforms(UB_VS_MVP,
                    &(sg_range){.ptr = &vs_mvp, .size = sizeof(VS_MVP_t)});

  Shader_Data inst_sd = draw_frame.shader_data;
  inst_sd.batch_props[0] = (float)material_get_tex_index(&req->material);
  // 与 mesh 路 (上方 memcpy(sd.params_2, mat->params)) 对齐:
  // instanced 同样以 material.params 喂 params_2 (fx 画刷 def 级参数经此下达)
  memcpy(inst_sd.params_2, req->material.params, sizeof(Vec4));

  inst_sd.col_override[0] = 1.0f;
  inst_sd.col_override[1] = 1.0f;
  inst_sd.col_override[2] = 1.0f;
  inst_sd.col_override[3] = 1.0f;
  inst_sd.col_override_2[0] = 1.0f;
  inst_sd.col_override_2[1] = 1.0f;
  inst_sd.col_override_2[2] = 1.0f;
  inst_sd.col_override_2[3] = 1.0f;
  sg_apply_uniforms(UB_Shader_Data,
                    &(sg_range){.ptr = &inst_sd, .size = sizeof(Shader_Data)});

  sg_draw(0, req->mesh->tri_count, count);

  g_graphics.draw_calls_this_frame++;
  g_graphics.triangles_this_frame += (req->mesh->tri_count / 3) * count;
}

int graphics_submit_instanced_count(void) {
  if (g_graphics.instanced_queue_count == 0)
    return 0;
  int dc_count = 0;

  for (int i = 0; i < g_graphics.instanced_queue_count; i++) {
    InstancedDrawRequest *req = &g_graphics.instanced_queue[i];
    if (!req->valid || req->count <= 0)
      continue;
    submit_one_instanced(req);
    dc_count++;
  }

  g_graphics.instanced_queue_count = 0;
  return dc_count;
}

int graphics_submit_instanced_range_count(int rq_min, int rq_max) {
  int dc_count = 0;
  for (int i = 0; i < g_graphics.instanced_queue_count; i++) {
    InstancedDrawRequest *req = &g_graphics.instanced_queue[i];
    if (!req->valid || req->count <= 0)
      continue;
    int rq = req->material.renderQueue;
    if (rq < rq_min || rq >= rq_max)
      continue;
    submit_one_instanced(req);
    req->valid = false; // 已消费, 帧末兜底 submit 跳过
    dc_count++;
  }
  return dc_count;
}

int graphics_get_draw_calls(void) { return g_graphics.draw_calls_this_frame; }

int graphics_get_triangle_count(void) {
  return g_graphics.triangles_this_frame;
}
