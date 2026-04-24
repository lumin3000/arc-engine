
#include "js_runtime.h"
#include "../bindings/diag_bindings.h"
#include "../bindings/draw_bindings.h"
#include "../bindings/font_bindings.h"
#include "../bindings/engine_bindings.h"
#include "../bindings/graphics_bindings.h"
#include "../bindings/input_bindings.h"
#include "../bindings/loader_buffer.h"
#include "../bindings/unified_mesh.h"
#include "../bindings/imgui_bindings.h"
#include "../bindings/batch.h"
#include "../log.h"
#include "jtask.h"
#include "jtask_api.h"
#include "message.h"
#include "quickjs-libc.h"
#include "quickjs.h"
#include "quickjs_coroutine.h"
#include "quickjs_searchpath.h"
#include "serialize.h"
#include "service.h"
#include "service_shim.h"
#if defined(_WIN32)
#include <windows.h>
#else
#include <dirent.h>
#endif
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "platform.h"

static service_id g_start_service_id = {0};
static service_id g_render_service_id = {0};
static service_id g_loader_service_id = {0};
static service_id g_game_service_id = {0};

static atomic_int g_render_service_ready = 0;

static int g_render_ctx_modules_initialized = 0;

static JSValue g_mainthread_wait_func;
static JSValue g_bootstrap_obj;
static int g_mainthread_wait_cached = 0;

static JSValue g_frame_func;
static int g_frame_func_cached = 0;

static uint64_t g_js_frame_start_us = 0;
#define JS_FRAME_TIMEOUT_US 5000000

static uint64_t get_time_us(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (uint64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}

static int js_interrupt_handler(JSRuntime *rt, void *opaque) {
  (void)rt;
  (void)opaque;
  if (g_js_frame_start_us == 0) return 0;
  uint64_t elapsed = get_time_us() - g_js_frame_start_us;
  if (elapsed > JS_FRAME_TIMEOUT_US) {
    fprintf(stderr, "\n[TIMEOUT] JS execution exceeded %d ms! Interrupting...\n",
            (int)(JS_FRAME_TIMEOUT_US / 1000));
    return 1;
  }
  return 0;
}

static int g_js_error_count = 0;
static int g_js_error_threshold = 10;

static bool is_react_sentinel_error(const char *msg) {
  if (!msg) return false;
  if (strstr(msg, "not a real error")) return true;
  if (strstr(msg, "react-stack-top-frame")) return true;
  if (strstr(msg, "selective hydration")) return true;
  return false;
}

static void arc_js_error_handler(const char *msg, const char *stack) {

  if (is_react_sentinel_error(msg)) return;

  g_js_error_count++;

  if (g_log_level >= LOG_LEVEL_NORMAL) {
    fprintf(stderr, "\n[JS_ERROR #%d] %s\n", g_js_error_count, msg);
    if (stack) {
      fprintf(stderr, "%s\n", stack);
    }
    fflush(stderr);
  }

  if (g_js_error_count >= g_js_error_threshold) {
    fprintf(stderr, "[JS_ERROR] Error threshold reached (%d), consider restart\n",
            g_js_error_count);
  }
}

int js_runtime_get_error_count(void) {
  return g_js_error_count;
}

void js_runtime_reset_error_count(void) {
  g_js_error_count = 0;
}

JSContext *g_bootstrap_ctx = NULL;
static bool g_initialized = false;
static JSRuntime *g_runtime = NULL;
static JSContext *g_context = NULL;
static JSCoroutineManager *g_coroutine_mgr = NULL;

void *js_runtime_get_context(void) {
  return (void *)g_context;
}

static char *g_bootstrap_path_override = NULL;
static char *g_main_script_path_override = NULL;

void js_runtime_set_bootstrap_path(const char *path) {
  if (g_bootstrap_path_override) free(g_bootstrap_path_override);
  g_bootstrap_path_override = path ? strdup(path) : NULL;
}

void js_runtime_set_main_script_path(const char *path) {
  if (g_main_script_path_override) free(g_main_script_path_override);
  g_main_script_path_override = path ? strdup(path) : NULL;
}

static JS_Runtime_Game_Bindings_Fn g_game_bindings_fn = NULL;

void js_runtime_set_game_bindings_registrar(JS_Runtime_Game_Bindings_Fn fn) {
  g_game_bindings_fn = fn;
}

static int eval_buf(JSContext *ctx, const void *buf, int buf_len,
                    const char *filename, int eval_flags) {
  JSValue val = JS_Eval(ctx, buf, buf_len, filename, eval_flags);
  int ret = 0;

  if (JS_IsException(val)) {
    js_std_dump_error(ctx);
    ret = -1;
  }

  JSContext *pctx;
  for (;;) {
    int err = JS_ExecutePendingJob(JS_GetRuntime(ctx), &pctx);
    if (err <= 0) {
      if (err < 0)
        js_std_dump_error(pctx);
      break;
    }
  }

  JS_FreeValue(ctx, val);
  return ret;
}

static int eval_file(JSContext *ctx, const char *filename) {
  size_t buf_len;
  uint8_t *buf = js_load_file(ctx, &buf_len, filename);
  if (!buf) {
    LOG_ERROR("Failed to load %s\n", filename);
    return -1;
  }
  int ret = eval_buf(ctx, buf, buf_len, filename, JS_EVAL_TYPE_GLOBAL);
  js_free(ctx, buf);
  return ret;
}

static JSValue js_std_loadfile(JSContext *ctx, JSValueConst this_val, int argc,
                               JSValueConst *argv) {
  if (argc < 1)
    return JS_ThrowTypeError(ctx, "loadFile requires 1 argument");

  const char *path = JS_ToCString(ctx, argv[0]);
  if (!path)
    return JS_EXCEPTION;

  size_t buf_len;
  uint8_t *buf = js_load_file(ctx, &buf_len, path);
  JS_FreeCString(ctx, path);

  if (!buf)
    return JS_NULL;

  JSValue result = JS_NewStringLen(ctx, (const char *)buf, buf_len);
  js_free(ctx, buf);
  return result;
}

static JSValue js_load_module(JSContext *ctx, JSValueConst this_val, int argc,
                              JSValueConst *argv) {
  if (argc < 1)
    return JS_ThrowTypeError(ctx, "loadModule requires at least 1 argument");

  const char *name = JS_ToCString(ctx, argv[0]);
  if (!name)
    return JS_EXCEPTION;

  const char *search_path =
      argc > 1 ? JS_ToCString(ctx, argv[1]) : "test/?.js;service/?.js";

  JSValue global = JS_GetGlobalObject(ctx);
  JSValue search_func = JS_GetPropertyStr(ctx, global, "searchPath");

  if (JS_IsUndefined(search_func)) {
    JS_FreeCString(ctx, name);
    if (argc > 1)
      JS_FreeCString(ctx, search_path);
    JS_FreeValue(ctx, global);
    return JS_ThrowReferenceError(ctx, "searchPath is not defined");
  }

  JSValue args[2] = {argv[0], JS_NewString(ctx, search_path)};
  JSValue path_result = JS_Call(ctx, search_func, global, 2, args);
  JS_FreeValue(ctx, args[1]);
  JS_FreeValue(ctx, search_func);
  JS_FreeValue(ctx, global);

  if (JS_IsException(path_result)) {
    JS_FreeCString(ctx, name);
    if (argc > 1)
      JS_FreeCString(ctx, search_path);
    return JS_EXCEPTION;
  }

  const char *file_path = JS_ToCString(ctx, path_result);
  JS_FreeValue(ctx, path_result);

  if (!file_path) {
    JS_FreeCString(ctx, name);
    if (argc > 1)
      JS_FreeCString(ctx, search_path);
    return JS_ThrowInternalError(ctx, "Module not found: %s", name);
  }

  size_t buf_len;
  uint8_t *buf = js_load_file(ctx, &buf_len, file_path);
  if (!buf) {
    JS_FreeCString(ctx, name);
    if (argc > 1)
      JS_FreeCString(ctx, search_path);
    JS_FreeCString(ctx, file_path);
    return JS_ThrowInternalError(ctx, "Failed to load file: %s", file_path);
  }

  const char *prefix = "(function() {\n";
  const char *suffix = "\n})()";
  size_t wrapped_len = strlen(prefix) + buf_len + strlen(suffix) + 1;

  char *wrapped = js_malloc(ctx, wrapped_len);
  if (!wrapped) {
    js_free(ctx, buf);
    JS_FreeCString(ctx, name);
    if (argc > 1)
      JS_FreeCString(ctx, search_path);
    JS_FreeCString(ctx, file_path);
    return JS_ThrowInternalError(ctx, "Out of memory");
  }

  sprintf(wrapped, "%s%.*s%s", prefix, (int)buf_len, buf, suffix);

  JSValue result =
      JS_Eval(ctx, wrapped, wrapped_len - 1, file_path, JS_EVAL_TYPE_GLOBAL);

  js_free(ctx, wrapped);
  js_free(ctx, buf);
  JS_FreeCString(ctx, name);
  if (argc > 1)
    JS_FreeCString(ctx, search_path);
  JS_FreeCString(ctx, file_path);

  return result;
}

static void do_inject_render_modules(void) {
  if (g_render_ctx_modules_initialized)
    return;

  struct jtask *task = jtask_get_instance();
  if (!task)
    return;

  if (g_render_service_id.id != 0) {
    JSContext *render_ctx =
        (JSContext *)service_get_context(task->services, g_render_service_id);
    if (render_ctx) {
      js_init_draw_module(render_ctx);
      js_init_graphics_module(render_ctx);
      js_init_engine_module(render_ctx);
      js_init_input_module(render_ctx);
      g_render_ctx_modules_initialized = 1;
      LOG_VERBOSE(
          "[JSRuntime] render_service context modules injected (Sync)\n");
    }
  }
}

static int g_mod_scripts_loaded = 0;

static void do_load_mod_scripts(void) {
  if (g_mod_scripts_loaded) return;

  struct jtask *task = jtask_get_instance();
  if (!task) return;
  if (g_game_service_id.id == 0) return;

  JSContext *game_ctx =
      (JSContext *)service_get_context(task->services, g_game_service_id);
  if (!game_ctx) {
    LOG_ERROR("[mod_scripts] cannot get game_service context\n");
    return;
  }

  FILE *fp = fopen("data/mods/load_order.txt", "r");
  if (!fp) {
    LOG_INFO("[mod_scripts] no load_order.txt, skipping\n");
    g_mod_scripts_loaded = 1;
    return;
  }

  char line[256];
  int total_files = 0;
  int total_mods = 0;

  while (fgets(line, sizeof(line), fp)) {

    char *p = line;
    while (*p == ' ' || *p == '\t') p++;
    size_t len = strlen(p);
    while (len > 0 && (p[len-1] == '\n' || p[len-1] == '\r' || p[len-1] == ' ')) {
      p[--len] = '\0';
    }
    if (len == 0 || p[0] == '#') continue;

    char scripts_dir[512];
    snprintf(scripts_dir, sizeof(scripts_dir), "data/mods/%s/scripts", p);

#if defined(_WIN32)
    char search_pattern[512];
    snprintf(search_pattern, sizeof(search_pattern), "%s\\*.js", scripts_dir);
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(search_pattern, &fd);
    if (hFind == INVALID_HANDLE_VALUE) continue;
    total_mods++;
    do {
      const char *fname = fd.cFileName;
      char file_path[768];
      snprintf(file_path, sizeof(file_path), "%s/%s", scripts_dir, fname);
#else
    DIR *dir = opendir(scripts_dir);
    if (!dir) continue;
    total_mods++;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
      size_t nlen = strlen(ent->d_name);
      if (nlen < 4 || strcmp(ent->d_name + nlen - 3, ".js") != 0) continue;
      char file_path[768];
      snprintf(file_path, sizeof(file_path), "%s/%s", scripts_dir, ent->d_name);
#endif

      LOG_INFO("[mod_scripts] loading %s\n", file_path);

      size_t buf_len;
      uint8_t *buf = js_load_file(game_ctx, &buf_len, file_path);
      if (!buf) {
        LOG_ERROR("[mod_scripts] failed to read %s\n", file_path);
        continue;
      }

      const char *prefix = "(function() {\n";
      const char *suffix = "\n})();";
      size_t wrapped_len = strlen(prefix) + buf_len + strlen(suffix);
      char *wrapped = malloc(wrapped_len + 1);
      if (!wrapped) {
        js_free(game_ctx, buf);
        LOG_ERROR("[mod_scripts] OOM wrapping %s\n", file_path);
        continue;
      }
      memcpy(wrapped, prefix, strlen(prefix));
      memcpy(wrapped + strlen(prefix), buf, buf_len);
      memcpy(wrapped + strlen(prefix) + buf_len, suffix, strlen(suffix));
      wrapped[wrapped_len] = '\0';
      js_free(game_ctx, buf);

      JSValue result = JS_Eval(game_ctx, wrapped, wrapped_len, file_path, JS_EVAL_TYPE_GLOBAL);
      free(wrapped);

      if (JS_IsException(result)) {
        LOG_ERROR("[mod_scripts] eval failed: %s\n", file_path);
        js_std_dump_error(game_ctx);
      } else {
        total_files++;
      }
      JS_FreeValue(game_ctx, result);
#if defined(_WIN32)
    } while (FindNextFileA(hFind, &fd));
    FindClose(hFind);
#else
    }
    closedir(dir);
#endif
  }
  fclose(fp);

  LOG_INFO("[mod_scripts] loaded %d files from %d mods\n", total_files, total_mods);
  g_mod_scripts_loaded = 1;
}

static JSValue js_message_inject_render_modules(JSContext *ctx,
                                                JSValueConst this_val, int argc,
                                                JSValueConst *argv) {
  do_inject_render_modules();
  return JS_UNDEFINED;
}

static JSValue js_message_load_mod_scripts(JSContext *ctx,
                                           JSValueConst this_val, int argc,
                                           JSValueConst *argv) {
  do_load_mod_scripts();
  return JS_UNDEFINED;
}

static JSValue js_message_now(JSContext *ctx, JSValueConst this_val, int argc,
                              JSValueConst *argv) {
  return JS_NewBigUint64(ctx, get_time_us());
}

static JSValue js_message_set_ready(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv) {
  return JS_UNDEFINED;
}

static JSValue js_message_register_service(JSContext *ctx,
                                           JSValueConst this_val, int argc,
                                           JSValueConst *argv) {
  if (argc < 2) {
    return JS_ThrowTypeError(ctx,
                             "register_service requires (name, service_id)");
  }

  const char *name = JS_ToCString(ctx, argv[0]);
  if (!name)
    return JS_EXCEPTION;

  int32_t sid;
  if (JS_ToInt32(ctx, &sid, argv[1]) < 0) {
    JS_FreeCString(ctx, name);
    return JS_EXCEPTION;
  }

  if (strcmp(name, "start") == 0) {
    g_start_service_id.id = (unsigned int)sid;
  } else if (strcmp(name, "render") == 0) {
    g_render_service_id.id = (unsigned int)sid;
  } else if (strcmp(name, "loader") == 0) {
    g_loader_service_id.id = (unsigned int)sid;
  } else if (strcmp(name, "game") == 0) {
    g_game_service_id.id = (unsigned int)sid;
  }

  JS_FreeCString(ctx, name);
  return JS_UNDEFINED;
}

static JSValue js_message_get_service(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv) {
  if (argc < 1) {
    return JS_ThrowTypeError(ctx, "get_service requires (name)");
  }

  const char *name = JS_ToCString(ctx, argv[0]);
  if (!name)
    return JS_EXCEPTION;

  int32_t sid = 0;
  if (strcmp(name, "start") == 0) {
    sid = (int32_t)g_start_service_id.id;
  } else if (strcmp(name, "render") == 0) {
    sid = (int32_t)g_render_service_id.id;
  } else if (strcmp(name, "loader") == 0) {
    sid = (int32_t)g_loader_service_id.id;
  } else if (strcmp(name, "game") == 0) {
    sid = (int32_t)g_game_service_id.id;
  }

  JS_FreeCString(ctx, name);

  if (sid == 0) {
    return JS_UNDEFINED;
  }
  return JS_NewInt32(ctx, sid);
}

static JSValue js_frame_callback(JSContext *ctx, JSValueConst this_val,
                                 int argc, JSValueConst *argv) {
  if (argc < 1) {
    return JS_ThrowTypeError(ctx, "frame requires (count)");
  }

  int32_t count;
  if (JS_ToInt32(ctx, &count, argv[0]) < 0) {
    return JS_EXCEPTION;
  }

  uint64_t t0 = get_time_us();
  uint64_t t1, t2, t3;

  int message_sent = 0;
  struct jtask *task = jtask_get_instance();
  if (task && g_render_service_id.id != 0) {

    JSValue pack_args[2] = {JS_NewString(ctx, "frame"),
                            JS_NewInt32(ctx, count)};
    int msg_sz = 0;
    void *msg_data = qjs_seri_pack(ctx, pack_args, 2, &msg_sz);
    JS_FreeValue(ctx, pack_args[0]);
    JS_FreeValue(ctx, pack_args[1]);

    if (msg_data) {
      struct message m;
      m.from.id = 0;
      m.to = g_render_service_id;
      m.session = 0;
      m.type = 1;
      m.msg = msg_data;
      m.sz = msg_sz;

      jtask_send_message(g_render_service_id, message_new(&m));

      message_sent = 1;
    }
  }

  t1 = get_time_us();

  if (!message_sent) {
    return JS_UNDEFINED;
  }

  t2 = get_time_us();

  if (g_mainthread_wait_cached) {
    JSValue result =
        JS_Call(ctx, g_mainthread_wait_func, g_bootstrap_obj, 0, NULL);
    if (JS_IsException(result)) {
      return result;
    }
    JS_FreeValue(ctx, result);
  } else {
    LOG_ERROR("mainthread_wait not cached\n");
  }

  t3 = get_time_us();

  if (g_log_level >= LOG_LEVEL_VERBOSE && count % 60 == 0) {
    fprintf(
        stderr,
        "[RTT-C] Frame %d | post:%llu wakeup:%llu wait:%llu total:%llu us\n",
        count, t1 - t0, t2 - t1, t3 - t2, t3 - t0);
  }

  return JS_UNDEFINED;
}

int js_init_message_module(JSContext *ctx) {
  JSValue global = JS_GetGlobalObject(ctx);
  JSValue msg_obj = JS_NewObject(ctx);

  JS_SetPropertyStr(ctx, msg_obj, "now",
                    JS_NewCFunction(ctx, js_message_now, "now", 0));
  JS_SetPropertyStr(
      ctx, msg_obj, "register_service",
      JS_NewCFunction(ctx, js_message_register_service, "register_service", 2));
  JS_SetPropertyStr(
      ctx, msg_obj, "get_service",
      JS_NewCFunction(ctx, js_message_get_service, "get_service", 1));
  JS_SetPropertyStr(ctx, msg_obj, "inject_render_modules",
                    JS_NewCFunction(ctx, js_message_inject_render_modules,
                                    "inject_render_modules", 0));
  JS_SetPropertyStr(ctx, msg_obj, "load_mod_scripts",
                    JS_NewCFunction(ctx, js_message_load_mod_scripts,
                                    "load_mod_scripts", 0));
  JS_SetPropertyStr(ctx, msg_obj, "set_ready",
                    JS_NewCFunction(ctx, js_message_set_ready, "set_ready", 1));

  JS_SetPropertyStr(ctx, msg_obj, "frame",
                    JS_NewCFunction(ctx, js_frame_callback, "frame", 1));

  JS_SetPropertyStr(ctx, global, "message", msg_obj);
  JS_FreeValue(ctx, global);

  js_init_loader_module(ctx);
  js_init_unified_mesh_module(ctx);
  js_init_graphics_module(ctx);
  js_init_input_module(ctx);
  js_init_draw_module(ctx);
  js_init_font_module(ctx);
  js_init_diag_module(ctx);
  js_init_imgui_module(ctx);
  js_init_batch_module(ctx);

  // Game-provided JS bindings must be registered here — before the JS
  // global scope is observed by any jtask service worker. If this runs
  // after scripts/main.js evaluation, worker services can see undefined
  // globals and silently fail.
  if (g_game_bindings_fn) {
    g_game_bindings_fn((void *)ctx);
  }

  {
    JSValue g = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, g, "__RUN_MODE__",
                      JS_NewString(ctx, g_run_mode));
    JS_FreeValue(ctx, g);
  }

  return 0;
}

int js_runtime_init(void) {
  if (g_initialized)
    return 0;

  g_runtime = JS_NewRuntime();
  if (!g_runtime) {
    LOG_ERROR("Failed to create JS runtime\n");
    return -1;
  }

  JS_SetInterruptHandler(g_runtime, js_interrupt_handler, NULL);
  JS_SetErrorCallback(arc_js_error_handler);

  g_context = JS_NewContext(g_runtime);
  if (!g_context) {
    LOG_ERROR("Failed to create JS context\n");
    JS_FreeRuntime(g_runtime);
    return -1;
  }
  g_bootstrap_ctx = g_context;

  g_coroutine_mgr = JS_NewCoroutineManager(g_runtime);
  if (g_coroutine_mgr) {
    JS_EnableCoroutines(g_context, g_coroutine_mgr);
  }

  js_init_searchpath(g_context);
  js_std_add_helpers(g_context, 0, NULL);

  {
    JSValue global = JS_GetGlobalObject(g_context);
    JSValue std_obj = JS_NewObject(g_context);
    JS_SetPropertyStr(
        g_context, std_obj, "loadFile",
        JS_NewCFunction(g_context, js_std_loadfile, "loadFile", 1));
    JS_SetPropertyStr(g_context, global, "std", std_obj);
    JS_FreeValue(g_context, global);
  }

  jtask_set_bootstrap_path(
      g_bootstrap_path_override ? g_bootstrap_path_override
                                : "external/jtask/jslib/bootstrap.js");

  if (jtask_init_bindings(g_context) < 0) {
    LOG_ERROR("Failed to initialize jtask bindings\n");
    JS_FreeContext(g_context);
    JS_FreeRuntime(g_runtime);
    return -1;
  }

  if (js_init_draw_module(g_context) < 0) {
    LOG_ERROR("Failed to initialize draw bindings\n");
    JS_FreeContext(g_context);
    JS_FreeRuntime(g_runtime);
    return -1;
  }

  if (js_init_message_module(g_context) < 0) {
    LOG_ERROR("Failed to initialize message bindings\n");
    JS_FreeContext(g_context);
    JS_FreeRuntime(g_runtime);
    return -1;
  }

  if (js_init_engine_module(g_context) < 0) {
    LOG_ERROR("Failed to initialize game bindings\n");
    JS_FreeContext(g_context);
    JS_FreeRuntime(g_runtime);
    return -1;
  }

  {
    JSValue global = JS_GetGlobalObject(g_context);
    JS_SetPropertyStr(
        g_context, global, "loadModule",
        JS_NewCFunction(g_context, js_load_module, "loadModule", 2));
    JS_FreeValue(g_context, global);
  }

  JS_SetModuleLoaderFunc(g_runtime, NULL, js_module_loader, NULL);

  const char *script_path = g_main_script_path_override
                                ? g_main_script_path_override
                                : "scripts/main.js";
  if (eval_file(g_context, script_path) < 0) {
    LOG_ERROR("Failed to execute %s\n", script_path);
    JS_FreeContext(g_context);
    JS_FreeRuntime(g_runtime);
    return -1;
  }

  g_initialized = true;

  {
    JSValue global = JS_GetGlobalObject(g_context);
    JSValue jtask_obj = JS_GetPropertyStr(g_context, global, "jtask");
    g_bootstrap_obj = JS_GetPropertyStr(g_context, jtask_obj, "bootstrap");
    g_mainthread_wait_func =
        JS_GetPropertyStr(g_context, g_bootstrap_obj, "mainthread_wait");

    if (JS_IsFunction(g_context, g_mainthread_wait_func)) {
      g_mainthread_wait_cached = 1;
      JSValue result =
          JS_Call(g_context, g_mainthread_wait_func, g_bootstrap_obj, 0, NULL);
      if (JS_IsException(result)) {
        js_std_dump_error(g_context);
      }
      JS_FreeValue(g_context, result);
    } else {
      LOG_ERROR("mainthread_wait is not a function\n");
    }

    JS_FreeValue(g_context, jtask_obj);
    JS_FreeValue(g_context, global);
  }

  {
    struct jtask *task = jtask_get_instance();
    if (task && task->external_message) {
      LOG_VERBOSE("[JSRuntime] external_message queue available\n");
    } else {
      LOG_ERROR("external_message queue not available\n");
    }
  }

  atomic_store(&g_render_service_ready, 1);
  return 0;
}

void js_runtime_shutdown(void) {
  if (!g_initialized)
    return;

  jtask_join();
  jtask_shutdown();

  if (g_coroutine_mgr) {
    JS_FreeCoroutineManager(g_coroutine_mgr);
    g_coroutine_mgr = NULL;
  }

  if (g_context) {
    JS_FreeContext(g_context);
    g_context = NULL;
  }
  if (g_runtime) {
    JS_FreeRuntime(g_runtime);
    g_runtime = NULL;
  }

  g_initialized = false;
}

void js_runtime_handle_external_command(const char *cmd) {
  if (!g_initialized || !g_context) {
    return;
  }

  struct jtask *task = jtask_get_instance();
  if (!task || g_game_service_id.id == 0) {
    LOG_VERBOSE("[stdin] game_service not ready, dropping: %s\n", cmd);
    return;
  }

  JSValue pack_args[2] = {JS_NewString(g_context, "external"),
                          JS_NewString(g_context, cmd)};
  int msg_sz = 0;
  void *msg_data = qjs_seri_pack(g_context, pack_args, 2, &msg_sz);
  JS_FreeValue(g_context, pack_args[0]);
  JS_FreeValue(g_context, pack_args[1]);

  if (!msg_data) {
    LOG_ERROR("[stdin] Failed to pack message: %s\n", cmd);
    return;
  }

  struct message m;
  m.from.id = 0;
  m.to = g_game_service_id;
  m.session = 0;
  m.type = 1;
  m.msg = msg_data;
  m.sz = msg_sz;

  jtask_send_message(g_game_service_id, message_new(&m));

  LOG_VERBOSE("[stdin] Sent to game_service: %s\n", cmd);
}

static int g_render_frame_count = 0;

void js_runtime_render(void) {
  if (!g_initialized || !g_context) {
    return;
  }

  struct jtask *task = jtask_get_instance();
  if (!task || !task->external_message) {
    return;
  }

  if (!atomic_load(&g_render_service_ready)) {
    return;
  }

  if (!g_render_ctx_modules_initialized && g_render_service_id.id != 0) {
    do_inject_render_modules();
  }

  if (!g_frame_func_cached) {
    JSValue global = JS_GetGlobalObject(g_context);
    JSValue msg_obj = JS_GetPropertyStr(g_context, global, "message");
    g_frame_func = JS_GetPropertyStr(g_context, msg_obj, "frame");

    if (JS_IsFunction(g_context, g_frame_func)) {
      g_frame_func_cached = 1;
      LOG_VERBOSE("[JSRuntime] frame function cached\n");
    } else {
      LOG_ERROR("message.frame is not a function\n");
      JS_FreeValue(g_context, g_frame_func);
    }

    JS_FreeValue(g_context, msg_obj);
    JS_FreeValue(g_context, global);
  }

  if (g_frame_func_cached) {
    ++g_render_frame_count;
    JSValue args[1] = {JS_NewInt32(g_context, g_render_frame_count)};

    g_js_frame_start_us = get_time_us();

    JSValue result = JS_Call(g_context, g_frame_func, JS_UNDEFINED, 1, args);

    g_js_frame_start_us = 0;

    if (JS_IsException(result)) {
      LOG_ERROR("[JSRuntime] JS frame exception detected!\n");
      js_std_dump_error(g_context);
    }

    JS_FreeValue(g_context, result);
    JS_FreeValue(g_context, args[0]);
  }
}

bool js_runtime_is_ready(void) { return g_initialized; }
