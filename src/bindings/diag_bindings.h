
#ifndef DIAG_BINDINGS_H
#define DIAG_BINDINGS_H

#include "quickjs.h"

int js_init_diag_module(JSContext *ctx);

void diag_handle_stdin_command(const char *action, JSContext *ctx,
                               JSValueConst params);

#endif
