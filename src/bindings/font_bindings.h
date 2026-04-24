
#ifndef FONT_BINDINGS_H
#define FONT_BINDINGS_H

#include "quickjs.h"

int js_init_font_module(JSContext *ctx);

void js_shutdown_font_module(void);

struct font_manager *font_bindings_get_manager(void);

#endif
