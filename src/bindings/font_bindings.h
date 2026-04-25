/*
 * font_bindings.h - 字体管理器 JS 绑定
 */

#ifndef FONT_BINDINGS_H
#define FONT_BINDINGS_H

#include "quickjs.h"

// 初始化字体模块
int js_init_font_module(JSContext *ctx);

// 销毁字体模块
void js_shutdown_font_module(void);

// 获取全局字体管理器 (供 draw_bindings 使用)
struct font_manager *font_bindings_get_manager(void);

#endif // FONT_BINDINGS_H
