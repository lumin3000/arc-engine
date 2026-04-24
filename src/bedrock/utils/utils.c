
#include "utils.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <direct.h>
#include <sys/stat.h>
#include <windows.h>
#define mkdir(path, mode) _mkdir(path)
#else
#include <sys/stat.h>
#endif
#include "../platform.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define DEG_TO_RAD(deg) ((deg) * M_PI / 180.0f)
#define RAD_TO_DEG(rad) ((rad) * 180.0f / M_PI)

void scale_from_pivot(Pivot pivot, Vec2 out) {
  switch (pivot) {
  case Pivot_bottom_left:
    out[0] = 0.0f;
    out[1] = 0.0f;
    break;
  case Pivot_bottom_center:
    out[0] = 0.5f;
    out[1] = 0.0f;
    break;
  case Pivot_bottom_right:
    out[0] = 1.0f;
    out[1] = 0.0f;
    break;
  case Pivot_center_left:
    out[0] = 0.0f;
    out[1] = 0.5f;
    break;
  case Pivot_center_center:
    out[0] = 0.5f;
    out[1] = 0.5f;
    break;
  case Pivot_center_right:
    out[0] = 1.0f;
    out[1] = 0.5f;
    break;
  case Pivot_top_left:
    out[0] = 0.0f;
    out[1] = 1.0f;
    break;
  case Pivot_top_center:
    out[0] = 0.5f;
    out[1] = 1.0f;
    break;
  case Pivot_top_right:
    out[0] = 1.0f;
    out[1] = 1.0f;
    break;
  default:
    out[0] = 0.0f;
    out[1] = 0.0f;
    break;
  }
}

void cardinal_direction_offset(int i, Vec2i out) {
  switch (i) {
  case 0:
    out[0] = 0;
    out[1] = 1;
    break;
  case 1:
    out[0] = 1;
    out[1] = 0;
    break;
  case 2:
    out[0] = 0;
    out[1] = -1;
    break;
  case 3:
    out[0] = -1;
    out[1] = 0;
    break;
  default:
    fprintf(stderr, "ERROR: unknown direction %d\n", i);
    out[0] = 0;
    out[1] = 0;
    break;
  }
}

void vector_from_direction(Direction dir, Vec2 out) {
  Vec2i offset;
  cardinal_direction_offset((int)dir, offset);
  out[0] = (float)offset[0];
  out[1] = (float)offset[1];
}

float rotation_from_direction(Direction dir) {

  switch (dir) {
  case Direction_north:
    return 0.0f;
  case Direction_east:
    return 90.0f;
  case Direction_south:
    return 180.0f;
  case Direction_west:
    return 270.0f;
  default:
    return 0.0f;
  }
}

void rotate_vector(Vec2 vec, float angle_degrees, Vec2 out) {
  float rad = DEG_TO_RAD(angle_degrees);
  float c = cosf(rad);
  float s = sinf(rad);

  float x = vec[0] * c - vec[1] * s;
  float y = vec[0] * s + vec[1] * c;

  out[0] = x;
  out[1] = y;
}

float angle_from_vector(Vec2 v) { return RAD_TO_DEG(atan2f(v[1], v[0])); }

void vec2_normalize(Vec2 v, Vec2 out) {
  float len = sqrtf(v[0] * v[0] + v[1] * v[1]);
  if (len > 0.0f) {
    out[0] = v[0] / len;
    out[1] = v[1] / len;
  } else {
    out[0] = 0.0f;
    out[1] = 0.0f;
  }
}

void get_sprite_size(Sprite_Name sprite, Vec2 out) {

  extern void get_sprite_size_render(Sprite_Name sprite, Vec2 out);
  get_sprite_size_render(sprite, out);
}

void xform_identity(Matrix4 out) {
  memset(out, 0, sizeof(Matrix4));
  out[0] = out[5] = out[10] = out[15] = 1.0f;
}

void xform_translate(Vec2 pos, Matrix4 out) {
  xform_identity(out);
  out[12] = pos[0];
  out[13] = pos[1];
  out[14] = 0.0f;
}

void xform_rotate(float angle_degrees, Matrix4 out) {
  float rad = DEG_TO_RAD(angle_degrees);
  float c = cosf(rad);
  float s = sinf(rad);

  xform_identity(out);

  out[0] = c;
  out[1] = s;
  out[4] = -s;
  out[5] = c;
}

void xform_scale(Vec2 scale, Matrix4 out) {
  xform_identity(out);
  out[0] = scale[0];
  out[5] = scale[1];
  out[10] = 1.0f;
}

void matrix4_mul(const Matrix4 a, const Matrix4 b, Matrix4 out) {
  Matrix4 temp;

  for (int row = 0; row < 4; row++) {
    for (int col = 0; col < 4; col++) {
      float sum = 0.0f;
      for (int k = 0; k < 4; k++) {
        sum += a[k * 4 + row] * b[col * 4 + k];
      }
      temp[col * 4 + row] = sum;
    }
  }

  memcpy(out, temp, sizeof(Matrix4));
}

void matrix4_mul_vec2(const Matrix4 m, Vec2 v, Vec2 out) {

  float x = m[0] * v[0] + m[4] * v[1] + m[8] * 0.0f + m[12] * 1.0f;
  float y = m[1] * v[0] + m[5] * v[1] + m[9] * 0.0f + m[13] * 1.0f;

  out[0] = x;
  out[1] = y;
}

void matrix4_mul_vec4(const Matrix4 m, const float v[4], float out[4]) {
  float temp[4];
  temp[0] = m[0] * v[0] + m[4] * v[1] + m[8] * v[2] + m[12] * v[3];
  temp[1] = m[1] * v[0] + m[5] * v[1] + m[9] * v[2] + m[13] * v[3];
  temp[2] = m[2] * v[0] + m[6] * v[1] + m[10] * v[2] + m[14] * v[3];
  temp[3] = m[3] * v[0] + m[7] * v[1] + m[11] * v[2] + m[15] * v[3];

  memcpy(out, temp, 4 * sizeof(float));
}

void xform_translate_3d(Vec3 pos, Matrix4 out) {
  xform_identity(out);
  out[12] = pos[0];
  out[13] = pos[1];
  out[14] = pos[2];
}

void xform_rotate_x(float angle_degrees, Matrix4 out) {
  float rad = DEG_TO_RAD(angle_degrees);
  float c = cosf(rad);
  float s = sinf(rad);
  xform_identity(out);
  out[5] = c;
  out[6] = s;
  out[9] = -s;
  out[10] = c;
}

void xform_rotate_y(float angle_degrees, Matrix4 out) {
  float rad = DEG_TO_RAD(angle_degrees);
  float c = cosf(rad);
  float s = sinf(rad);
  xform_identity(out);
  out[0] = c;
  out[2] = -s;
  out[8] = s;
  out[10] = c;
}

void xform_rotate_z(float angle_degrees, Matrix4 out) {
  xform_rotate(angle_degrees, out);
}

void xform_scale_3d(Vec3 scale, Matrix4 out) {
  xform_identity(out);
  out[0] = scale[0];
  out[5] = scale[1];
  out[10] = scale[2];
}

void matrix4_from_trs(Vec3 t, Vec3 r, Vec3 s, Matrix4 out) {

  Matrix4 mt, mrx, mry, mrz, ms;

  xform_scale_3d(s, ms);

  xform_rotate_z(r[2], mrz);

  xform_rotate_x(r[0], mrx);

  xform_rotate_y(r[1], mry);

  xform_translate_3d(t, mt);

  Matrix4 r_yx;
  matrix4_mul(mry, mrx, r_yx);

  Matrix4 rot;
  matrix4_mul(r_yx, mrz, rot);

  Matrix4 tr;
  matrix4_mul(mt, rot, tr);

  matrix4_mul(tr, ms, out);
}

void matrix4_ortho3d(float left, float right, float bottom, float top,
                     float znear, float zfar, Matrix4 out) {
  memset(out, 0, sizeof(Matrix4));

  out[0] = 2.0f / (right - left);
  out[5] = 2.0f / (top - bottom);
  out[10] = 2.0f / (zfar - znear);
  out[12] = -(right + left) / (right - left);
  out[13] = -(top + bottom) / (top - bottom);
  out[14] = -(zfar + znear) / (zfar - znear);
  out[15] = 1.0f;

  out[2] = -out[2];
  out[6] = -out[6];
  out[10] = -out[10];
  out[14] = -out[14];
}

void matrix4_inverse(const Matrix4 m, Matrix4 out) {
  float inv[16];
  float det;

  inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] +
           m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];

  inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] -
           m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];

  inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] +
           m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];

  inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] -
            m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];

  inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] -
           m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];

  inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] +
           m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];

  inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] -
           m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];

  inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] +
            m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];

  inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] +
           m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];

  inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] -
           m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];

  inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] +
            m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];

  inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] -
            m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];

  inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] -
           m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];

  inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] +
           m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];

  inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] -
            m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];

  inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] +
            m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];

  det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];

  if (fabsf(det) < 1e-10f) {

    xform_identity(out);
    return;
  }

  det = 1.0f / det;

  for (int i = 0; i < 16; i++) {
    out[i] = inv[i] * det;
  }
}

float sine_breathe_alpha(float phase) {
  return (sinf((phase - 0.25f) * 2.0f * M_PI) / 2.0f) + 0.5f;
}

bool animate_to_target_f32(float *value, float target, float delta_t,
                           float rate, float good_enough) {
  *value += (target - *value) * (1.0f - powf(2.0f, -rate * delta_t));

  if (almost_equals(*value, target, good_enough)) {
    *value = target;
    return true;
  }
  return false;
}

void animate_to_target_v2(Vec2 value, Vec2 target, float delta_t, float rate,
                          float good_enough) {
  animate_to_target_f32(&value[0], target[0], delta_t, rate, good_enough);
  animate_to_target_f32(&value[1], target[1], delta_t, rate, good_enough);
}

bool almost_equals(float a, float b, float epsilon) {
  return fabsf(a - b) <= epsilon;
}

float float_alpha(float x, float min, float max, bool clamp_result) {
  float res = (x - min) / (max - min);
  if (clamp_result) {
    if (res < 0.0f)
      res = 0.0f;
    if (res > 1.0f)
      res = 1.0f;
  }
  return res;
}

float rand_f32_range(float min, float max) {
  float scale = rand() / (float)RAND_MAX;
  return min + scale * (max - min);
}

int rand_int(int max) { return rand() % (max + 1); }

float random_sign(void) { return (rand() % 2 == 0) ? 1.0f : -1.0f; }

void random_dir(Vec2 out) {
  float x = rand_f32_range(-1.0f, 1.0f);
  float y = rand_f32_range(-1.0f, 1.0f);

  float len = sqrtf(x * x + y * y);
  if (len > 0.0f) {
    out[0] = x / len;
    out[1] = y / len;
  } else {
    out[0] = 1.0f;
    out[1] = 0.0f;
  }
}

void random_cardinal_dir_offset(Vec2i out) {
  out[0] = rand_int(2) - 1;
  out[1] = rand_int(2) - 1;
}

bool pct_chance(float pct) { return rand_f32_range(0.0f, 1.0f) < pct; }

void hex_to_rgba(uint32_t hex, Vec4 out) {
  out[0] = ((hex & 0xff000000) >> 24) / 255.0f;
  out[1] = ((hex & 0x00ff0000) >> 16) / 255.0f;
  out[2] = ((hex & 0x0000ff00) >> 8) / 255.0f;
  out[3] = (hex & 0x000000ff) / 255.0f;
}

void u32_to_rgba(uint32_t val, Vec4 out) { hex_to_rgba(val, out); }

#ifdef _WIN32
static LARGE_INTEGER init_time = {0};
static LARGE_INTEGER frequency = {0};

double seconds_since_init(void) {
  if (frequency.QuadPart == 0) {
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&init_time);
    return 0.0;
  }

  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);

  return (double)(now.QuadPart - init_time.QuadPart) /
         (double)frequency.QuadPart;
}
#else
static struct timeval init_time = {0, 0};

double seconds_since_init(void) {
  if (init_time.tv_sec == 0 && init_time.tv_usec == 0) {
    gettimeofday(&init_time, NULL);
    return 0.0;
  }

  struct timeval now;
  gettimeofday(&now, NULL);

  double elapsed = (now.tv_sec - init_time.tv_sec);
  elapsed += (now.tv_usec - init_time.tv_usec) / 1000000.0;

  return elapsed;
}
#endif

float ms_to_s(float ms) { return ms / 1000.0f; }

char *snake_case_to_pretty_name(const char *snake) {
  if (!snake)
    return NULL;

  size_t len = strlen(snake);
  char *result = (char *)malloc(len + 1);
  if (!result)
    return NULL;

  strcpy(result, snake);

  for (size_t i = 0; i < len; i++) {
    if (result[i] == '_') {
      result[i] = ' ';
      if (i + 1 < len) {
        result[i + 1] = toupper(result[i + 1]);
      }
    }
  }

  if (len > 0) {
    result[0] = toupper(result[0]);
  }

  return result;
}

void crash_when_debug(const char *message) {
#ifdef DEBUG
  fprintf(stderr, "FATAL ERROR: %s\n", message);
  abort();
#else
  fprintf(stderr, "ERROR: %s\n", message);
#endif
}

void crash(const char *message) {
  fprintf(stderr, "FATAL: %s\n", message);
  abort();
}

void make_directory_if_not_exist(const char *path) {
  struct stat st = {0};

  if (stat(path, &st) == -1) {
    if (mkdir(path, 0755) != 0) {
      fprintf(stderr, "ERROR: Failed to create directory %s: %s\n", path,
              strerror(errno));
    }
  }
}
