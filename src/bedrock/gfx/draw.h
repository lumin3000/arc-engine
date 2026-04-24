
#ifndef BALD_DRAW_DRAW_H
#define BALD_DRAW_DRAW_H

#include "../../types.h"
#include "../utils/shape.h"
#include "../utils/utils.h"
#include "render.h"

void draw_sprite(Vec2 pos, Sprite_Name sprite,

                 Pivot pivot,
                 bool flip_x,
                 Vec2 draw_offset,
                 Matrix4 xform,
                 int anim_index,
                 Vec4 col,
                 Vec4 col_override,
                 ZLayer z_layer,
                 Quad_Flags flags,
                 Vec4 params,
                 float crop_top,
                 float crop_left,
                 float crop_bottom,
                 float crop_right,
                 int z_layer_queue
);

void draw_rect(Rect rect,

               Sprite_Name sprite,
               Vec4 uv,
               Vec4 outline_col,
               Vec4 col,
               Vec4 col_override,
               ZLayer z_layer,
               Quad_Flags flags,
               Vec4 params,
               float crop_top,
               float crop_left,
               float crop_bottom,
               float crop_right,
               int z_layer_queue
);

void draw_sprite_in_rect(Sprite_Name sprite, Vec2 pos, Vec2 size,
                         Matrix4 xform,
                         Vec4 col,
                         Vec4 col_override,
                         ZLayer z_layer,
                         Quad_Flags flags,
                         float pad_pct
);

void draw_rect_xform(Matrix4 xform, Vec2 size,

                     Sprite_Name sprite,
                     Vec4 uv,
                     uint8_t tex_index,
                     int anim_index,
                     Vec4 col,
                     Vec4 col_override,
                     ZLayer z_layer,
                     Quad_Flags flags,
                     Vec4 params,
                     float crop_top,
                     float crop_left,
                     float crop_bottom,
                     float crop_right,
                     int z_layer_queue
);

typedef struct {
  Pivot pivot;
  bool flip_x;
  Vec2 draw_offset;
  Matrix4 xform;
  int anim_index;
  Vec4 col;
  Vec4 col_override;
  ZLayer z_layer;
  Quad_Flags flags;
  Vec4 params;
  float crop_top;
  float crop_left;
  float crop_bottom;
  float crop_right;
  int z_layer_queue;
} Draw_Sprite_Opts;

typedef struct {
  Sprite_Name sprite;
  Vec4 uv;
  Vec4 outline_col;
  Vec4 col;
  Vec4 col_override;
  ZLayer z_layer;
  Quad_Flags flags;
  Vec4 params;
  float crop_top;
  float crop_left;
  float crop_bottom;
  float crop_right;
  int z_layer_queue;
} Draw_Rect_Opts;

typedef struct {
  Matrix4 xform;
  Vec4 col;
  Vec4 col_override;
  ZLayer z_layer;
  Quad_Flags flags;
  float pad_pct;
} Draw_Sprite_In_Rect_Opts;

typedef struct {
  Sprite_Name sprite;
  Vec4 uv;
  uint8_t tex_index;
  int anim_index;
  Vec4 col;
  Vec4 col_override;
  ZLayer z_layer;
  Quad_Flags flags;
  Vec4 params;
  float crop_top;
  float crop_left;
  float crop_bottom;
  float crop_right;
  int z_layer_queue;
} Draw_Rect_Xform_Opts;

typedef struct {
  float width;
  Vec4 col;
  ZLayer z_layer;
  int z_layer_queue;
} Draw_Line_Opts;

typedef struct {
  float width;
  Vec4 col;
  float altitude;
} Draw_Line_XZ_Opts;

extern const Draw_Sprite_Opts DRAW_SPRITE_OPTS_DEFAULT;
extern const Draw_Rect_Opts DRAW_RECT_OPTS_DEFAULT;
extern const Draw_Sprite_In_Rect_Opts DRAW_SPRITE_IN_RECT_OPTS_DEFAULT;
extern const Draw_Rect_Xform_Opts DRAW_RECT_XFORM_OPTS_DEFAULT;
extern const Draw_Line_Opts DRAW_LINE_OPTS_DEFAULT;
extern const Draw_Line_XZ_Opts DRAW_LINE_XZ_OPTS_DEFAULT;

void draw_sprite_ex(Vec2 pos, Sprite_Name sprite, Draw_Sprite_Opts opts);
void draw_rect_ex(Rect rect, Draw_Rect_Opts opts);
void draw_rect_xz_ex(Rect rect, Draw_Rect_Opts opts);
void draw_sprite_in_rect_ex(Sprite_Name sprite, Vec2 pos, Vec2 size,
                            Draw_Sprite_In_Rect_Opts opts);
void draw_rect_xform_ex(Matrix4 xform, Vec2 size, Draw_Rect_Xform_Opts opts);

void draw_line_ex(Vec2 a, Vec2 b, Draw_Line_Opts opts);
void draw_line_xz_ex(Vec2 a, Vec2 b, Draw_Line_XZ_Opts opts);

void draw_line_xz_flush(void);

#endif
