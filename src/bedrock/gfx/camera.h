
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
  // > 0: orbit mode. position (x, z) is the target on the ground plane y = 0 and the eye sits
  // orbit_dist back from it, against the view direction given by rotation; 0: position is the eye.
  float orbit_dist;
  // world y is scaled by this before viewing (0 = 1). Lets a consumer whose world y is a layer
  // order rather than a height keep those layers nearly flat under a tilted camera.
  float world_y_scale;
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
// orbit mode (see Camera.orbit_dist); dist <= 0 turns it off
void camera_set_orbit(Camera *cam, float dist);
// camera-to-world transform (eye placement including orbit mode); the view matrix is its inverse
void camera_get_world_matrix(Camera *cam, Matrix4 out);
void camera_set_world_y_scale(Camera *cam, float s);

const Matrix4 *camera_get_vp_matrix(Camera *cam);

const Matrix4 *camera_get_view_matrix(Camera *cam);

const Matrix4 *camera_get_projection_matrix(Camera *cam);

void camera_update_matrices(Camera *cam);

void camera_world_to_screen(Camera *cam, const Vec3 world, Vec3 screen);

void camera_screen_to_world(Camera *cam, const Vec3 screen, Vec3 world);

// Screen-pixel → ground-plane (y=0) world point. Shared by the
// coord.screen_to_world binding and the mouse-anchored zoom path so both
// use identical unprojection math.
void camera_screen_to_world_ground(Camera *cam, float sx, float sy,
                                   float win_w, float win_h, float *out_x,
                                   float *out_z);

Camera *camera_get_main(void);

void camera_main_init(void);

#endif
