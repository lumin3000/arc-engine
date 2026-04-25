/*
 * imgui_bindings.h - Dear ImGui JS 绑定头文件
 */

#ifndef IMGUI_BINDINGS_H
#define IMGUI_BINDINGS_H

#include "quickjs.h"

// 初始化 imgui 模块，注册到 JS context (globalThis.imgui)
int js_init_imgui_module(JSContext *ctx);

#endif // IMGUI_BINDINGS_H
