
#include "camera.h"
#include "../utils/utils.h"
#include <math.h>
#include <stdio.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

#define DEG_TO_RAD(deg) ((deg) * M_PI / 180.0f)
#define RAD_TO_DEG(rad) ((rad) * 180.0f / M_PI)
#include <string.h>

static Camera g_main_camera = {0};
static bool g_main_camera_initialized = false;

Camera *camera_get_main(void) {
  if (!g_main_camera_initialized) {
    camera_main_init();
  }
  return &g_main_camera;
}

void camera_main_init(void) {
  camera_init(&g_main_camera);
  g_main_camera_initialized = true;
}

void camera_init(Camera *cam) {
  if (!cam)
    return;

  memset(cam, 0, sizeof(Camera));

  cam->position[0] = 0.0f;
  cam->position[1] = 10.0f;
  cam->position[2] = 0.0f;

  cam->rotation[0] = 60.0f;
  cam->rotation[1] = 0.0f;
  cam->rotation[2] = 0.0f;

  cam->forward[0] = 0.0f;
  cam->forward[1] = -1.0f;
  cam->forward[2] = 0.0f;

  cam->orthographic_size = 5.0f;
  cam->aspect_ratio = 16.0f / 9.0f;
  cam->near_clip = 0.1f;
  cam->far_clip = 100.0f;

  xform_identity(cam->view_matrix);
  xform_identity(cam->projection_matrix);
  xform_identity(cam->vp_matrix);

  cam->matrices_dirty = true;
}

void camera_init_ortho(Camera *cam, float ortho_size, float aspect) {
  camera_init(cam);
  cam->orthographic_size = ortho_size;
  cam->aspect_ratio = aspect;
  cam->matrices_dirty = true;
}

void camera_set_position(Camera *cam, float x, float y, float z) {
  if (!cam)
    return;
  cam->position[0] = x;
  cam->position[1] = y;
  cam->position[2] = z;
  cam->matrices_dirty = true;
}

void camera_set_position_v3(Camera *cam, const Vec3 pos) {
  if (!cam || !pos)
    return;
  memcpy(cam->position, pos, sizeof(Vec3));
  cam->matrices_dirty = true;
}

void camera_set_rotation(Camera *cam, float x, float y, float z) {
  if (!cam)
    return;
  cam->rotation[0] = x;
  cam->rotation[1] = y;
  cam->rotation[2] = z;
  cam->matrices_dirty = true;
}

void camera_set_rotation_v3(Camera *cam, const Vec3 rot) {
  if (!cam || !rot)
    return;
  memcpy(cam->rotation, rot, sizeof(Vec3));
  cam->matrices_dirty = true;
}

void camera_look_at(Camera *cam, const Vec3 target, const Vec3 up) {
  if (!cam)
    return;

  float dx = target[0] - cam->position[0];
  float dy = target[1] - cam->position[1];
  float dz = target[2] - cam->position[2];

  float len = sqrtf(dx * dx + dy * dy + dz * dz);
  if (len < 0.0001f)
    return;
  dx /= len;
  dy /= len;
  dz /= len;

  float pitch = asinf(-dy);
  float yaw = atan2f(dx, dz);

  cam->rotation[0] = RAD_TO_DEG(pitch);
  cam->rotation[1] = RAD_TO_DEG(yaw);
  cam->rotation[2] = 0.0f;

  cam->forward[0] = dx;
  cam->forward[1] = dy;
  cam->forward[2] = dz;

  cam->matrices_dirty = true;
}

void camera_set_ortho_size(Camera *cam, float size) {
  if (!cam || size <= 0.0f)
    return;
  cam->orthographic_size = size;
  cam->matrices_dirty = true;
}

void camera_set_aspect(Camera *cam, float aspect) {
  if (!cam || aspect <= 0.0f)
    return;
  cam->aspect_ratio = aspect;
  cam->matrices_dirty = true;
}

void camera_set_clip_planes(Camera *cam, float near_clip, float far_clip) {
  if (!cam)
    return;
  cam->near_clip = near_clip;
  cam->far_clip = far_clip;
  cam->matrices_dirty = true;
}

static void compute_view_matrix(Camera *cam) {

  Vec3 scale = {1.0f, 1.0f, 1.0f};
  Matrix4 world_matrix;

  matrix4_from_trs(cam->position, cam->rotation, scale, world_matrix);

  matrix4_inverse(world_matrix, cam->view_matrix);
}

static void compute_projection_matrix(Camera *cam) {
  float half_height = cam->orthographic_size;
  float half_width = half_height * cam->aspect_ratio;

  float left = -half_width;
  float right = half_width;
  float bottom = -half_height;
  float top = half_height;
  float znear = cam->near_clip;
  float zfar = cam->far_clip;

  matrix4_ortho3d(left, right, bottom, top, znear, zfar,
                  cam->projection_matrix);
}

void camera_update_matrices(Camera *cam) {
  if (!cam)
    return;

  compute_view_matrix(cam);
  compute_projection_matrix(cam);

  matrix4_mul(cam->projection_matrix, cam->view_matrix, cam->vp_matrix);

  cam->matrices_dirty = false;
}

const Matrix4 *camera_get_vp_matrix(Camera *cam) {
  if (!cam)
    return NULL;

  if (cam->matrices_dirty) {
    camera_update_matrices(cam);
  }

  return (const Matrix4 *)cam->vp_matrix;
}

const Matrix4 *camera_get_view_matrix(Camera *cam) {
  if (!cam)
    return NULL;

  if (cam->matrices_dirty) {
    camera_update_matrices(cam);
  }

  return (const Matrix4 *)cam->view_matrix;
}

const Matrix4 *camera_get_projection_matrix(Camera *cam) {
  if (!cam)
    return NULL;

  if (cam->matrices_dirty) {
    camera_update_matrices(cam);
  }

  return (const Matrix4 *)cam->projection_matrix;
}

void camera_world_to_screen(Camera *cam, const Vec3 world, Vec3 screen) {
  if (!cam || !world || !screen)
    return;

  const Matrix4 *vp = camera_get_vp_matrix(cam);

  float in[4] = {world[0], world[1], world[2], 1.0f};
  float out[4];

  matrix4_mul_vec4(*vp, in, out);

  if (out[3] != 0.0f) {
    screen[0] = out[0] / out[3];
    screen[1] = out[1] / out[3];
    screen[2] = out[2] / out[3];
  } else {
    screen[0] = out[0];
    screen[1] = out[1];
    screen[2] = out[2];
  }
}

void camera_screen_to_world(Camera *cam, const Vec3 screen, Vec3 world) {
  if (!cam || !screen || !world)
    return;

  const Matrix4 *vp = camera_get_vp_matrix(cam);
  Matrix4 vp_inv;
  matrix4_inverse(*vp, vp_inv);

  float in[4] = {screen[0], screen[1], screen[2], 1.0f};
  float out[4];

  matrix4_mul_vec4(vp_inv, in, out);

  if (out[3] != 0.0f) {
    world[0] = out[0] / out[3];
    world[1] = out[1] / out[3];
    world[2] = out[2] / out[3];
  } else {
    world[0] = out[0];
    world[1] = out[1];
    world[2] = out[2];
  }
}
