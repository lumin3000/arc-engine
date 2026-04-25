
#if defined(_WIN32)
#define popen _popen
#define pclose _pclose
#endif
#include "draw_bindings.h"
#include "../log.h"
#include "bedrock/gfx/draw.h"
#include "bedrock/gfx/render.h"
#include "bedrock/gfx/text.h"
#include "bedrock/gfx/font_manager.h"
#include "bedrock/gfx/sdf_text_pipeline.h"
#include "bedrock/utils/utils.h"
#include "quickjs.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

extern struct font_manager *get_global_font_manager(void);

static void js_get_vec2(JSContext *ctx, JSValue obj, const char *name,
                        Vec2 out) {
  JSValue v = JS_GetPropertyStr(ctx, obj, name);
  if (JS_IsArray(v)) {
    JSValue x = JS_GetPropertyUint32(ctx, v, 0);
    JSValue y = JS_GetPropertyUint32(ctx, v, 1);
    double dx, dy;
    JS_ToFloat64(ctx, &dx, x);
    JS_ToFloat64(ctx, &dy, y);
    out[0] = (float)dx;
    out[1] = (float)dy;
    JS_FreeValue(ctx, x);
    JS_FreeValue(ctx, y);
  }
  JS_FreeValue(ctx, v);
}

static void js_get_vec4(JSContext *ctx, JSValue obj, const char *name,
                        Vec4 out) {
  JSValue v = JS_GetPropertyStr(ctx, obj, name);
  if (!JS_IsUndefined(v) && !JS_IsNull(v) && JS_IsArray(v)) {
    for (int i = 0; i < 4; i++) {
      JSValue vi = JS_GetPropertyUint32(ctx, v, i);
      double d;
      JS_ToFloat64(ctx, &d, vi);
      out[i] = (float)d;
      JS_FreeValue(ctx, vi);
    }
  }
  JS_FreeValue(ctx, v);
}

static int32_t js_get_int(JSContext *ctx, JSValue obj, const char *name,
                          int32_t def) {
  JSValue v = JS_GetPropertyStr(ctx, obj, name);
  int32_t result = def;
  if (!JS_IsUndefined(v)) {
    JS_ToInt32(ctx, &result, v);
  }
  JS_FreeValue(ctx, v);
  return result;
}

static double js_get_float(JSContext *ctx, JSValue obj, const char *name,
                           double def) {
  JSValue v = JS_GetPropertyStr(ctx, obj, name);
  double result = def;
  if (!JS_IsUndefined(v)) {
    JS_ToFloat64(ctx, &result, v);
  }
  JS_FreeValue(ctx, v);
  return result;
}

static bool js_get_bool(JSContext *ctx, JSValue obj, const char *name,
                        bool def) {
  JSValue v = JS_GetPropertyStr(ctx, obj, name);
  bool result = def;
  if (!JS_IsUndefined(v)) {
    result = JS_ToBool(ctx, v);
  }
  JS_FreeValue(ctx, v);
  return result;
}

#include "../bedrock/screenshot.h"

static JSValue js_render_screenshot(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv) {
  (void)this_val;
  const char *path = argc >= 1 ? JS_ToCString(ctx, argv[0]) : NULL;
  const char *p = path ? path : "/tmp/screenshot.png";

  int ret = screenshot_request(p);
  if (path) JS_FreeCString(ctx, path);
  return JS_NewInt32(ctx, ret);
}

static JSValue js_render_exec(JSContext *ctx, JSValueConst this_val,
                              int argc, JSValueConst *argv) {
  (void)this_val;
  if (argc < 1) return JS_ThrowTypeError(ctx, "render.exec: missing cmd");
  const char *cmd = JS_ToCString(ctx, argv[0]);
  if (!cmd) return JS_UNDEFINED;
  FILE *fp = popen(cmd, "r");
  JS_FreeCString(ctx, cmd);
  if (!fp) return JS_NewString(ctx, "ERROR:popen_failed");
  char out[8192]; size_t n = 0, len;
  char buf[512];
  while (fgets(buf, sizeof(buf), fp) && n < sizeof(out) - 1) {
    len = strlen(buf);
    if (n + len >= sizeof(out)) len = sizeof(out) - 1 - n;
    memcpy(out + n, buf, len); n += len;
  }
  out[n] = '\0';
  pclose(fp);
  while (n > 0 && (out[n-1] == '\n' || out[n-1] == '\r')) out[--n] = '\0';
  return JS_NewString(ctx, out);
}

static JSValue js_render_quit(JSContext *ctx, JSValueConst this_val,
                              int argc, JSValueConst *argv) {
  (void)ctx; (void)this_val; (void)argc; (void)argv;
  exit(0);
  return JS_UNDEFINED;
}

// render.sprite_id_by_name(name) -> int (>=0 if registered, -1 otherwise)
// Game scripts use this to look up engine_register_sprites() registrations
// without hard-coded enums. The lookup is O(N) over the registered count;
// JS callers are expected to cache results.
static JSValue js_render_sprite_id_by_name(JSContext *ctx, JSValueConst this_val,
                                           int argc, JSValueConst *argv) {
  (void)this_val;
  if (argc < 1) {
    return JS_ThrowTypeError(ctx, "render.sprite_id_by_name(name) requires 1 argument");
  }
  const char *name = JS_ToCString(ctx, argv[0]);
  if (!name) return JS_EXCEPTION;
  Sprite_Name id = engine_get_sprite_index_by_name(name);
  JS_FreeCString(ctx, name);
  return JS_NewInt32(ctx, (int32_t)id);
}

static JSValue js_draw_sprite(JSContext *ctx, JSValueConst this_val, int argc,
                              JSValueConst *argv) {
  if (argc < 3) {
    return JS_ThrowTypeError(
        ctx, "draw.sprite requires at least 3 arguments (x, y, sprite)");
  }

  double x, y;
  int32_t sprite;
  JS_ToFloat64(ctx, &x, argv[0]);
  JS_ToFloat64(ctx, &y, argv[1]);
  JS_ToInt32(ctx, &sprite, argv[2]);

  Vec2 pos = {(float)x, (float)y};

  Draw_Sprite_Opts opts = DRAW_SPRITE_OPTS_DEFAULT;

  if (argc > 3 && JS_IsObject(argv[3])) {
    JSValue opt = argv[3];
    opts.pivot = js_get_int(ctx, opt, "pivot", Pivot_center_center);
    opts.flip_x = js_get_bool(ctx, opt, "flip_x", false);
    opts.z_layer = js_get_int(ctx, opt, "z_layer", ZLayer_playspace);
    opts.anim_index = js_get_int(ctx, opt, "anim_index", 0);

    js_get_vec2(ctx, opt, "draw_offset", opts.draw_offset);
    js_get_vec4(ctx, opt, "col", opts.col);
    js_get_vec4(ctx, opt, "col_override", opts.col_override);
  }

  draw_sprite_ex(pos, (Sprite_Name)sprite, opts);

  return JS_UNDEFINED;
}

static JSValue js_draw_rect(JSContext *ctx, JSValueConst this_val, int argc,
                            JSValueConst *argv) {
  if (argc < 4) {
    return JS_ThrowTypeError(
        ctx, "draw.rect requires at least 4 arguments (x1, y1, x2, y2)");
  }

  double x1, y1, x2, y2;
  JS_ToFloat64(ctx, &x1, argv[0]);
  JS_ToFloat64(ctx, &y1, argv[1]);
  JS_ToFloat64(ctx, &x2, argv[2]);
  JS_ToFloat64(ctx, &y2, argv[3]);

  Rect rect = {(float)x1, (float)y1, (float)x2, (float)y2};

  Draw_Rect_Opts opts = DRAW_RECT_OPTS_DEFAULT;

  if (argc > 4 && JS_IsObject(argv[4])) {
    JSValue opt = argv[4];
    opts.z_layer = js_get_int(ctx, opt, "z_layer", ZLayer_playspace);
    opts.flags = js_get_int(ctx, opt, "flags", 0);

    js_get_vec4(ctx, opt, "col", opts.col);
    js_get_vec4(ctx, opt, "col_override", opts.col_override);
  }

  draw_rect_ex(rect, opts);

  return JS_UNDEFINED;
}

static JSValue js_draw_ground_rect(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv) {
  if (argc < 4) {
    return JS_ThrowTypeError(
        ctx, "draw.ground_rect requires at least 4 arguments (x1, z1, x2, z2)");
  }

  double x1, z1, x2, z2;
  JS_ToFloat64(ctx, &x1, argv[0]);
  JS_ToFloat64(ctx, &z1, argv[1]);
  JS_ToFloat64(ctx, &x2, argv[2]);
  JS_ToFloat64(ctx, &z2, argv[3]);

  Rect rect = {(float)x1, (float)z1, (float)x2, (float)z2};

  Draw_Rect_Opts opts = DRAW_RECT_OPTS_DEFAULT;

  if (argc > 4 && JS_IsObject(argv[4])) {
    JSValue opt = argv[4];
    opts.z_layer = js_get_int(ctx, opt, "z_layer", ZLayer_playspace);
    opts.flags = js_get_int(ctx, opt, "flags", 0);

    js_get_vec4(ctx, opt, "col", opts.col);
    js_get_vec4(ctx, opt, "col_override", opts.col_override);
  }

  draw_rect_xz_ex(rect, opts);

  return JS_UNDEFINED;
}

static JSValue js_draw_text(JSContext *ctx, JSValueConst this_val, int argc,
                            JSValueConst *argv) {
  if (argc < 3) {
    return JS_ThrowTypeError(
        ctx, "draw.text requires at least 3 arguments (x, y, text)");
  }

  double x, y;
  JS_ToFloat64(ctx, &x, argv[0]);
  JS_ToFloat64(ctx, &y, argv[1]);

  const char *text = JS_ToCString(ctx, argv[2]);
  if (!text)
    return JS_EXCEPTION;

  int font_size = 14;
  uint32_t color = 0xFFFFFFFF;

  if (argc > 3 && JS_IsObject(argv[3])) {
    JSValue opt = argv[3];
    font_size = (int)js_get_float(ctx, opt, "font_size", 14);

    Vec4 col = {1.0f, 1.0f, 1.0f, 1.0f};
    js_get_vec4(ctx, opt, "col", col);

    uint8_t r = (uint8_t)(col[0] * 255);
    uint8_t g = (uint8_t)(col[1] * 255);
    uint8_t b = (uint8_t)(col[2] * 255);
    uint8_t a = (uint8_t)(col[3] * 255);
    color = (a << 24) | (r << 16) | (g << 8) | b;
  }

  struct font_manager *fm = get_global_font_manager();
  if (!fm) {
    JS_FreeCString(ctx, text);
    return JS_UNDEFINED;
  }

  int ascent, descent, line_gap;
  font_manager_metrics(fm, 1, font_size, &ascent, &descent, &line_gap);

  float cur_x = (float)x;
  float cur_y = (float)y + ascent;
  const unsigned char *s = (const unsigned char *)text;

  while (*s) {
    uint32_t codepoint;

    if (*s < 0x80) {
      codepoint = *s++;
    } else if (*s < 0xE0) {
      codepoint = ((*s & 0x1F) << 6) | (s[1] & 0x3F);
      s += 2;
    } else if (*s < 0xF0) {
      codepoint = ((*s & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
      s += 3;
    } else {
      codepoint = ((*s & 0x07) << 18) | ((s[1] & 0x3F) << 12) |
                  ((s[2] & 0x3F) << 6) | (s[3] & 0x3F);
      s += 4;
    }

    if (codepoint == '\n') {

      cur_x = (float)x;
      cur_y += ascent - descent + line_gap;
      continue;
    }

    FontGlyph g, og;
    int fid = font_manager_has_codepoint(fm, 1, codepoint) ? 1 : 2;
    const char *err = font_manager_glyph_ex(fm, fid, codepoint, font_size, &g, &og);
    if (!err) {

      sdf_text_add_glyph_ex(cur_x, cur_y, g.u, g.v, g.w, g.h, og.w, og.h,
                            g.offset_x, g.offset_y, color);
      cur_x += g.advance_x;
    }
  }

  JS_FreeCString(ctx, text);
  return JS_UNDEFINED;
}

static JSValue js_render_get_sprite_uv(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv) {
  if (argc < 1) {
    return JS_ThrowTypeError(
        ctx, "render.get_sprite_uv requires 1 argument (sprite_id)");
  }

  int32_t sprite_id;
  JS_ToInt32(ctx, &sprite_id, argv[0]);

  Vec4 uv;
  atlas_uv_from_sprite((Sprite_Name)sprite_id, uv);

  JSValue arr = JS_NewArray(ctx);
  JS_SetPropertyUint32(ctx, arr, 0, JS_NewFloat64(ctx, uv[0]));
  JS_SetPropertyUint32(ctx, arr, 1, JS_NewFloat64(ctx, uv[1]));
  JS_SetPropertyUint32(ctx, arr, 2, JS_NewFloat64(ctx, uv[2]));
  JS_SetPropertyUint32(ctx, arr, 3, JS_NewFloat64(ctx, uv[3]));

  return arr;
}

static JSValue js_render_set_sway_head(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv) {
  if (argc < 1) {
    return JS_ThrowTypeError(ctx, "render.set_sway_head requires 1 argument (float)");
  }

  double value;
  JS_ToFloat64(ctx, &value, argv[0]);
  render_set_sway_head((float)value);

  return JS_UNDEFINED;
}

static JSValue js_render_set_bg_uv(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv) {
  if (argc < 1 || !JS_IsArray(argv[0])) {
    return JS_ThrowTypeError(
        ctx,
        "render.set_bg_uv requires 1 argument (uv array [u0, v0, u1, v1])");
  }

  Vec4 uv;
  for (int i = 0; i < 4; i++) {
    JSValue vi = JS_GetPropertyUint32(ctx, argv[0], i);
    double d;
    JS_ToFloat64(ctx, &d, vi);
    uv[i] = (float)d;
    JS_FreeValue(ctx, vi);
  }

  memcpy(draw_frame.shader_data.bg_repeat_tex0_atlas_uv, uv, sizeof(Vec4));

  return JS_UNDEFINED;
}

static int g_tilemap_width = 0;
static int g_tilemap_height = 0;
static int g_tilemap_tile_size = 0;
static Vec4 g_tilemap_color_light = {0.35f, 0.55f, 0.35f, 1.0f};
static Vec4 g_tilemap_color_dark = {0.3f, 0.5f, 0.3f, 1.0f};

static JSValue js_tilemap_init(JSContext *ctx, JSValueConst this_val, int argc,
                               JSValueConst *argv) {
  if (argc < 3) {
    return JS_ThrowTypeError(
        ctx, "tilemap.init requires (width, height, tile_size)");
  }

  int32_t w, h, ts;
  JS_ToInt32(ctx, &w, argv[0]);
  JS_ToInt32(ctx, &h, argv[1]);
  JS_ToInt32(ctx, &ts, argv[2]);

  g_tilemap_width = w;
  g_tilemap_height = h;
  g_tilemap_tile_size = ts;

  if (argc > 3 && JS_IsArray(argv[3])) {
    js_get_vec4(ctx, argv[3], "", g_tilemap_color_light);
  }
  if (argc > 4 && JS_IsArray(argv[4])) {
    js_get_vec4(ctx, argv[4], "", g_tilemap_color_dark);
  }

  return JS_UNDEFINED;
}

static JSValue js_tilemap_draw(JSContext *ctx, JSValueConst this_val, int argc,
                               JSValueConst *argv) {
  (void)this_val;
  (void)argc;
  (void)argv;

  if (g_tilemap_width == 0 || g_tilemap_height == 0) {
    return JS_UNDEFINED;
  }

  Draw_Rect_Opts opts = DRAW_RECT_OPTS_DEFAULT;
  opts.z_layer = ZLayer_playspace;

  for (int y = 0; y < g_tilemap_height; y++) {
    for (int x = 0; x < g_tilemap_width; x++) {
      float tile_x = (float)(x * g_tilemap_tile_size);
      float tile_y = (float)(y * g_tilemap_tile_size);

      if ((x + y) % 2 == 0) {
        memcpy(opts.col, g_tilemap_color_light, sizeof(Vec4));
      } else {
        memcpy(opts.col, g_tilemap_color_dark, sizeof(Vec4));
      }

      Rect rect = {tile_x, tile_y, tile_x + g_tilemap_tile_size,
                   tile_y + g_tilemap_tile_size};
      draw_rect_ex(rect, opts);
    }
  }

  return JS_UNDEFINED;
}

static JSValue js_draw_line(JSContext *ctx, JSValueConst this_val, int argc,
                            JSValueConst *argv) {
  if (argc < 4) {
    return JS_ThrowTypeError(
        ctx, "draw.line requires at least 4 arguments (ax, ay, bx, by)");
  }

  double ax, ay, bx, by;
  JS_ToFloat64(ctx, &ax, argv[0]);
  JS_ToFloat64(ctx, &ay, argv[1]);
  JS_ToFloat64(ctx, &bx, argv[2]);
  JS_ToFloat64(ctx, &by, argv[3]);

  Vec2 a = {(float)ax, (float)ay};
  Vec2 b = {(float)bx, (float)by};

  Draw_Line_Opts opts = DRAW_LINE_OPTS_DEFAULT;

  if (argc > 4 && JS_IsObject(argv[4])) {
    JSValue opt = argv[4];
    opts.width = (float)js_get_float(ctx, opt, "width", 1.0);
    opts.z_layer = js_get_int(ctx, opt, "z_layer", ZLayer_playspace);
    opts.z_layer_queue = js_get_int(ctx, opt, "z_layer_queue", -1);
    js_get_vec4(ctx, opt, "col", opts.col);
  }

  draw_line_ex(a, b, opts);

  return JS_UNDEFINED;
}

static JSValue js_draw_world_line(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv) {
  if (argc < 4) {
    return JS_ThrowTypeError(
        ctx, "draw.world_line requires at least 4 arguments (ax, az, bx, bz)");
  }

  double ax, az, bx, bz;
  JS_ToFloat64(ctx, &ax, argv[0]);
  JS_ToFloat64(ctx, &az, argv[1]);
  JS_ToFloat64(ctx, &bx, argv[2]);
  JS_ToFloat64(ctx, &bz, argv[3]);

  Vec2 a = {(float)ax, (float)az};
  Vec2 b = {(float)bx, (float)bz};

  Draw_Line_XZ_Opts opts = DRAW_LINE_XZ_OPTS_DEFAULT;

  if (argc > 4 && JS_IsObject(argv[4])) {
    JSValue opt = argv[4];
    opts.width = (float)js_get_float(ctx, opt, "width", 0.2);
    opts.altitude = (float)js_get_float(ctx, opt, "altitude", 0.5);
    js_get_vec4(ctx, opt, "col", opts.col);
  }

  draw_line_xz_ex(a, b, opts);

  return JS_UNDEFINED;
}

static JSValue js_draw_sdf_quad(JSContext *ctx, JSValueConst this_val, int argc,
                                JSValueConst *argv) {
  if (argc < 8) {
    return JS_ThrowTypeError(ctx, "draw.sdf_quad requires at least 8 arguments "
                                  "(x, y, w, h, u, v, uw, vh)");
  }

  double x, y, w, h, u, v, uw, vh;
  JS_ToFloat64(ctx, &x, argv[0]);
  JS_ToFloat64(ctx, &y, argv[1]);
  JS_ToFloat64(ctx, &w, argv[2]);
  JS_ToFloat64(ctx, &h, argv[3]);
  JS_ToFloat64(ctx, &u, argv[4]);
  JS_ToFloat64(ctx, &v, argv[5]);
  JS_ToFloat64(ctx, &uw, argv[6]);
  JS_ToFloat64(ctx, &vh, argv[7]);

  Vec4 col = {1.0f, 1.0f, 1.0f, 1.0f};
  ZLayer z_layer = ZLayer_ui;

  if (argc > 8 && JS_IsObject(argv[8])) {
    JSValue opt = argv[8];
    z_layer = js_get_int(ctx, opt, "z_layer", ZLayer_ui);
    js_get_vec4(ctx, opt, "col", col);
  }

  Vec2 rect_pos = {(float)x, (float)y};
  Matrix4 xform;
  xform_translate(rect_pos, xform);

  Vec2 size = {(float)w, (float)h};
  Vec4 uv_rect = {(float)u, (float)v, (float)(u + uw), (float)(v + vh)};

  Draw_Rect_Xform_Opts opts = DRAW_RECT_XFORM_OPTS_DEFAULT;
  opts.sprite = Sprite_Name_nil;
  memcpy(opts.uv, uv_rect, sizeof(Vec4));
  opts.tex_index = 2;
  memcpy(opts.col, col, sizeof(Vec4));
  opts.z_layer = z_layer;

  draw_rect_xform_ex(xform, size, opts);

  return JS_UNDEFINED;
}

int js_init_draw_module(JSContext *ctx) {
  JSValue global = JS_GetGlobalObject(ctx);
  JSValue draw_obj = JS_NewObject(ctx);

  JS_SetPropertyStr(ctx, draw_obj, "sprite",
                    JS_NewCFunction(ctx, js_draw_sprite, "sprite", 4));
  JS_SetPropertyStr(ctx, draw_obj, "rect",
                    JS_NewCFunction(ctx, js_draw_rect, "rect", 5));
  JS_SetPropertyStr(
      ctx, draw_obj, "ground_rect",
      JS_NewCFunction(ctx, js_draw_ground_rect, "ground_rect", 5));
  JS_SetPropertyStr(ctx, draw_obj, "text",
                    JS_NewCFunction(ctx, js_draw_text, "text", 4));
  JS_SetPropertyStr(ctx, draw_obj, "sdf_quad",
                    JS_NewCFunction(ctx, js_draw_sdf_quad, "sdf_quad", 9));
  JS_SetPropertyStr(ctx, draw_obj, "line",
                    JS_NewCFunction(ctx, js_draw_line, "line", 5));
  JS_SetPropertyStr(ctx, draw_obj, "world_line",
                    JS_NewCFunction(ctx, js_draw_world_line, "world_line", 5));

  JS_SetPropertyStr(ctx, draw_obj, "PIVOT_CENTER",
                    JS_NewInt32(ctx, Pivot_center_center));
  JS_SetPropertyStr(ctx, draw_obj, "PIVOT_TOP_LEFT",
                    JS_NewInt32(ctx, Pivot_top_left));
  JS_SetPropertyStr(ctx, draw_obj, "PIVOT_TOP_CENTER",
                    JS_NewInt32(ctx, Pivot_top_center));
  JS_SetPropertyStr(ctx, draw_obj, "PIVOT_BOTTOM_CENTER",
                    JS_NewInt32(ctx, Pivot_bottom_center));
  JS_SetPropertyStr(ctx, draw_obj, "PIVOT_BOTTOM_LEFT",
                    JS_NewInt32(ctx, Pivot_bottom_left));

  JS_SetPropertyStr(ctx, draw_obj, "ZLAYER_BACKGROUND",
                    JS_NewInt32(ctx, ZLayer_background));
  JS_SetPropertyStr(ctx, draw_obj, "ZLAYER_SHADOW",
                    JS_NewInt32(ctx, ZLayer_shadow));
  JS_SetPropertyStr(ctx, draw_obj, "ZLAYER_PLAYSPACE",
                    JS_NewInt32(ctx, ZLayer_playspace));
  JS_SetPropertyStr(ctx, draw_obj, "ZLAYER_UI", JS_NewInt32(ctx, ZLayer_ui));

  JS_SetPropertyStr(ctx, global, "draw", draw_obj);

  JSValue render_obj = JS_NewObject(ctx);

  JS_SetPropertyStr(
      ctx, render_obj, "get_sprite_uv",
      JS_NewCFunction(ctx, js_render_get_sprite_uv, "get_sprite_uv", 1));
  JS_SetPropertyStr(ctx, render_obj, "set_bg_uv",
                    JS_NewCFunction(ctx, js_render_set_bg_uv, "set_bg_uv", 1));
  JS_SetPropertyStr(ctx, render_obj, "set_sway_head",
                    JS_NewCFunction(ctx, js_render_set_sway_head, "set_sway_head", 1));
  JS_SetPropertyStr(ctx, render_obj, "screenshot",
                    JS_NewCFunction(ctx, js_render_screenshot, "screenshot", 1));
  JS_SetPropertyStr(ctx, render_obj, "exec",
                    JS_NewCFunction(ctx, js_render_exec, "exec", 1));
  JS_SetPropertyStr(ctx, render_obj, "quit",
                    JS_NewCFunction(ctx, js_render_quit, "quit", 0));
  JS_SetPropertyStr(
      ctx, render_obj, "sprite_id_by_name",
      JS_NewCFunction(ctx, js_render_sprite_id_by_name, "sprite_id_by_name", 1));

  JS_SetPropertyStr(ctx, global, "render", render_obj);

  JSValue tilemap_obj = JS_NewObject(ctx);

  JS_SetPropertyStr(ctx, tilemap_obj, "init",
                    JS_NewCFunction(ctx, js_tilemap_init, "init", 5));
  JS_SetPropertyStr(ctx, tilemap_obj, "draw",
                    JS_NewCFunction(ctx, js_tilemap_draw, "draw", 0));

  JS_SetPropertyStr(ctx, global, "tilemap", tilemap_obj);

  JS_FreeValue(ctx, global);

  LOG_VERBOSE("[DrawBindings] draw/render/popup/tilemap modules loaded\n");
  return 0;
}
