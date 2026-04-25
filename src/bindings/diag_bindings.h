/*
 * diag_bindings.h - 渲染诊断系统 JS 绑定
 */

#ifndef DIAG_BINDINGS_H
#define DIAG_BINDINGS_H

#include "quickjs.h"

// 初始化 diag 模块
int js_init_diag_module(JSContext *ctx);

// 处理 stdin 命令
void diag_handle_stdin_command(const char *action, JSContext *ctx,
                               JSValueConst params);

#endif // DIAG_BINDINGS_H
