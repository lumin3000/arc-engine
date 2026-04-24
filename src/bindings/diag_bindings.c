
#include "quickjs.h"
#include "../bedrock/gfx/render_diagnostics.h"
#include "../bedrock/gfx/render.h"
#include "../../external/sokol/c/sokol_gfx.h"
#include <stdio.h>

static JSValue js_diag_dump(JSContext *ctx, JSValueConst this_val, int argc,
                            JSValueConst *argv) {
  (void)ctx;
  (void)this_val;
  (void)argc;
  (void)argv;

  render_diag_dump();
  return JS_UNDEFINED;
}

static JSValue js_diag_request_dump(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv) {
  (void)ctx;
  (void)this_val;
  (void)argc;
  (void)argv;

  render_diag_request_dump();
  return JS_UNDEFINED;
}

static JSValue js_diag_set_continuous(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv) {
  if (argc < 1)
    return JS_ThrowTypeError(ctx, "setContinuous requires enabled argument");

  int enabled = JS_ToBool(ctx, argv[0]);
  render_diag_set_continuous(enabled);
  return JS_UNDEFINED;
}

static JSValue js_diag_set_filter(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv) {
  const char *filter = NULL;

  if (argc >= 1 && !JS_IsNull(argv[0]) && !JS_IsUndefined(argv[0])) {
    filter = JS_ToCString(ctx, argv[0]);
  }

  render_diag_set_filter(filter);

  if (filter)
    JS_FreeCString(ctx, filter);

  return JS_UNDEFINED;
}

static JSValue js_diag_set_issues_only(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv) {
  if (argc < 1)
    return JS_ThrowTypeError(ctx, "setIssuesOnly requires enabled argument");

  int enabled = JS_ToBool(ctx, argv[0]);
  render_diag_set_issues_only(enabled);
  return JS_UNDEFINED;
}

static JSValue js_diag_set_rq_range(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv) {
  if (argc < 2)
    return JS_ThrowTypeError(ctx, "setRenderQueueRange requires min, max");

  int32_t min_rq, max_rq;
  if (JS_ToInt32(ctx, &min_rq, argv[0]) < 0)
    return JS_EXCEPTION;
  if (JS_ToInt32(ctx, &max_rq, argv[1]) < 0)
    return JS_EXCEPTION;

  render_diag_set_render_queue_range(min_rq, max_rq);
  return JS_UNDEFINED;
}

static JSValue js_diag_clear_filters(JSContext *ctx, JSValueConst this_val,
                                     int argc, JSValueConst *argv) {
  (void)ctx;
  (void)this_val;
  (void)argc;
  (void)argv;

  render_diag_clear_filters();
  return JS_UNDEFINED;
}

static JSValue js_diag_num_draw(JSContext *ctx, JSValueConst this_val,
                                int argc, JSValueConst *argv) {
  (void)this_val;
  (void)argc;
  (void)argv;
  sg_stats stats = sg_query_stats();
  return JS_NewInt32(ctx, (int32_t)stats.prev_frame.num_draw);
}

static JSValue js_diag_get_stats(JSContext *ctx, JSValueConst this_val,
                                 int argc, JSValueConst *argv) {
  (void)this_val;
  (void)argc;
  (void)argv;

  JSValue obj = JS_NewObject(ctx);
  JS_SetPropertyStr(ctx, obj, "entryCount",
                    JS_NewInt32(ctx, render_diag_get_entry_count()));
  JS_SetPropertyStr(ctx, obj, "isEnabled",
                    JS_NewBool(ctx, render_diag_is_enabled()));
  return obj;
}

static JSValue js_diag_dc_breakdown(JSContext *ctx, JSValueConst this_val,
                                     int argc, JSValueConst *argv) {
  (void)this_val;
  (void)argc;
  (void)argv;

  JSValue obj = JS_NewObject(ctx);
  JS_SetPropertyStr(ctx, obj, "mesh0",
                    JS_NewInt32(ctx, g_dc_breakdown_prev.mesh_rq_0_4000));
  JS_SetPropertyStr(ctx, obj, "quads",
                    JS_NewInt32(ctx, g_dc_breakdown_prev.quad_batches));
  JS_SetPropertyStr(ctx, obj, "tris",
                    JS_NewInt32(ctx, g_dc_breakdown_prev.tri_meshes));
  JS_SetPropertyStr(ctx, obj, "mesh4k",
                    JS_NewInt32(ctx, g_dc_breakdown_prev.mesh_rq_4000_plus));
  JS_SetPropertyStr(ctx, obj, "inst",
                    JS_NewInt32(ctx, g_dc_breakdown_prev.instanced));
  JS_SetPropertyStr(ctx, obj, "imgui",
                    JS_NewInt32(ctx, g_dc_breakdown_prev.imgui));
  JS_SetPropertyStr(ctx, obj, "sdf",
                    JS_NewInt32(ctx, g_dc_breakdown_prev.sdf_text));
  JS_SetPropertyStr(ctx, obj, "total",
                    JS_NewInt32(ctx, g_dc_breakdown_prev.total));
  return obj;
}

void diag_handle_stdin_command(const char *action, JSContext *ctx,
                               JSValueConst params) {
  if (!action)
    return;

  if (strcmp(action, "dump") == 0) {
    render_diag_dump();
  } else if (strcmp(action, "start") == 0) {
    render_diag_set_continuous(true);
  } else if (strcmp(action, "stop") == 0) {
    render_diag_set_continuous(false);
  } else if (strcmp(action, "filter") == 0) {

    JSValue name_val = JS_GetPropertyStr(ctx, params, "name");
    if (!JS_IsUndefined(name_val) && !JS_IsNull(name_val)) {
      const char *name = JS_ToCString(ctx, name_val);
      render_diag_set_filter(name);
      if (name)
        JS_FreeCString(ctx, name);
    } else {
      render_diag_set_filter(NULL);
    }
    JS_FreeValue(ctx, name_val);
  } else if (strcmp(action, "issues_only") == 0) {
    JSValue val = JS_GetPropertyStr(ctx, params, "value");
    int enabled = JS_ToBool(ctx, val);
    render_diag_set_issues_only(enabled);
    JS_FreeValue(ctx, val);
  } else if (strcmp(action, "clear") == 0) {
    render_diag_clear_filters();
  } else {
    fprintf(stderr, "[diag] Unknown action: %s\n", action);
  }
}

int js_init_diag_module(JSContext *ctx) {
  JSValue global = JS_GetGlobalObject(ctx);
  JSValue obj = JS_NewObject(ctx);

  JS_SetPropertyStr(ctx, obj, "dump",
                    JS_NewCFunction(ctx, js_diag_dump, "dump", 0));
  JS_SetPropertyStr(ctx, obj, "requestDump",
                    JS_NewCFunction(ctx, js_diag_request_dump, "requestDump", 0));
  JS_SetPropertyStr(ctx, obj, "setContinuous",
                    JS_NewCFunction(ctx, js_diag_set_continuous, "setContinuous", 1));
  JS_SetPropertyStr(ctx, obj, "setFilter",
                    JS_NewCFunction(ctx, js_diag_set_filter, "setFilter", 1));
  JS_SetPropertyStr(ctx, obj, "setIssuesOnly",
                    JS_NewCFunction(ctx, js_diag_set_issues_only, "setIssuesOnly", 1));
  JS_SetPropertyStr(ctx, obj, "setRenderQueueRange",
                    JS_NewCFunction(ctx, js_diag_set_rq_range, "setRenderQueueRange", 2));
  JS_SetPropertyStr(ctx, obj, "clearFilters",
                    JS_NewCFunction(ctx, js_diag_clear_filters, "clearFilters", 0));
  JS_SetPropertyStr(ctx, obj, "getStats",
                    JS_NewCFunction(ctx, js_diag_get_stats, "getStats", 0));
  JS_SetPropertyStr(ctx, obj, "numDraw",
                    JS_NewCFunction(ctx, js_diag_num_draw, "numDraw", 0));
  JS_SetPropertyStr(ctx, obj, "dcBreakdown",
                    JS_NewCFunction(ctx, js_diag_dc_breakdown, "dcBreakdown", 0));

  JS_SetPropertyStr(ctx, global, "diag", obj);
  JS_FreeValue(ctx, global);

  fprintf(stderr, "[diag_bindings] module loaded\n");
  return 0;
}
