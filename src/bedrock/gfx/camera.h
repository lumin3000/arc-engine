
#ifndef BALD_GFX_CAMERA_H
#define BALD_GFX_CAMERA_H

#include "../../types.h"
#include <stdbool.h>

typedef struct {

  Vec3 position;
  Vec3 rotation;
  Vec3 forward;
  float orthographic_size;
  float aspect_ratio;
  float near_clip;
  float far_clip;

  Matrix4 view_matrix;
  Matrix4 projection_matrix;
  Matrix4 vp_matrix;

  bool matrices_dirty;
} Camera;

void camera_init(Camera *cam);

void camera_init_ortho(Camera *cam, float ortho_size, float aspect);

void camera_set_position(Camera *cam, float x, float y, float z);

void camera_set_position_v3(Camera *cam, const Vec3 pos);

void camera_set_rotation(Camera *cam, float x, float y, float z);

void camera_set_rotation_v3(Camera *cam, const Vec3 rot);

void camera_look_at(Camera *cam, const Vec3 target, const Vec3 up);

void camera_set_ortho_size(Camera *cam, float size);

void camera_set_aspect(Camera *cam, float aspect);

void camera_set_clip_planes(Camera *cam, float near_clip, float far_clip);

const Matrix4 *camera_get_vp_matrix(Camera *cam);

const Matrix4 *camera_get_view_matrix(Camera *cam);

const Matrix4 *camera_get_projection_matrix(Camera *cam);

void camera_update_matrices(Camera *cam);

void camera_world_to_screen(Camera *cam, const Vec3 world, Vec3 screen);

void camera_screen_to_world(Camera *cam, const Vec3 screen, Vec3 world);

Camera *camera_get_main(void);

void camera_main_init(void);

#endif
