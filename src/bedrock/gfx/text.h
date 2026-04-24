#ifndef BALD_DRAW_TEXT_H
#define BALD_DRAW_TEXT_H

#include "../../types.h"
#include "../utils/utils.h"

void draw_text(
    Vec2 pos,
    const char* text,

    Vec4 drop_shadow_col,
    Vec4 col,
    float scale,
    Pivot pivot,
    ZLayer z_layer,
    Vec4 col_override
);

void draw_text_with_drop_shadow(
    Vec2 pos,
    const char* text,
    Vec4 drop_shadow_col,
    Vec4 col,
    float scale,
    Pivot pivot,
    ZLayer z_layer,
    Vec4 col_override,
    Vec2 out_size
);

void draw_text_no_drop_shadow(
    Vec2 pos,
    const char* text,
    Vec4 col,
    float scale,
    Pivot pivot,
    ZLayer z_layer,
    Vec4 col_override,
    Vec2 out_size
);

void draw_text_wrapped(
    Vec2 pos,
    const char* text,
    float wrap_width,
    Vec4 col,
    float scale,
    Pivot pivot,
    ZLayer z_layer,
    Vec4 col_override,
    Vec2 out_size
);

typedef struct {
    Vec4 drop_shadow_col;
    Vec4 col;
    float scale;
    Pivot pivot;
    ZLayer z_layer;
    Vec4 col_override;
} Draw_Text_Opts;

extern const Draw_Text_Opts DRAW_TEXT_OPTS_DEFAULT;

void draw_text_ex(Vec2 pos, const char* text, Draw_Text_Opts opts, Vec2 out_size);

#endif
