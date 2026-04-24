
#include "shape.h"
#include "utils.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

void rect_get_center(Rect rect, Vec2 out) {
    Vec2 min = {rect[0], rect[1]};
    Vec2 max = {rect[2], rect[3]};
    out[0] = min[0] + 0.5f * (max[0] - min[0]);
    out[1] = min[1] + 0.5f * (max[1] - min[1]);
}

void rect_make_with_pos(Vec2 pos, Vec2 size, Pivot pivot, Rect out) {

    out[0] = 0.0f;
    out[1] = 0.0f;
    out[2] = size[0];
    out[3] = size[1];

    Vec2 pivot_scale;
    scale_from_pivot(pivot, pivot_scale);

    Vec2 shift_amount = {
        pos[0] - pivot_scale[0] * size[0],
        pos[1] - pivot_scale[1] * size[1]
    };

    Rect temp;
    memcpy(temp, out, sizeof(Rect));
    rect_shift(temp, shift_amount, out);
}

void rect_make_with_size(Vec2 size, Pivot pivot, Rect out) {
    Vec2 zero = {0.0f, 0.0f};
    rect_make_with_pos(zero, size, pivot, out);
}

void rect_shift(Rect rect, Vec2 amount, Rect out) {
    out[0] = rect[0] + amount[0];
    out[1] = rect[1] + amount[1];
    out[2] = rect[2] + amount[0];
    out[3] = rect[3] + amount[1];
}

void rect_size(Rect rect, Vec2 out) {
    out[0] = fabsf(rect[0] - rect[2]);
    out[1] = fabsf(rect[1] - rect[3]);
}

void rect_scale(Rect rect, float scale, Rect out) {

    Vec2 origin = {rect[0], rect[1]};

    Vec2 neg_origin = {-origin[0], -origin[1]};
    Rect temp;
    rect_shift(rect, neg_origin, temp);

    Vec2 zw = {temp[2], temp[3]};
    Vec2 scaled_zw = {zw[0] * scale, zw[1] * scale};
    Vec2 scale_amount = {
        scaled_zw[0] - zw[0],
        scaled_zw[1] - zw[1]
    };

    temp[0] -= scale_amount[0] / 2.0f;
    temp[1] -= scale_amount[1] / 2.0f;
    temp[2] += scale_amount[0] / 2.0f;
    temp[3] += scale_amount[1] / 2.0f;

    rect_shift(temp, origin, out);
}

void rect_scale_v2(Rect rect, Vec2 scale, Rect out) {

    Vec2 origin = {rect[0], rect[1]};

    Vec2 neg_origin = {-origin[0], -origin[1]};
    Rect temp;
    rect_shift(rect, neg_origin, temp);

    Vec2 zw = {temp[2], temp[3]};
    Vec2 scaled_zw = {zw[0] * scale[0], zw[1] * scale[1]};
    Vec2 scale_amount = {
        scaled_zw[0] - zw[0],
        scaled_zw[1] - zw[1]
    };

    temp[0] -= scale_amount[0] / 2.0f;
    temp[1] -= scale_amount[1] / 2.0f;
    temp[2] += scale_amount[0] / 2.0f;
    temp[3] += scale_amount[1] / 2.0f;

    rect_shift(temp, origin, out);
}

void rect_expand(Rect rect, float amount, Rect out) {
    out[0] = rect[0] - amount;
    out[1] = rect[1] - amount;
    out[2] = rect[2] + amount;
    out[3] = rect[3] + amount;
}

bool rect_contains(Rect rect, Vec2 point) {
    return (point[0] >= rect[0]) && (point[0] <= rect[2]) &&
           (point[1] >= rect[1]) && (point[1] <= rect[3]);
}

void circle_shift(Circle circle, Vec2 amount, Circle* out) {
    out->pos[0] = circle.pos[0] + amount[0];
    out->pos[1] = circle.pos[1] + amount[1];
    out->radius = circle.radius;
}

void shape_shift(const Shape* shape, Vec2 amount, Shape* out) {
    if (!shape || shape->type == SHAPE_TYPE_NONE) {
        out->type = SHAPE_TYPE_NONE;
        return;
    }

    if (amount[0] == 0.0f && amount[1] == 0.0f) {
        *out = *shape;
        return;
    }

    out->type = shape->type;

    switch (shape->type) {
        case SHAPE_TYPE_RECT:
            rect_shift(shape->rect, amount, out->rect);
            break;

        case SHAPE_TYPE_CIRCLE:
            circle_shift(shape->circle, amount, &out->circle);
            break;

        default:
            fprintf(stderr, "ERROR: unsupported shape shift type %d\n", shape->type);
            out->type = SHAPE_TYPE_NONE;
            break;
    }
}

bool collide(const Shape* a, const Shape* b, Vec2 out_depth) {

    out_depth[0] = 0.0f;
    out_depth[1] = 0.0f;

    if (!a || !b || a->type == SHAPE_TYPE_NONE || b->type == SHAPE_TYPE_NONE) {
        return false;
    }

    switch (a->type) {
        case SHAPE_TYPE_RECT:
            switch (b->type) {
                case SHAPE_TYPE_RECT:
                    return rect_collide_rect(a->rect, b->rect, out_depth);

                case SHAPE_TYPE_CIRCLE:
                    return rect_collide_circle(a->rect, b->circle, out_depth);

                default:
                    break;
            }
            break;

        case SHAPE_TYPE_CIRCLE:
            if (b->type == SHAPE_TYPE_RECT) {
                return rect_collide_circle(b->rect, a->circle, out_depth);
            }
            break;

        default:
            break;
    }

    fprintf(stderr, "ERROR: unsupported shape collision types %d against %d\n",
            a->type, b->type);
    return false;
}

bool rect_collide_circle(Rect aabb, Circle circle, Vec2 out_depth) {

    Vec2 closest_point = {
        fmaxf(aabb[0], fminf(circle.pos[0], aabb[2])),
        fmaxf(aabb[1], fminf(circle.pos[1], aabb[3]))
    };

    Vec2 diff = {
        closest_point[0] - circle.pos[0],
        closest_point[1] - circle.pos[1]
    };
    float distance = sqrtf(diff[0] * diff[0] + diff[1] * diff[1]);

    out_depth[0] = 0.0f;
    out_depth[1] = 0.0f;

    return distance <= circle.radius;
}

bool rect_collide_rect(Rect a, Rect b, Vec2 out_depth) {

    float dx = ((a[2] + a[0]) / 2.0f) - ((b[2] + b[0]) / 2.0f);
    float dy = ((a[3] + a[1]) / 2.0f) - ((b[3] + b[1]) / 2.0f);

    float overlap_x = ((a[2] - a[0]) / 2.0f) + ((b[2] - b[0]) / 2.0f) - fabsf(dx);
    float overlap_y = ((a[3] - a[1]) / 2.0f) + ((b[3] - b[1]) / 2.0f) - fabsf(dy);

    if (overlap_x <= 0.0f || overlap_y <= 0.0f) {
        out_depth[0] = 0.0f;
        out_depth[1] = 0.0f;
        return false;
    }

    out_depth[0] = 0.0f;
    out_depth[1] = 0.0f;

    if (overlap_x < overlap_y) {
        out_depth[0] = (dx > 0.0f) ? overlap_x : -overlap_x;
    } else {
        out_depth[1] = (dy > 0.0f) ? overlap_y : -overlap_y;
    }

    return true;
}
