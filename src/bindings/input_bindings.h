// input_bindings.h - 输入系统 JS 绑定
#ifndef INPUT_BINDINGS_H
#define INPUT_BINDINGS_H

#include "quickjs.h"

// 初始化 input 模块
int js_init_input_module(JSContext *ctx);

#endif // INPUT_BINDINGS_H
