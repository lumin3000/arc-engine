// graphics_bindings.h - Graphics API JS 绑定
#ifndef GRAPHICS_BINDINGS_H
#define GRAPHICS_BINDINGS_H

#include "quickjs.h"
#include "sokol_gfx.h"
int graphics_register_render_texture(sg_image image, sg_view view);

// 初始化 graphics 模块
int js_init_graphics_module(JSContext *ctx);

#endif // GRAPHICS_BINDINGS_H
