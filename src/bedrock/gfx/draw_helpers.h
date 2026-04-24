#ifndef BALD_DRAW_HELPERS_H
#define BALD_DRAW_HELPERS_H

#include "draw.h"
#include "text.h"

#define draw_sprite_opts(pos_, sprite_, ...) \
    draw_sprite_ex((pos_), (sprite_), (Draw_Sprite_Opts){DRAW_SPRITE_OPTS_DEFAULT, ##__VA_ARGS__})

#define draw_rect_opts(rect_, ...) \
    draw_rect_ex((rect_), (Draw_Rect_Opts){DRAW_RECT_OPTS_DEFAULT, ##__VA_ARGS__})

#define draw_rect_xform_opts(xform_, size_, ...) \
    draw_rect_xform_ex((xform_), (size_), (Draw_Rect_Xform_Opts){DRAW_RECT_XFORM_OPTS_DEFAULT, ##__VA_ARGS__})

#define draw_text_opts(pos_, text_, ...) \
    draw_text_ex((pos_), (text_), (Draw_Text_Opts){DRAW_TEXT_OPTS_DEFAULT, ##__VA_ARGS__}, NULL)

#endif
