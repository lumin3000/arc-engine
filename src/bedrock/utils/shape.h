#ifndef BALD_SHAPE_H
#define BALD_SHAPE_H

#include <stdbool.h>
#include "../../types.h"

typedef Vec4 Rect;

typedef struct {
    Vec2 pos;
    float radius;
} Circle;

typedef enum {
    SHAPE_TYPE_NONE = 0,
    SHAPE_TYPE_RECT,
    SHAPE_TYPE_CIRCLE,
} Shape_Type;

typedef struct {
    Shape_Type type;
    union {
        Rect rect;
        Circle circle;
    };
} Shape;

void rect_get_center(Rect rect, Vec2 out);

void rect_make_with_pos(Vec2 pos, Vec2 size, Pivot pivot, Rect out);

void rect_make_with_size(Vec2 size, Pivot pivot, Rect out);

void rect_shift(Rect rect, Vec2 amount, Rect out);

void rect_size(Rect rect, Vec2 out);

void rect_scale(Rect rect, float scale, Rect out);

void rect_scale_v2(Rect rect, Vec2 scale, Rect out);

void rect_expand(Rect rect, float amount, Rect out);

bool rect_contains(Rect rect, Vec2 point);

void circle_shift(Circle circle, Vec2 amount, Circle* out);

void shape_shift(const Shape* shape, Vec2 amount, Shape* out);

bool collide(const Shape* a, const Shape* b, Vec2 out_depth);

bool rect_collide_rect(Rect a, Rect b, Vec2 out_depth);
bool rect_collide_circle(Rect aabb, Circle circle, Vec2 out_depth);

#endif
