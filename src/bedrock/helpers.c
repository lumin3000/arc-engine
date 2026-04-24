
#include "helpers.h"
#include "engine_state.h"
#include "gfx/camera.h"
#include "gfx/draw.h"
#include "input/input.h"
#include "utils/shape.h"
#include "utils/utils.h"
#include <math.h>
#include <stdio.h>

Coord_Space get_world_space(void) {
  Coord_Space space;
  get_world_space_proj(space.proj);
  get_world_space_camera(space.camera);
  return space;
}

Coord_Space get_screen_space(void) {
  Coord_Space space;
  get_screen_space_proj(space.proj);
  memcpy(space.camera, MATRIX4_IDENTITY, sizeof(Matrix4));
  return space;
}

void get_world_space_proj(Matrix4 out) {

  Camera *cam = camera_get_main();
  const Matrix4 *proj = camera_get_projection_matrix(cam);

  if (proj) {
    memcpy(out, proj, sizeof(Matrix4));
  } else {

    xform_identity(out);
  }
}

void get_world_space_camera(Matrix4 out) {

  Camera *cam = camera_get_main();

  Vec3 scale = {1.0f, 1.0f, 1.0f};
  matrix4_from_trs(cam->position, cam->rotation, scale, out);
}

float get_camera_zoom(void) {

  if (ctx.gs && ctx.gs->zoom_level > 0.0f) {
    return ctx.gs->zoom_level;
  }
  return 1.0f;
}

void get_screen_space_proj(Matrix4 out) {

  float w = (float)window_w;
  float h = (float)window_h;

  matrix4_ortho3d(0.0f, w, h, 0.0f, -1.0f, 1.0f, out);
}

void screen_pivot(Pivot pivot, Vec2 out) {
  float x = 0.0f;
  float y = 0.0f;

  switch (pivot) {
  case Pivot_top_left:
    x = 0.0f;
    y = (float)window_h;
    break;

  case Pivot_top_center:
    x = (float)window_w / 2.0f;
    y = (float)window_h;
    break;

  case Pivot_bottom_left:
    x = 0.0f;
    y = 0.0f;
    break;

  case Pivot_center_center:
    x = (float)window_w / 2.0f;
    y = (float)window_h / 2.0f;
    break;

  case Pivot_top_right:
    x = (float)window_w;
    y = (float)window_h;
    break;

  case Pivot_bottom_center:
    x = (float)window_w / 2.0f;
    y = 0.0f;
    break;

  default:
    fprintf(stderr, "TODO: screen_pivot for pivot %d\n", pivot);
    break;
  }

  float ndc_x = (x / ((float)window_w * 0.5f)) - 1.0f;
  float ndc_y = (y / ((float)window_h * 0.5f)) - 1.0f;

  Vec2 mouse_ndc = {ndc_x, ndc_y};
  float mouse_world_h[4] = {mouse_ndc[0], mouse_ndc[1], 0.0f, 1.0f};

  Matrix4 screen_proj, inv_screen_proj;
  get_screen_space_proj(screen_proj);
  matrix4_inverse(screen_proj, inv_screen_proj);

  float result[4];
  matrix4_mul_vec4(inv_screen_proj, mouse_world_h, result);

  out[0] = result[0];
  out[1] = result[1];
}

void raw_button(Rect rect, bool *out_hover, bool *out_pressed) {
  Vec2 mouse_pos;
  mouse_pos_in_current_space(mouse_pos);

  bool hover = rect_contains(rect, mouse_pos);
  bool pressed = false;

  if (hover && key_pressed(KEY_LEFT_MOUSE)) {
    consume_key_pressed(KEY_LEFT_MOUSE);
    pressed = true;
  }

  if (out_hover)
    *out_hover = hover;
  if (out_pressed)
    *out_pressed = pressed;
}

void mouse_pos_in_current_space(Vec2 out) {
  Matrix4 proj, cam;
  memcpy(proj, draw_frame.coord_space.proj, sizeof(Matrix4));
  memcpy(cam, draw_frame.coord_space.camera, sizeof(Matrix4));

  bool proj_is_zero = true;
  bool cam_is_zero = true;

  for (int i = 0; i < 16; i++) {
    if (proj[i] != 0.0f)
      proj_is_zero = false;
    if (cam[i] != 0.0f)
      cam_is_zero = false;
  }

  if (proj_is_zero || cam_is_zero) {
    fprintf(stderr, "not in a space, need to push_coord_space first\n");
    out[0] = 0.0f;
    out[1] = 0.0f;
    return;
  }

  Vec2 mouse = {input_state->mouse_x, input_state->mouse_y};

  float ndc_x = (mouse[0] / ((float)window_w * 0.5f)) - 1.0f;
  float ndc_y = (mouse[1] / ((float)window_h * 0.5f)) - 1.0f;
  ndc_y *= -1.0f;

  Vec2 mouse_ndc = {ndc_x, ndc_y};

  float mouse_world_h[4] = {mouse_ndc[0], mouse_ndc[1], 0.0f, 1.0f};

  Matrix4 inv_proj;
  matrix4_inverse(proj, inv_proj);

  float temp[4];
  matrix4_mul_vec4(inv_proj, mouse_world_h, temp);

  float result[4];
  matrix4_mul_vec4(cam, temp, result);

  out[0] = result[0];
  out[1] = result[1];
}

double app_now(void) { return seconds_since_init(); }

float time_since(double time) {
  if (time == 0.0) {
    return 99999999.0f;
  }
  return (float)(ctx.gs->game_time_elapsed - time);
}

void get_sprite_center_mass(Sprite_Name sprite, Vec2 out) {
  Vec2 size;
  get_sprite_size(sprite, size);

  Vec2 offset;
  Pivot pivot;
  get_sprite_offset(sprite, offset, &pivot);

  Vec2 pivot_scale;
  scale_from_pivot(pivot, pivot_scale);

  Vec2 center = {size[0] * pivot_scale[0], size[1] * pivot_scale[1]};

  center[0] -= offset[0];
  center[1] -= offset[1];

  out[0] = center[0];
  out[1] = center[1];
}
