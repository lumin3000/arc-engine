
#include "draw.h"
#include "../utils/utils.h"
#include "graphics.h"
#include "material.h"
#include "mesh.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static Mesh g_xz_quad_mesh;
static Material g_xz_quad_material;
static bool g_xz_quad_initialized = false;

#define MAX_LINE_XZ_INSTANCES 512
static Matrix4 g_line_xz_transforms[MAX_LINE_XZ_INSTANCES];
static Vec4    g_line_xz_colors[MAX_LINE_XZ_INSTANCES];
static int     g_line_xz_count = 0;

static void _ensure_xz_quad(void) {
  if (g_xz_quad_initialized)
    return;

  mesh_init(&g_xz_quad_mesh, 4, 6);

  mesh_add_vertex(&g_xz_quad_mesh, 0, 0, 0, 1, 1, 1, 1, 0, 0);
  mesh_add_vertex(&g_xz_quad_mesh, 1, 0, 0, 1, 1, 1, 1, 1, 0);
  mesh_add_vertex(&g_xz_quad_mesh, 1, 0, 1, 1, 1, 1, 1, 1, 1);
  mesh_add_vertex(&g_xz_quad_mesh, 0, 0, 1, 1, 1, 1, 1, 0, 1);
  mesh_add_triangle(&g_xz_quad_mesh, 0, 2, 1);
  mesh_add_triangle(&g_xz_quad_mesh, 0, 3, 2);
  mesh_upload_to_gpu(&g_xz_quad_mesh);

  material_init(&g_xz_quad_material);
  material_set_shader_type(&g_xz_quad_material, SHADER_TYPE_NONE);
  material_set_color(&g_xz_quad_material, 1, 1, 1, 1);
  material_set_render_queue(&g_xz_quad_material, RQ_TRANSPARENT);

  g_xz_quad_initialized = true;
}

const Draw_Sprite_Opts DRAW_SPRITE_OPTS_DEFAULT = {
    .pivot = Pivot_center_center,
    .flip_x = false,
    .draw_offset = {0, 0},
    .xform = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0,
              1},
    .anim_index = 0,
    .col = {1.0f, 1.0f, 1.0f, 1.0f},
    .col_override = {1.0f, 1.0f, 1.0f, 1.0f},
    .z_layer = ZLayer_nil,
    .flags = 0,
    .params = {0, 0, 0, 0},
    .crop_top = 0.0f,
    .crop_left = 0.0f,
    .crop_bottom = 0.0f,
    .crop_right = 0.0f,
    .z_layer_queue = -1};

const Draw_Rect_Opts DRAW_RECT_OPTS_DEFAULT = {
    .sprite = Sprite_Name_nil,
    .uv = {0, 0, 1, 1},
    .outline_col = {0, 0, 0, 0},
    .col = {1.0f, 1.0f, 1.0f, 1.0f},
    .col_override = {1.0f, 1.0f, 1.0f, 1.0f},
    .z_layer = ZLayer_nil,
    .flags = 0,
    .params = {0, 0, 0, 0},
    .crop_top = 0.0f,
    .crop_left = 0.0f,
    .crop_bottom = 0.0f,
    .crop_right = 0.0f,
    .z_layer_queue = -1};

const Draw_Sprite_In_Rect_Opts DRAW_SPRITE_IN_RECT_OPTS_DEFAULT = {
    .xform = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0,
              1},
    .col = {1.0f, 1.0f, 1.0f, 1.0f},
    .col_override = {1.0f, 1.0f, 1.0f, 1.0f},
    .z_layer = ZLayer_nil,
    .flags = 0,
    .pad_pct = 0.1f};

const Draw_Rect_Xform_Opts DRAW_RECT_XFORM_OPTS_DEFAULT = {
    .sprite = Sprite_Name_nil,
    .uv = {0, 0, 1, 1},
    .tex_index = 0,
    .anim_index = 0,
    .col = {1.0f, 1.0f, 1.0f, 1.0f},
    .col_override = {1.0f, 1.0f, 1.0f, 1.0f},
    .z_layer = ZLayer_nil,
    .flags = 0,
    .params = {0, 0, 0, 0},
    .crop_top = 0.0f,
    .crop_left = 0.0f,
    .crop_bottom = 0.0f,
    .crop_right = 0.0f,
    .z_layer_queue = -1};

const Draw_Line_Opts DRAW_LINE_OPTS_DEFAULT = {
    .width = 1.0f,
    .col = {1.0f, 1.0f, 1.0f, 1.0f},
    .z_layer = ZLayer_playspace,
    .z_layer_queue = -1};

const Draw_Line_XZ_Opts DRAW_LINE_XZ_OPTS_DEFAULT = {
    .width = 0.2f,
    .col = {1.0f, 1.0f, 1.0f, 1.0f},
    .altitude = 0.5f};

void draw_sprite_ex(Vec2 pos, Sprite_Name sprite, Draw_Sprite_Opts opts) {
  Vec2 rect_size;
  get_sprite_size(sprite, rect_size);
  int frame_count = get_frame_count(sprite);
  rect_size[0] /= (float)frame_count;

  Matrix4 xform0;
  xform_identity(xform0);

  Matrix4 translate_pos;
  xform_translate(pos, translate_pos);
  matrix4_mul(xform0, translate_pos, xform0);

  if (opts.flip_x) {
    Matrix4 flip;
    Vec2 flip_scale = {-1.0f, 1.0f};
    xform_scale(flip_scale, flip);
    matrix4_mul(xform0, flip, xform0);
  }

  matrix4_mul(xform0, opts.xform, xform0);

  Vec2 pivot_scale;
  scale_from_pivot(opts.pivot, pivot_scale);
  Vec2 pivot_offset = {-rect_size[0] * pivot_scale[0],
                       -rect_size[1] * pivot_scale[1]};
  Matrix4 pivot_xform;
  xform_translate(pivot_offset, pivot_xform);
  matrix4_mul(xform0, pivot_xform, xform0);

  Vec2 neg_draw_offset = {-opts.draw_offset[0], -opts.draw_offset[1]};
  Matrix4 draw_offset_xform;
  xform_translate(neg_draw_offset, draw_offset_xform);
  matrix4_mul(xform0, draw_offset_xform, xform0);

  Draw_Rect_Xform_Opts rect_opts = DRAW_RECT_XFORM_OPTS_DEFAULT;
  rect_opts.sprite = sprite;
  rect_opts.tex_index = 0;
  rect_opts.anim_index = opts.anim_index;
  memcpy(rect_opts.col, opts.col, sizeof(Vec4));
  memcpy(rect_opts.col_override, opts.col_override, sizeof(Vec4));
  rect_opts.z_layer = opts.z_layer;
  rect_opts.flags = opts.flags;
  memcpy(rect_opts.params, opts.params, sizeof(Vec4));
  rect_opts.crop_top = opts.crop_top;
  rect_opts.crop_left = opts.crop_left;
  rect_opts.crop_bottom = opts.crop_bottom;
  rect_opts.crop_right = opts.crop_right;
  rect_opts.z_layer_queue = opts.z_layer_queue;

  draw_rect_xform_ex(xform0, rect_size, rect_opts);
}

void draw_rect_ex(Rect rect, Draw_Rect_Opts opts) {

  Vec2 rect_pos = {rect[0], rect[1]};
  Matrix4 xform;
  xform_translate(rect_pos, xform);

  Vec2 size = {rect[2] - rect[0], rect[3] - rect[1]};

  Vec4 zero_vec = {0, 0, 0, 0};
  if (memcmp(&opts.outline_col, &zero_vec, sizeof(Vec4)) != 0) {
    Vec2 outline_size = {size[0] + 2.0f, size[1] + 2.0f};
    Vec2 outline_offset = {-1.0f, -1.0f};

    Matrix4 outline_xform;
    xform_translate(outline_offset, outline_xform);
    matrix4_mul(xform, outline_xform, outline_xform);

    Draw_Rect_Xform_Opts outline_opts = DRAW_RECT_XFORM_OPTS_DEFAULT;
    outline_opts.sprite = Sprite_Name_nil;
    memcpy(outline_opts.uv, opts.uv, sizeof(Vec4));
    outline_opts.tex_index = 0;
    outline_opts.anim_index = 0;
    memcpy(outline_opts.col, opts.outline_col, sizeof(Vec4));
    memcpy(outline_opts.col_override, opts.col_override, sizeof(Vec4));
    outline_opts.z_layer = opts.z_layer;
    outline_opts.flags = opts.flags;
    memcpy(outline_opts.params, opts.params, sizeof(Vec4));
    outline_opts.crop_top = 0.0f;
    outline_opts.crop_left = 0.0f;
    outline_opts.crop_bottom = 0.0f;
    outline_opts.crop_right = 0.0f;
    outline_opts.z_layer_queue = opts.z_layer_queue;

    draw_rect_xform_ex(outline_xform, outline_size, outline_opts);
  }

  Draw_Rect_Xform_Opts rect_opts = DRAW_RECT_XFORM_OPTS_DEFAULT;
  rect_opts.sprite = opts.sprite;
  memcpy(rect_opts.uv, opts.uv, sizeof(Vec4));
  rect_opts.tex_index = 0;
  rect_opts.anim_index = 0;
  memcpy(rect_opts.col, opts.col, sizeof(Vec4));
  memcpy(rect_opts.col_override, opts.col_override, sizeof(Vec4));
  rect_opts.z_layer = opts.z_layer;
  rect_opts.flags = opts.flags;
  memcpy(rect_opts.params, opts.params, sizeof(Vec4));
  rect_opts.crop_top = opts.crop_top;
  rect_opts.crop_left = opts.crop_left;
  rect_opts.crop_bottom = opts.crop_bottom;
  rect_opts.crop_right = opts.crop_right;
  rect_opts.z_layer_queue = opts.z_layer_queue;

  draw_rect_xform_ex(xform, size, rect_opts);
}

void draw_rect_xz_ex(Rect rect, Draw_Rect_Opts opts) {

  _ensure_xz_quad();

  float x = rect[0];
  float z = rect[1];
  float w = rect[2] - rect[0];
  float h = rect[3] - rect[1];

  Matrix4 transform;
  xform_identity(transform);

  transform[0] = w;
  transform[5] = 1.0f;
  transform[10] = h;
  transform[12] = x;
  transform[13] = 0.5f;
  transform[14] = z;

  Material mat = g_xz_quad_material;
  memcpy(mat.color, opts.col, sizeof(Vec4));

  graphics_draw_mesh(&g_xz_quad_mesh, transform, &mat, NULL, 0);
}

void draw_sprite_in_rect_ex(Sprite_Name sprite, Vec2 pos, Vec2 size,
                            Draw_Sprite_In_Rect_Opts opts) {
  Vec2 img_size;
  get_sprite_size(sprite, img_size);

  Rect rect = {pos[0], pos[1], pos[0] + size[0], pos[1] + size[1]};

  {
    Vec2 rect_size = {rect[2] - rect[0], rect[3] - rect[1]};
    float pad_x = rect_size[0] * opts.pad_pct * 0.5f;
    float pad_y = rect_size[1] * opts.pad_pct * 0.5f;

    rect[0] += pad_x;
    rect[1] += pad_y;
    rect[2] -= pad_x;
    rect[3] -= pad_y;
  }

  {
    Vec2 rect_size = {rect[2] - rect[0], rect[3] - rect[1]};

    float size_diff_x = rect_size[0] - img_size[0];
    if (size_diff_x < 0.0f)
      size_diff_x = 0.0f;

    float size_diff_y = rect_size[1] - img_size[1];
    if (size_diff_y < 0.0f)
      size_diff_y = 0.0f;

    Vec2 offset_pos = {rect[0], rect[1]};
    rect[0] = 0.0f + size_diff_x * 0.5f;
    rect[1] = 0.0f + size_diff_y * 0.5f;
    rect[2] = rect_size[0] - size_diff_x * 0.5f;
    rect[3] = rect_size[1] - size_diff_y * 0.5f;

    rect[0] += offset_pos[0];
    rect[1] += offset_pos[1];
    rect[2] += offset_pos[0];
    rect[3] += offset_pos[1];
  }

  if (img_size[0] > img_size[1]) {
    Vec2 rect_size = {rect[2] - rect[0], rect[3] - rect[1]};
    float new_height = rect_size[0] * (img_size[1] / img_size[0]);
    rect[3] = rect[1] + new_height;

    float height_diff = rect_size[1] - new_height;
    rect[1] += height_diff * 0.5f;
    rect[3] += height_diff * 0.5f;
  } else if (img_size[1] > img_size[0]) {
    Vec2 rect_size = {rect[2] - rect[0], rect[3] - rect[1]};
    float new_width = rect_size[1] * (img_size[0] / img_size[1]);
    rect[2] = rect[0] + new_width;

    float width_diff = rect_size[0] - new_width;
    rect[0] += width_diff * 0.5f;
    rect[2] += width_diff * 0.5f;
  }

  Draw_Rect_Opts draw_opts = DRAW_RECT_OPTS_DEFAULT;
  draw_opts.sprite = sprite;
  memcpy(draw_opts.col, opts.col, sizeof(Vec4));
  memcpy(draw_opts.col_override, opts.col_override, sizeof(Vec4));
  draw_opts.z_layer = opts.z_layer;
  draw_opts.flags = opts.flags;
  draw_opts.z_layer_queue = -1;

  draw_rect_ex(rect, draw_opts);
}

void draw_rect_xform_ex(Matrix4 xform, Vec2 size, Draw_Rect_Xform_Opts opts) {
  Vec4 uv;
  memcpy(uv, opts.uv, sizeof(Vec4));
  Vec2 current_size = {size[0], size[1]};

  Vec4 default_uv = {0, 0, 1, 1};
  if (memcmp(uv, default_uv, sizeof(Vec4)) == 0 &&
      opts.sprite != Sprite_Name_nil) {
    atlas_uv_from_sprite(opts.sprite, uv);

    int frame_count = get_frame_count(opts.sprite);
    Vec2 frame_size = {size[0] / (float)frame_count, size[1]};
    Vec2 uv_size = {uv[2] - uv[0], uv[3] - uv[1]};
    Vec2 uv_frame_size = {uv_size[0] * (frame_size[0] / size[0]), uv_size[1]};

    uv[2] = uv[0] + uv_frame_size[0];
    uv[3] = uv[1] + uv_frame_size[1];

    float uv_offset_x = (float)opts.anim_index * uv_frame_size[0];
    uv[0] += uv_offset_x;
    uv[2] += uv_offset_x;
  }

  Matrix4 model;
  memcpy(model, xform, sizeof(Matrix4));

  Matrix4 view;
  matrix4_inverse(draw_frame.coord_space.camera, view);

  Matrix4 projection;
  memcpy(projection, draw_frame.coord_space.proj, sizeof(Matrix4));

  Matrix4 temp, local_to_clip_space;
  matrix4_mul(view, model, temp);
  matrix4_mul(projection, temp, local_to_clip_space);

  static int debug_mvp = 1;
  if (debug_mvp && opts.sprite != Sprite_Name_nil) {
    fprintf(stderr, "\n==== MVP DEBUG for sprite %d ====\n", opts.sprite);
    fprintf(stderr, "Model matrix:\n");
    for (int i = 0; i < 4; i++) {
      fprintf(stderr, "  [%.4f, %.4f, %.4f, %.4f]\n", model[i], model[i + 4],
              model[i + 8], model[i + 12]);
    }
    fprintf(stderr, "View matrix (inverse of camera):\n");
    for (int i = 0; i < 4; i++) {
      fprintf(stderr, "  [%.4f, %.4f, %.4f, %.4f]\n", view[i], view[i + 4],
              view[i + 8], view[i + 12]);
    }
    fprintf(stderr, "Projection matrix:\n");
    for (int i = 0; i < 4; i++) {
      fprintf(stderr, "  [%.4f, %.4f, %.4f, %.4f]\n", projection[i],
              projection[i + 4], projection[i + 8], projection[i + 12]);
    }
    fprintf(stderr, "Final MVP matrix:\n");
    for (int i = 0; i < 4; i++) {
      fprintf(stderr, "  [%.4f, %.4f, %.4f, %.4f]\n", local_to_clip_space[i],
              local_to_clip_space[i + 4], local_to_clip_space[i + 8],
              local_to_clip_space[i + 12]);
    }

    Vec2 test_point = {0.0f, 0.0f};
    Vec2 transformed;
    matrix4_mul_vec2(local_to_clip_space, test_point, transformed);
    fprintf(stderr, "Transform (0,0) -> (%.4f, %.4f)\n", transformed[0],
            transformed[1]);

    fflush(stderr);
    debug_mvp = 0;
  }

  Matrix4 crop_xform;
  xform_identity(crop_xform);

  if (opts.crop_bottom > 0.0f) {
    float crop = current_size[1] * (1.0f - opts.crop_bottom);
    float diff = crop - current_size[1];
    current_size[1] = crop;

    Vec2 uv_size = {uv[2] - uv[0], uv[3] - uv[1]};
    uv[1] += uv_size[1] * opts.crop_bottom;

    Vec2 crop_offset = {0.0f, -diff};
    Matrix4 crop_translate;
    xform_translate(crop_offset, crop_translate);
    matrix4_mul(local_to_clip_space, crop_translate, local_to_clip_space);
  }

  if (opts.crop_right > 0.0f) {
    current_size[0] *= (1.0f - opts.crop_right);
    Vec2 uv_size = {uv[2] - uv[0], uv[3] - uv[1]};
    uv[2] -= uv_size[0] * opts.crop_right;
  }

  Vec2 bl = {0.0f, 0.0f};
  Vec2 tl = {0.0f, current_size[1]};
  Vec2 tr = {current_size[0], current_size[1]};
  Vec2 br = {current_size[0], 0.0f};

  Vec2 positions[4] = {
      {bl[0], bl[1]}, {tl[0], tl[1]}, {tr[0], tr[1]}, {br[0], br[1]}};

  Vec4 colors[4] = {{opts.col[0], opts.col[1], opts.col[2], opts.col[3]},
                    {opts.col[0], opts.col[1], opts.col[2], opts.col[3]},
                    {opts.col[0], opts.col[1], opts.col[2], opts.col[3]},
                    {opts.col[0], opts.col[1], opts.col[2], opts.col[3]}};

  Vec2 uvs[4] = {
      {uv[0], uv[1]},
      {uv[0], uv[3]},
      {uv[2], uv[3]},
      {uv[2], uv[1]}
  };

  uint8_t tex_index = opts.tex_index;
  if (tex_index == 0 && opts.sprite == Sprite_Name_nil) {

    tex_index = 255;
  }

  Vec4 col_override_2 = {1.0f, 1.0f, 1.0f,
                         1.0f};
  draw_quad_projected(local_to_clip_space, positions, colors, uvs, tex_index,
                      current_size, opts.col_override, col_override_2,
                      opts.z_layer, opts.flags, opts.params,
                      opts.z_layer_queue);
}

void draw_line_ex(Vec2 a, Vec2 b, Draw_Line_Opts opts) {
  float dx = b[0] - a[0];
  float dy = b[1] - a[1];
  float length = sqrtf(dx * dx + dy * dy);
  if (length < 0.001f)
    return;

  Vec2 mid = {(a[0] + b[0]) * 0.5f, (a[1] + b[1]) * 0.5f};

  float angle = atan2f(dy, dx) * (180.0f / (float)M_PI);

  Matrix4 t_mid, r_angle, t_offset, temp, xform;
  xform_translate(mid, t_mid);
  xform_rotate(angle, r_angle);
  Vec2 offset = {-length * 0.5f, -opts.width * 0.5f};
  xform_translate(offset, t_offset);

  matrix4_mul(t_mid, r_angle, temp);
  matrix4_mul(temp, t_offset, xform);

  Vec2 size = {length, opts.width};
  Draw_Rect_Xform_Opts rect_opts = DRAW_RECT_XFORM_OPTS_DEFAULT;
  memcpy(rect_opts.col, opts.col, sizeof(Vec4));
  rect_opts.z_layer = opts.z_layer;
  rect_opts.z_layer_queue = opts.z_layer_queue;

  draw_rect_xform_ex(xform, size, rect_opts);
}

void draw_line_xz_ex(Vec2 a, Vec2 b, Draw_Line_XZ_Opts opts) {
  float dx = b[0] - a[0];
  float dz = b[1] - a[1];
  float length = sqrtf(dx * dx + dz * dz);
  if (length < 0.001f)
    return;

  if (g_line_xz_count >= MAX_LINE_XZ_INSTANCES) {
    draw_line_xz_flush();
  }

  float mid_x = (a[0] + b[0]) * 0.5f;
  float mid_z = (a[1] + b[1]) * 0.5f;

  float angle = atan2f(dx, dz);
  float cos_a = cosf(angle);
  float sin_a = sinf(angle);

  float rs_00 = cos_a * opts.width;
  float rs_20 = -sin_a * opts.width;
  float rs_02 = sin_a * length;
  float rs_22 = cos_a * length;

  float off_x = rs_00 * (-0.5f) + rs_02 * (-0.5f);
  float off_z = rs_20 * (-0.5f) + rs_22 * (-0.5f);

  int idx = g_line_xz_count;
  Matrix4 *t = &g_line_xz_transforms[idx];
  xform_identity(*t);
  (*t)[0]  = rs_00;
  (*t)[2]  = rs_20;
  (*t)[5]  = 1.0f;
  (*t)[8]  = rs_02;
  (*t)[10] = rs_22;
  (*t)[12] = mid_x + off_x;
  (*t)[13] = opts.altitude;
  (*t)[14] = mid_z + off_z;

  memcpy(g_line_xz_colors[idx], opts.col, sizeof(Vec4));
  g_line_xz_count++;
}

void draw_line_xz_flush(void) {
  if (g_line_xz_count <= 0)
    return;
  _ensure_xz_quad();

  extern Coord_Space get_world_space(void);
  extern Coord_Space push_coord_space(Coord_Space coord);
  Coord_Space saved = push_coord_space(get_world_space());

  graphics_draw_mesh_instanced(&g_xz_quad_mesh, &g_xz_quad_material,
                               g_line_xz_transforms, g_line_xz_colors,
                               g_line_xz_count, 0);

  push_coord_space(saved);
  g_line_xz_count = 0;
}
