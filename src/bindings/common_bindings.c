
#include "common_bindings.h"
#include "batch.h"
#include "diag_bindings.h"
#include "draw_bindings.h"
#include "engine_bindings.h"
#include "font_bindings.h"
#include "graphics_bindings.h"
#include "imgui_bindings.h"
#include "input_bindings.h"
#include "loader_buffer.h"
#include "unified_mesh.h"

void arc_register_main_js_bindings(JSContext *ctx) {
  js_init_loader_module(ctx);
  js_init_unified_mesh_module(ctx);
  js_init_graphics_module(ctx);
  js_init_input_module(ctx);
  js_init_draw_module(ctx);
  js_init_font_module(ctx);
  js_init_diag_module(ctx);
  js_init_imgui_module(ctx);
  js_init_batch_module(ctx);
  // coord / game / config APIs — required in worker ctx (game_service
  // bundle's 03_player.js etc. call coord.screen_to_world). s1.6 called
  // js_init_engine_module(g_context) only for the bootstrap ctx, but
  // because js_init_message_module is weak-linked and invoked per-worker
  // by jtask, the registrar (via js_init_message_module) is the right
  // hook to reach every ctx.
  js_init_engine_module(ctx);
}

void arc_register_render_js_bindings(JSContext *ctx) {
  // render_service worker receives the same bundle as game_service, so
  // it needs the full binding set. The previous do_inject_render_modules
  // only installed draw/graphics/engine/input because jtask's weak hook
  // already installed the rest via js_init_message_module; keeping a
  // single full-set registrar here is simpler and idempotent.
  arc_register_main_js_bindings(ctx);
}
