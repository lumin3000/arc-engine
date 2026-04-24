#ifndef BALD_UTILS_H
#define BALD_UTILS_H

#include "../../types.h"
#include <math.h>
#include <stdbool.h>
#include <stdint.h>

void scale_from_pivot(Pivot pivot, Vec2 out);

void vector_from_direction(Direction dir, Vec2 out);

void cardinal_direction_offset(int i, Vec2i out);

float rotation_from_direction(Direction dir);

void rotate_vector(Vec2 vec, float angle_degrees, Vec2 out);

float angle_from_vector(Vec2 v);

void vec2_normalize(Vec2 v, Vec2 out);

void get_sprite_size(Sprite_Name sprite, Vec2 out);

void xform_identity(Matrix4 out);
void xform_translate(Vec2 pos, Matrix4 out);
void xform_rotate(float angle_degrees, Matrix4 out);
void xform_scale(Vec2 scale, Matrix4 out);

void matrix4_mul(const Matrix4 a, const Matrix4 b, Matrix4 out);

void matrix4_mul_vec2(const Matrix4 m, Vec2 v, Vec2 out);

void matrix4_mul_vec4(const Matrix4 m, const float v[4], float out[4]);

void matrix4_inverse(const Matrix4 m, Matrix4 out);

void matrix4_ortho3d(float left, float right, float bottom, float top,
                     float znear, float zfar, Matrix4 out);

void xform_translate_3d(Vec3 pos, Matrix4 out);
void xform_rotate_x(float angle_degrees, Matrix4 out);
void xform_rotate_y(float angle_degrees, Matrix4 out);
void xform_rotate_z(float angle_degrees, Matrix4 out);
void xform_scale_3d(Vec3 scale, Matrix4 out);
void matrix4_from_trs(Vec3 t, Vec3 r, Vec3 s, Matrix4 out);

float sine_breathe_alpha(float phase);

bool animate_to_target_f32(float *value, float target, float delta_t,
                           float rate, float good_enough);

void animate_to_target_v2(Vec2 value, Vec2 target, float delta_t, float rate,
                          float good_enough);

bool almost_equals(float a, float b, float epsilon);

float float_alpha(float x, float min, float max, bool clamp_result);

float rand_f32_range(float min, float max);

int rand_int(int max);

float random_sign(void);

void random_dir(Vec2 out);

void random_cardinal_dir_offset(Vec2i out);

bool pct_chance(float pct);

void hex_to_rgba(uint32_t hex, Vec4 out);
void u32_to_rgba(uint32_t val, Vec4 out);

double seconds_since_init(void);

float ms_to_s(float ms);

char *snake_case_to_pretty_name(const char *snake);

void crash_when_debug(const char *message);

void crash(const char *message);

void make_directory_if_not_exist(const char *path);

#endif
