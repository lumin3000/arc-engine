
#include "engine_bindings.h"
#include "../log.h"
#include "bedrock/engine_state.h"
#include "bedrock/gfx/camera.h"
#include "bedrock/gfx/render.h"
#include "bedrock/helpers.h"
#include "bedrock/utils/utils.h"
#include "types.h"
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

static JSValue js_coord_push_world_space(JSContext *js_ctx,
                                         JSValueConst this_val, int argc,
                                         JSValueConst *argv) {
  (void)js_ctx;
  (void)this_val;
  (void)argc;
  (void)argv;
  Coord_Space world_space = get_world_space();
  push_coord_space(world_space);
  return JS_UNDEFINED;
}

static JSValue js_coord_push_clip_space(JSContext *js_ctx,
                                        JSValueConst this_val, int argc,
                                        JSValueConst *argv) {
  (void)js_ctx;
  (void)this_val;
  (void)argc;
  (void)argv;
  Coord_Space clip_space;
  xform_identity(clip_space.proj);
  xform_identity(clip_space.camera);
  push_coord_space(clip_space);
  return JS_UNDEFINED;
}

static JSValue js_coord_push_screen_space(JSContext *js_ctx,
                                          JSValueConst this_val, int argc,
                                          JSValueConst *argv) {
  (void)js_ctx;
  (void)this_val;
  (void)argc;
  (void)argv;
  Coord_Space screen_space = get_screen_space();
  push_coord_space(screen_space);
  return JS_UNDEFINED;
}

static JSValue js_coord_world_to_screen(JSContext *js_ctx,
                                        JSValueConst this_val, int argc,
                                        JSValueConst *argv) {
  if (argc < 2) {
    return JS_ThrowTypeError(js_ctx,
                             "world_to_screen requires 2 arguments (x, z)");
  }

  double world_x, world_z;
  JS_ToFloat64(js_ctx, &world_x, argv[0]);
  JS_ToFloat64(js_ctx, &world_z, argv[1]);

  if (window_w <= 1 || window_h <= 1) {
    JSValue ret = JS_NewObject(js_ctx);
    JS_SetPropertyStr(js_ctx, ret, "x", JS_NewFloat64(js_ctx, 0.0));
    JS_SetPropertyStr(js_ctx, ret, "y", JS_NewFloat64(js_ctx, 0.0));
    return ret;
  }

  Camera *cam = camera_get_main();
  Vec3 world = {(float)world_x, 0.0f, (float)world_z};
  Vec3 ndc;
  camera_world_to_screen(cam, world, ndc);

  float screen_x = (ndc[0] + 1.0f) * 0.5f * (float)window_w;
  float screen_y = (-ndc[1] + 1.0f) * 0.5f * (float)window_h;

  JSValue ret = JS_NewObject(js_ctx);
  JS_SetPropertyStr(js_ctx, ret, "x", JS_NewFloat64(js_ctx, screen_x));
  JS_SetPropertyStr(js_ctx, ret, "y", JS_NewFloat64(js_ctx, screen_y));
  return ret;
}

static JSValue js_coord_screen_to_world(JSContext *js_ctx,
                                        JSValueConst this_val, int argc,
                                        JSValueConst *argv) {
  if (argc < 2) {
    return JS_ThrowTypeError(js_ctx,
                             "screen_to_world requires 2 arguments (x, y)");
  }

  double screen_x, screen_y;
  JS_ToFloat64(js_ctx, &screen_x, argv[0]);
  JS_ToFloat64(js_ctx, &screen_y, argv[1]);

  if (window_w <= 1 || window_h <= 1) {
    JSValue ret = JS_NewObject(js_ctx);
    JS_SetPropertyStr(js_ctx, ret, "x", JS_NewFloat64(js_ctx, 0.0));
    JS_SetPropertyStr(js_ctx, ret, "y", JS_NewFloat64(js_ctx, 0.0));
    return ret;
  }

  float ndc_x = ((float)screen_x / ((float)window_w * 0.5f)) - 1.0f;
  float ndc_y = ((float)screen_y / ((float)window_h * 0.5f)) - 1.0f;
  ndc_y *= -1.0f;

  Camera *cam = camera_get_main();
  const Matrix4 *vp = camera_get_vp_matrix(cam);
  Matrix4 vp_inv;
  matrix4_inverse(*vp, vp_inv);

  float v_near[4] = {ndc_x, ndc_y, -1.0f, 1.0f};
  float w_near[4];
  matrix4_mul_vec4(vp_inv, v_near, w_near);
  if (w_near[3] != 0.0f) {
    w_near[0] /= w_near[3];
    w_near[1] /= w_near[3];
    w_near[2] /= w_near[3];
  }

  float v_far[4] = {ndc_x, ndc_y, 1.0f, 1.0f};
  float w_far[4];
  matrix4_mul_vec4(vp_inv, v_far, w_far);
  if (w_far[3] != 0.0f) {
    w_far[0] /= w_far[3];
    w_far[1] /= w_far[3];
    w_far[2] /= w_far[3];
  }

  Vec3 ro = {w_near[0], w_near[1], w_near[2]};
  Vec3 rd = {w_far[0] - w_near[0], w_far[1] - w_near[1], w_far[2] - w_near[2]};

  float t = 0.0f;
  if (fabsf(rd[1]) > 1e-6f) {
    t = -ro[1] / rd[1];
  }

  Vec3 world = {ro[0] + t * rd[0], 0.0f, ro[2] + t * rd[2]};

  JSValue ret = JS_NewObject(js_ctx);
  JS_SetPropertyStr(js_ctx, ret, "x", JS_NewFloat64(js_ctx, world[0]));
  JS_SetPropertyStr(js_ctx, ret, "y",
                    JS_NewFloat64(js_ctx, world[2]));
  return ret;
}

static JSValue js_coord_get_view_bounds(JSContext *js_ctx,
                                        JSValueConst this_val, int argc,
                                        JSValueConst *argv) {
  (void)this_val;
  (void)argc;
  (void)argv;

  float cam_x = ctx.gs ? ctx.gs->cam_pos[0] : 0;
  float cam_z = ctx.gs ? ctx.gs->cam_pos[2] : 0;

  Camera *cam = camera_get_main();
  float half_h = cam->orthographic_size;
  float half_w = half_h * cam->aspect_ratio;

  float world_min_x = cam_x - half_w;
  float world_min_z = cam_z - half_h;
  float world_max_x = cam_x + half_w;
  float world_max_z = cam_z + half_h;

  int cell_min_x = (int)floorf(world_min_x) - 2;
  int cell_min_z = (int)floorf(world_min_z) - 2;
  int cell_max_x = (int)ceilf(world_max_x) + 2;
  int cell_max_z = (int)ceilf(world_max_z) + 2;

  if (cell_min_x < 0)
    cell_min_x = 0;
  if (cell_min_z < 0)
    cell_min_z = 0;

  JSValue ret = JS_NewObject(js_ctx);
  JS_SetPropertyStr(js_ctx, ret, "min_x", JS_NewInt32(js_ctx, cell_min_x));
  JS_SetPropertyStr(js_ctx, ret, "min_z", JS_NewInt32(js_ctx, cell_min_z));
  JS_SetPropertyStr(js_ctx, ret, "max_x", JS_NewInt32(js_ctx, cell_max_x));
  JS_SetPropertyStr(js_ctx, ret, "max_z", JS_NewInt32(js_ctx, cell_max_z));
  return ret;
}

static JSValue js_game_get_state(JSContext *js_ctx, JSValueConst this_val,
                                 int argc, JSValueConst *argv) {
  (void)this_val;
  (void)argc;
  (void)argv;

  JSValue state = JS_NewObject(js_ctx);

  JS_SetPropertyStr(js_ctx, state, "window_w", JS_NewInt32(js_ctx, window_w));
  JS_SetPropertyStr(js_ctx, state, "window_h", JS_NewInt32(js_ctx, window_h));
  JS_SetPropertyStr(js_ctx, state, "log_level",
                    JS_NewInt32(js_ctx, g_log_level));

  if (ctx.gs) {
    JS_SetPropertyStr(js_ctx, state, "cam_x",
                      JS_NewFloat64(js_ctx, ctx.gs->cam_pos[0]));
    JS_SetPropertyStr(js_ctx, state, "cam_y",
                      JS_NewFloat64(js_ctx, ctx.gs->cam_pos[1]));
    JS_SetPropertyStr(js_ctx, state, "cam_z",
                      JS_NewFloat64(js_ctx, ctx.gs->cam_pos[2]));

    JS_SetPropertyStr(js_ctx, state, "rot_x",
                      JS_NewFloat64(js_ctx, ctx.gs->cam_rot[0]));
    JS_SetPropertyStr(js_ctx, state, "rot_y",
                      JS_NewFloat64(js_ctx, ctx.gs->cam_rot[1]));
    JS_SetPropertyStr(js_ctx, state, "rot_z",
                      JS_NewFloat64(js_ctx, ctx.gs->cam_rot[2]));

    JS_SetPropertyStr(js_ctx, state, "zoom",
                      JS_NewFloat64(js_ctx, ctx.gs->zoom_level));

    JS_SetPropertyStr(js_ctx, state, "ticks",
                      JS_NewFloat64(js_ctx, (double)ctx.gs->ticks));
    JS_SetPropertyStr(js_ctx, state, "time",
                      JS_NewFloat64(js_ctx, ctx.gs->game_time_elapsed));
  }
  return state;
}

static JSValue js_coord_set_camera_pos(JSContext *js_ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv) {
  (void)this_val;
  if (argc < 2)
    return JS_ThrowTypeError(
        js_ctx, "set_camera_pos requires at least 2 arguments (x, z)");

  double y = (ctx.gs) ? (double)ctx.gs->cam_pos[1] : 10.0;

  double x, z;
  if (JS_ToFloat64(js_ctx, &x, argv[0]) < 0 ||
      JS_ToFloat64(js_ctx, &z, argv[1]) < 0)
    return JS_EXCEPTION;

  if (argc >= 3) {
    if (JS_ToFloat64(js_ctx, &y, argv[2]) < 0)
      return JS_EXCEPTION;
  }

  if (ctx.gs) {
    ctx.gs->cam_pos[0] = (float)x;
    ctx.gs->cam_pos[1] = (float)y;
    ctx.gs->cam_pos[2] = (float)z;
  }

  camera_set_position(camera_get_main(), (float)x, (float)y, (float)z);

  return JS_UNDEFINED;
}

static JSValue js_coord_set_camera_rotation(JSContext *js_ctx,
                                            JSValueConst this_val, int argc,
                                            JSValueConst *argv) {
  (void)this_val;
  if (argc < 3)
    return JS_ThrowTypeError(
        js_ctx, "set_camera_rotation requires 3 arguments (x, y, z)");
  double x, y, z;
  if (JS_ToFloat64(js_ctx, &x, argv[0]) < 0 ||
      JS_ToFloat64(js_ctx, &y, argv[1]) < 0 ||
      JS_ToFloat64(js_ctx, &z, argv[2]) < 0)
    return JS_EXCEPTION;

  if (ctx.gs) {
    ctx.gs->cam_rot[0] = (float)x;
    ctx.gs->cam_rot[1] = (float)y;
    ctx.gs->cam_rot[2] = (float)z;
  }
  return JS_UNDEFINED;
}

static JSValue js_coord_set_camera_zoom(JSContext *js_ctx,
                                        JSValueConst this_val, int argc,
                                        JSValueConst *argv) {
  (void)this_val;
  if (argc < 1)
    return JS_ThrowTypeError(js_ctx, "set_camera_zoom requires 1 argument");
  double z;
  if (JS_ToFloat64(js_ctx, &z, argv[0]) < 0)
    return JS_EXCEPTION;
  if (ctx.gs) {
    ctx.gs->zoom_level = (float)z;
    ctx.gs->desired_zoom_level = (float)z;
  }
  return JS_UNDEFINED;
}

static JSValue js_app_quit(JSContext *js_ctx, JSValueConst this_val, int argc,
                           JSValueConst *argv) {
  (void)this_val;
  int code = 0;
  if (argc > 0) {
    JS_ToInt32(js_ctx, &code, argv[0]);
  }
  LOG_INFO("[app_quit] Exiting with code %d\n", code);
  exit(code);
  return JS_UNDEFINED;
}

static JSValue js_app_restart(JSContext *js_ctx, JSValueConst this_val, int argc,
                              JSValueConst *argv) {
  (void)this_val;
  (void)argc;
  (void)argv;

  char **saved_argv = app_get_argv();
  if (!saved_argv || !saved_argv[0]) {
    LOG_ERROR("[app_restart] No saved argv, cannot restart\n");
    return JS_ThrowInternalError(js_ctx, "Cannot restart: no saved argv");
  }

  LOG_INFO("[app_restart] Restarting process: %s\n", saved_argv[0]);
  fflush(stdout);
  fflush(stderr);

#ifdef _WIN32

  _execv(saved_argv[0], (const char *const *)saved_argv);
#else

  execv(saved_argv[0], saved_argv);
#endif

  LOG_ERROR("[app_restart] execv failed: %s\n", strerror(errno));
  return JS_ThrowInternalError(js_ctx, "execv failed");
}

static JSValue js_fatal(JSContext *js_ctx, JSValueConst this_val, int argc,
                        JSValueConst *argv) {
  (void)this_val;

  const char *msg = "fatal error";
  if (argc > 0) {
    msg = JS_ToCString(js_ctx, argv[0]);
    if (!msg)
      msg = "fatal error (failed to get message)";
  }

  LOG_ERROR("[FATAL] %s\n", msg);
  LOG_ERROR("[FATAL] Program terminated due to unrecoverable error.\n");

  if (argc > 0 && msg) {
    JS_FreeCString(js_ctx, msg);
  }

  exit(1);

  return JS_UNDEFINED;
}

static JSValue js_game_set_volume(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv) {
  if (argc < 1) return JS_UNDEFINED;
  double v;
  if (JS_ToFloat64(ctx, &v, argv[0]) < 0) return JS_EXCEPTION;
  if (v < 0.0) v = 0.0;
  if (v > 1.0) v = 1.0;
  g_master_volume = (float)v;
  return JS_UNDEFINED;
}

static JSValue js_game_get_volume(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv) {
  return JS_NewFloat64(ctx, (double)g_master_volume);
}

int js_init_engine_module(JSContext *js_ctx) {
  JSValue global = JS_GetGlobalObject(js_ctx);

  JSValue coord_obj = JS_NewObject(js_ctx);

  JS_SetPropertyStr(js_ctx, coord_obj, "push_world_space",
                    JS_NewCFunction(js_ctx, js_coord_push_world_space,
                                    "push_world_space", 0));
  JS_SetPropertyStr(
      js_ctx, coord_obj, "push_clip_space",
      JS_NewCFunction(js_ctx, js_coord_push_clip_space, "push_clip_space", 0));
  JS_SetPropertyStr(js_ctx, coord_obj, "push_screen_space",
                    JS_NewCFunction(js_ctx, js_coord_push_screen_space,
                                    "push_screen_space", 0));
  JS_SetPropertyStr(
      js_ctx, coord_obj, "get_view_bounds",
      JS_NewCFunction(js_ctx, js_coord_get_view_bounds, "get_view_bounds", 1));
  JS_SetPropertyStr(
      js_ctx, coord_obj, "screen_to_world",
      JS_NewCFunction(js_ctx, js_coord_screen_to_world, "screen_to_world", 2));
  JS_SetPropertyStr(
      js_ctx, coord_obj, "world_to_screen",
      JS_NewCFunction(js_ctx, js_coord_world_to_screen, "world_to_screen", 2));
  JS_SetPropertyStr(
      js_ctx, coord_obj, "set_camera_pos",
      JS_NewCFunction(js_ctx, js_coord_set_camera_pos, "set_camera_pos", 2));
  JS_SetPropertyStr(js_ctx, coord_obj, "set_camera_rotation",
                    JS_NewCFunction(js_ctx, js_coord_set_camera_rotation,
                                    "set_camera_rotation", 3));
  JS_SetPropertyStr(
      js_ctx, coord_obj, "set_camera_zoom",
      JS_NewCFunction(js_ctx, js_coord_set_camera_zoom, "set_camera_zoom", 1));

  JS_SetPropertyStr(js_ctx, global, "coord", coord_obj);

  JSValue game_obj = JS_NewObject(js_ctx);

  JS_SetPropertyStr(js_ctx, game_obj, "get_state",
                    JS_NewCFunction(js_ctx, js_game_get_state, "get_state", 0));

  JS_SetPropertyStr(js_ctx, global, "game", game_obj);

  JS_SetPropertyStr(js_ctx, global, "__BP_VERBOSE__",
                    JS_NewBool(js_ctx, g_log_level >= LOG_LEVEL_VERBOSE));

  JS_SetPropertyStr(js_ctx, global, "fatal",
                    JS_NewCFunction(js_ctx, js_fatal, "fatal", 1));

  JS_SetPropertyStr(js_ctx, global, "app_quit",
                    JS_NewCFunction(js_ctx, js_app_quit, "app_quit", 1));

  JS_SetPropertyStr(js_ctx, global, "app_restart",
                    JS_NewCFunction(js_ctx, js_app_restart, "app_restart", 0));

  // volume control on game object
  JS_SetPropertyStr(js_ctx, game_obj, "set_volume",
                    JS_NewCFunction(js_ctx, js_game_set_volume, "set_volume", 1));
  JS_SetPropertyStr(js_ctx, game_obj, "get_volume",
                    JS_NewCFunction(js_ctx, js_game_get_volume, "get_volume", 0));

  JS_FreeValue(js_ctx, global);

  LOG_VERBOSE("[GameBindings] game/coord/config modules loaded\n");
  return 0;
}
