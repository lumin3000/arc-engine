
#include "text.h"
#include "draw.h"
#include "render.h"
#include "../utils/utils.h"
#include "../utils/shape.h"
#include <string.h>
#include <math.h>
#include <assert.h>

#include "../../external/stb/stb_truetype.h"

const Draw_Text_Opts DRAW_TEXT_OPTS_DEFAULT = {
    .drop_shadow_col = {0, 0, 0, 1},
    .col = {1.0f, 1.0f, 1.0f, 1.0f},
    .scale = 1.0f,
    .pivot = Pivot_bottom_left,
    .z_layer = ZLayer_nil,
    .col_override = {1.0f, 1.0f, 1.0f, 1.0f}
};

void draw_text(
    Vec2 pos,
    const char* text,
    Vec4 drop_shadow_col,
    Vec4 col,
    float scale,
    Pivot pivot,
    ZLayer z_layer,
    Vec4 col_override
) {
    Vec2 size;
    draw_text_with_drop_shadow(pos, text, drop_shadow_col, col, scale, pivot, z_layer, col_override, size);
}

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
) {
    Vec2 offset = {1.0f * scale, -1.0f * scale};
    Vec2 shadow_pos = {pos[0] + offset[0], pos[1] + offset[1]};

    Vec4 shadow_col = {
        drop_shadow_col[0] * col[0],
        drop_shadow_col[1] * col[1],
        drop_shadow_col[2] * col[2],
        drop_shadow_col[3] * col[3]
    };

    Vec2 temp_size;
    draw_text_no_drop_shadow(shadow_pos, text, shadow_col, scale, pivot, z_layer, col_override, temp_size);
    draw_text_no_drop_shadow(pos, text, col, scale, pivot, z_layer, col_override, out_size);
}

void draw_text_no_drop_shadow(
    Vec2 pos,
    const char* text,
    Vec4 col,
    float scale,
    Pivot pivot,
    ZLayer z_layer,
    Vec4 col_override,
    Vec2 out_size
) {
    if (!text || text[0] == '\0') {
        if (out_size) {
            out_size[0] = 0.0f;
            out_size[1] = 0.0f;
        }
        return;
    }

    ZLayer old_layer = push_z_layer(z_layer != ZLayer_nil ? z_layer : draw_frame.active_z_layer);

    Vec2 total_size = {0, 0};
    int text_len = strlen(text);

    for (int i = 0; i < text_len; i++) {
        char c = text[i];

        float advance_x = 0.0f;
        float advance_y = 0.0f;
        stbtt_aligned_quad q;

        stbtt_GetBakedQuad(
            (stbtt_bakedchar*)font.char_data,
            FONT_BITMAP_W,
            FONT_BITMAP_H,
            (int)c - 32,
            &advance_x,
            &advance_y,
            &q,
            0
        );

        float size_x = fabsf(q.x0 - q.x1);
        float size_y = fabsf(q.y0 - q.y1);

        float bottom_left_y = -q.y1;
        float top_right_y = -q.y0;

        if (i == text_len - 1) {
            total_size[0] += size_x;
        } else {
            total_size[0] += advance_x;
        }

        total_size[1] = fmaxf(total_size[1], top_right_y);
    }

    Vec2 pivot_scale;
    scale_from_pivot(pivot, pivot_scale);
    Vec2 pivot_offset = {
        total_size[0] * -pivot_scale[0],
        total_size[1] * -pivot_scale[1]
    };

    float x = 0.0f;
    float y = 0.0f;

    for (int i = 0; i < text_len; i++) {
        char c = text[i];

        float advance_x = 0.0f;
        float advance_y = 0.0f;
        stbtt_aligned_quad q;

        stbtt_GetBakedQuad(
            (stbtt_bakedchar*)font.char_data,
            FONT_BITMAP_W,
            FONT_BITMAP_H,
            (int)c - 32,
            &advance_x,
            &advance_y,
            &q,
            0
        );

        Vec2 size = {fabsf(q.x0 - q.x1), fabsf(q.y0 - q.y1)};
        Vec2 bottom_left = {q.x0, -q.y1};

        Vec2 offset_to_render_at = {
            x + bottom_left[0] + pivot_offset[0],
            y + bottom_left[1] + pivot_offset[1]
        };

        Vec4 uv = {
            q.s0, q.t1,
            q.s1, q.t0
        };

        Matrix4 xform, translate_pos, scale_mat, translate_offset;
        xform_identity(xform);
        xform_translate(pos, translate_pos);

        Vec2 scale_vec = {scale, scale};
        xform_scale(scale_vec, scale_mat);
        xform_translate(offset_to_render_at, translate_offset);

        Matrix4 temp1, temp2;
        matrix4_mul(translate_pos, scale_mat, temp1);
        matrix4_mul(temp1, translate_offset, xform);

        Draw_Rect_Xform_Opts glyph_opts = DRAW_RECT_XFORM_OPTS_DEFAULT;
        memcpy(glyph_opts.uv, uv, sizeof(Vec4));
        glyph_opts.tex_index = 1;
        memcpy(glyph_opts.col, col, sizeof(Vec4));
        memcpy(glyph_opts.col_override, col_override, sizeof(Vec4));
        draw_rect_xform_ex(xform, size, glyph_opts);

        x += advance_x;
        y += -advance_y;
    }

    set_z_layer(old_layer);

    if (out_size) {
        out_size[0] = total_size[0] * scale;
        out_size[1] = total_size[1] * scale;
    }
}

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
) {

    (void)wrap_width;
    draw_text_no_drop_shadow(pos, text, col, scale, pivot, z_layer, col_override, out_size);
}

void draw_text_ex(Vec2 pos, const char* text, Draw_Text_Opts opts, Vec2 out_size) {
    draw_text_with_drop_shadow(
        pos, text,
        opts.drop_shadow_col,
        opts.col,
        opts.scale,
        opts.pivot,
        opts.z_layer,
        opts.col_override,
        out_size
    );
}
