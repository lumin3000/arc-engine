
#include "imgui_bindings.h"
#include "../log.h"
#include "quickjs.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "../../external/cimgui/cimgui.h"

#include "../../external/sokol/c/sokol_app.h"

extern void simgui_setup_wrapper(void);
extern void simgui_new_frame_wrapper(int width, int height, double delta_time);
extern void simgui_render_wrapper(void);
extern void simgui_shutdown_wrapper(void);
extern bool simgui_handle_event_wrapper(const sapp_event *event);

static struct {
  int initialized;
  int frame_active;
} g_imgui = {0};

#define MAX_CHECKBOX_STATES 128
#define MAX_SLIDER_STATES 128
#define MAX_TEXTBOX_STATES 32
#define MAX_TEXTBOX_LEN 256

static bool g_checkbox_states[MAX_CHECKBOX_STATES];
static float g_slider_states[MAX_SLIDER_STATES];
static char g_textbox_states[MAX_TEXTBOX_STATES][MAX_TEXTBOX_LEN];

int imgui_frame_is_active(void) {
  return g_imgui.initialized && g_imgui.frame_active;
}

void imgui_frame_reset(void) { g_imgui.frame_active = 0; }

static JSValue js_nk_init(JSContext *ctx, JSValueConst this_val, int argc,
                          JSValueConst *argv) {
  if (g_imgui.initialized)
    return JS_UNDEFINED;
  simgui_setup_wrapper();
  g_imgui.initialized = 1;
  memset(g_checkbox_states, 0, sizeof(g_checkbox_states));
  memset(g_slider_states, 0, sizeof(g_slider_states));
  memset(g_textbox_states, 0, sizeof(g_textbox_states));
  LOG_INFO("[imgui] initialized\n");
  return JS_UNDEFINED;
}

static JSValue js_nk_begin_frame(JSContext *ctx, JSValueConst this_val,
                                 int argc, JSValueConst *argv) {
  if (!g_imgui.initialized)
    return JS_UNDEFINED;

  if (g_imgui.frame_active)
    return JS_UNDEFINED;
  int w = sapp_width();
  int h = sapp_height();
  simgui_new_frame_wrapper(w, h, 1.0 / 60.0);
  g_imgui.frame_active = 1;
  return JS_UNDEFINED;
}

static JSValue js_nk_end_frame(JSContext *ctx, JSValueConst this_val, int argc,
                               JSValueConst *argv) {
  return JS_UNDEFINED;
}

static JSValue js_nk_render(JSContext *ctx, JSValueConst this_val, int argc,
                            JSValueConst *argv) {
  return JS_UNDEFINED;
}

static JSValue js_noop(JSContext *ctx, JSValueConst this_val, int argc,
                       JSValueConst *argv) {
  return JS_UNDEFINED;
}

static int mu_opts_to_imgui_flags(int mu_opt) {
  int flags = 0;
  if (mu_opt & 0x80)
    flags |= ImGuiWindowFlags_NoTitleBar;
  if (mu_opt & 0x10)
    flags |= ImGuiWindowFlags_NoResize;
  if (mu_opt & 0x20)
    flags |= ImGuiWindowFlags_NoScrollbar;
  if (mu_opt & 0x08)
    flags |= ImGuiWindowFlags_NoBackground;
  if (mu_opt & 0x04)
    flags |= ImGuiWindowFlags_NoInputs;

  return flags;
}

static JSValue js_nk_begin_window(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv) {
  if (argc < 5)
    return JS_NewBool(ctx, 0);

  const char *title = JS_ToCString(ctx, argv[0]);
  if (!title)
    return JS_EXCEPTION;

  double x, y, w, h;
  JS_ToFloat64(ctx, &x, argv[0 + 1]);
  JS_ToFloat64(ctx, &y, argv[1 + 1]);
  JS_ToFloat64(ctx, &w, argv[2 + 1]);
  JS_ToFloat64(ctx, &h, argv[3 + 1]);

  int mu_opt = 0;
  if (argc > 5)
    JS_ToInt32(ctx, &mu_opt, argv[5]);

  int flags = mu_opts_to_imgui_flags(mu_opt);

  if (mu_opt & 0x80) {
    flags |= ImGuiWindowFlags_NoCollapse;
  }

  ImVec2_c pos = {(float)x, (float)y};
  ImVec2_c pivot = {0, 0};
  ImVec2_c size = {(float)w, (float)h};
  igSetNextWindowPos(pos, ImGuiCond_Always, pivot);
  igSetNextWindowSize(size, ImGuiCond_Always);

  bool p_open_val = true;
  bool *p_open = (mu_opt & 0x40) ? NULL : &p_open_val;

  bool visible = igBegin(title, p_open, flags);
  JS_FreeCString(ctx, title);

  if (p_open && !p_open_val) {
    igEnd();
    return JS_NewBool(ctx, 0);
  }

  return JS_NewBool(ctx, visible);
}

static JSValue js_nk_end_window(JSContext *ctx, JSValueConst this_val, int argc,
                                JSValueConst *argv) {
  igEnd();
  return JS_UNDEFINED;
}

static JSValue js_nk_get_current_container(JSContext *ctx,
                                           JSValueConst this_val, int argc,
                                           JSValueConst *argv) {
  ImVec2_c pos = igGetWindowPos();
  ImVec2_c size = igGetWindowSize();

  float title_bar_h = igGetFrameHeight();

  JSValue obj = JS_NewObject(ctx);
  JS_SetPropertyStr(ctx, obj, "x", JS_NewFloat64(ctx, pos.x));
  JS_SetPropertyStr(ctx, obj, "y", JS_NewFloat64(ctx, pos.y));
  JS_SetPropertyStr(ctx, obj, "body_w", JS_NewFloat64(ctx, size.x));
  JS_SetPropertyStr(ctx, obj, "body_h",
                    JS_NewFloat64(ctx, size.y - title_bar_h));
  return obj;
}

static int g_layout_cols = 1;
static float g_layout_widths[32];
static float g_layout_height = 0;
static int g_layout_col_index = 0;

static JSValue js_nk_layout_row(JSContext *ctx, JSValueConst this_val, int argc,
                                JSValueConst *argv) {
  if (argc < 3)
    return JS_UNDEFINED;

  JS_ToInt32(ctx, &g_layout_cols, argv[0]);
  if (g_layout_cols > 32)
    g_layout_cols = 32;

  if (JS_IsArray(argv[1])) {
    for (int i = 0; i < g_layout_cols; i++) {
      JSValue elem = JS_GetPropertyUint32(ctx, argv[1], i);
      double w;
      JS_ToFloat64(ctx, &w, elem);
      g_layout_widths[i] = (float)w;
      JS_FreeValue(ctx, elem);
    }
  }

  double h;
  JS_ToFloat64(ctx, &h, argv[2]);
  g_layout_height = (float)h;
  g_layout_col_index = 0;

  return JS_UNDEFINED;
}

static void layout_advance(void) {
  if (g_layout_cols <= 1)
    return;
  if (g_layout_col_index > 0 && g_layout_col_index < g_layout_cols) {
    igSameLine(0, -1);
  }

  float w = g_layout_widths[g_layout_col_index];
  if (w > 0) {
    igSetNextItemWidth(w);
  } else if (w < 0) {

    ImVec2_c avail = igGetContentRegionAvail();
    igSetNextItemWidth(avail.x);
  }
  g_layout_col_index++;
  if (g_layout_col_index >= g_layout_cols) {
    g_layout_col_index = 0;
  }
}

static JSValue js_nk_layout_set_next(JSContext *ctx, JSValueConst this_val,
                                     int argc, JSValueConst *argv) {

  return JS_UNDEFINED;
}

static JSValue js_nk_layout_next(JSContext *ctx, JSValueConst this_val,
                                 int argc, JSValueConst *argv) {
  ImVec2_c cursor = igGetCursorScreenPos();

  JSValue obj = JS_NewObject(ctx);
  JS_SetPropertyStr(ctx, obj, "x", JS_NewFloat64(ctx, cursor.x));
  JS_SetPropertyStr(ctx, obj, "y", JS_NewFloat64(ctx, cursor.y));

  ImVec2_c avail = igGetContentRegionAvail();
  JS_SetPropertyStr(ctx, obj, "w", JS_NewFloat64(ctx, avail.x));
  JS_SetPropertyStr(ctx, obj, "h", JS_NewFloat64(ctx, g_layout_height));
  return obj;
}

static JSValue js_nk_layout_begin_column(JSContext *ctx, JSValueConst this_val,
                                         int argc, JSValueConst *argv) {
  igBeginGroup();
  return JS_UNDEFINED;
}

static JSValue js_nk_layout_end_column(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv) {
  igEndGroup();
  return JS_UNDEFINED;
}

static JSValue js_nk_button(JSContext *ctx, JSValueConst this_val, int argc,
                            JSValueConst *argv) {
  layout_advance();
  if (argc < 1)
    return JS_NewBool(ctx, 0);
  const char *label = JS_ToCString(ctx, argv[0]);
  if (!label)
    return JS_EXCEPTION;
  ImVec2_c size = {0, 0};

  if (argc > 1) { double v; JS_ToFloat64(ctx, &v, argv[1]); size.x = (float)v; }
  if (argc > 2) { double v; JS_ToFloat64(ctx, &v, argv[2]); size.y = (float)v; }
  bool clicked = igButton(label, size);
  JS_FreeCString(ctx, label);
  return JS_NewBool(ctx, clicked);
}

static JSValue js_nk_label(JSContext *ctx, JSValueConst this_val, int argc,
                           JSValueConst *argv) {
  layout_advance();
  if (argc < 1)
    return JS_UNDEFINED;
  const char *str = JS_ToCString(ctx, argv[0]);
  if (!str)
    return JS_EXCEPTION;
  igTextUnformatted(str, NULL);
  JS_FreeCString(ctx, str);
  return JS_UNDEFINED;
}

static JSValue js_nk_text(JSContext *ctx, JSValueConst this_val, int argc,
                          JSValueConst *argv) {
  layout_advance();
  if (argc < 1)
    return JS_UNDEFINED;
  const char *str = JS_ToCString(ctx, argv[0]);
  if (!str)
    return JS_EXCEPTION;
  igTextWrapped("%s", str);
  JS_FreeCString(ctx, str);
  return JS_UNDEFINED;
}

static JSValue js_nk_checkbox(JSContext *ctx, JSValueConst this_val, int argc,
                              JSValueConst *argv) {
  layout_advance();
  if (argc < 2)
    return JS_UNDEFINED;
  const char *label = JS_ToCString(ctx, argv[0]);
  if (!label)
    return JS_EXCEPTION;
  int id = 0;
  JS_ToInt32(ctx, &id, argv[1]);
  if (id < 0 || id >= MAX_CHECKBOX_STATES)
    id = 0;

  igCheckbox(label, &g_checkbox_states[id]);
  JS_FreeCString(ctx, label);
  return JS_UNDEFINED;
}

static JSValue js_nk_get_checkbox_state(JSContext *ctx, JSValueConst this_val,
                                        int argc, JSValueConst *argv) {
  if (argc < 1)
    return JS_NewBool(ctx, 0);
  int id = 0;
  JS_ToInt32(ctx, &id, argv[0]);
  if (id < 0 || id >= MAX_CHECKBOX_STATES)
    return JS_NewBool(ctx, 0);
  return JS_NewBool(ctx, g_checkbox_states[id]);
}

static JSValue js_nk_set_checkbox_state(JSContext *ctx, JSValueConst this_val,
                                        int argc, JSValueConst *argv) {
  if (argc < 2)
    return JS_UNDEFINED;
  int id = 0;
  JS_ToInt32(ctx, &id, argv[0]);
  if (id < 0 || id >= MAX_CHECKBOX_STATES)
    return JS_UNDEFINED;
  g_checkbox_states[id] = JS_ToBool(ctx, argv[1]);
  return JS_UNDEFINED;
}

static JSValue js_nk_slider(JSContext *ctx, JSValueConst this_val, int argc,
                            JSValueConst *argv) {
  layout_advance();
  if (argc < 4)
    return JS_NULL;
  int id = 0;
  JS_ToInt32(ctx, &id, argv[0]);
  if (id < 0 || id >= MAX_SLIDER_STATES)
    return JS_NULL;

  double vmin, vmax, step;
  JS_ToFloat64(ctx, &vmin, argv[1]);
  JS_ToFloat64(ctx, &vmax, argv[2]);
  JS_ToFloat64(ctx, &step, argv[3]);

  const char *fmt = "%.2f";
  if (argc > 4) {
    fmt = JS_ToCString(ctx, argv[4]);
    if (!fmt)
      fmt = "%.2f";
  }

  char label[32];
  snprintf(label, sizeof(label), "##slider_%d", id);
  bool changed = igSliderFloat(label, &g_slider_states[id], (float)vmin,
                               (float)vmax, fmt, 0);

  if (argc > 4)
    JS_FreeCString(ctx, fmt);

  JSValue result = JS_NewObject(ctx);
  JS_SetPropertyStr(ctx, result, "value",
                    JS_NewFloat64(ctx, g_slider_states[id]));
  JS_SetPropertyStr(ctx, result, "changed", JS_NewBool(ctx, changed));
  return result;
}

static JSValue js_nk_set_slider_state(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv) {
  if (argc < 2)
    return JS_UNDEFINED;
  int id = 0;
  JS_ToInt32(ctx, &id, argv[0]);
  if (id < 0 || id >= MAX_SLIDER_STATES)
    return JS_UNDEFINED;
  double v;
  JS_ToFloat64(ctx, &v, argv[1]);
  g_slider_states[id] = (float)v;
  return JS_UNDEFINED;
}

static JSValue js_nk_textbox(JSContext *ctx, JSValueConst this_val, int argc,
                             JSValueConst *argv) {
  layout_advance();
  if (argc < 1)
    return JS_NULL;
  int id = 0;
  JS_ToInt32(ctx, &id, argv[0]);
  if (id < 0 || id >= MAX_TEXTBOX_STATES)
    return JS_NULL;

  char label[32];
  snprintf(label, sizeof(label), "##textbox_%d", id);

  bool changed =
      igInputText(label, g_textbox_states[id], MAX_TEXTBOX_LEN, 0, NULL, NULL);
  bool submitted =
      changed && igIsKeyPressed_Bool(ImGuiKey_Enter, false);

  JSValue result = JS_NewObject(ctx);
  JS_SetPropertyStr(ctx, result, "text",
                    JS_NewString(ctx, g_textbox_states[id]));
  JS_SetPropertyStr(ctx, result, "changed", JS_NewBool(ctx, changed));
  JS_SetPropertyStr(ctx, result, "submitted", JS_NewBool(ctx, submitted));
  return result;
}

static JSValue js_nk_set_textbox_state(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv) {
  if (argc < 2)
    return JS_UNDEFINED;
  int id = 0;
  JS_ToInt32(ctx, &id, argv[0]);
  if (id < 0 || id >= MAX_TEXTBOX_STATES)
    return JS_UNDEFINED;
  const char *text = JS_ToCString(ctx, argv[1]);
  if (!text)
    return JS_EXCEPTION;
  strncpy(g_textbox_states[id], text, MAX_TEXTBOX_LEN - 1);
  g_textbox_states[id][MAX_TEXTBOX_LEN - 1] = '\0';
  JS_FreeCString(ctx, text);
  return JS_UNDEFINED;
}

static JSValue js_nk_draw_rect(JSContext *ctx, JSValueConst this_val, int argc,
                               JSValueConst *argv) {
  if (argc < 8)
    return JS_UNDEFINED;
  double x, y, w, h;
  int r, g, b, a;
  JS_ToFloat64(ctx, &x, argv[0]);
  JS_ToFloat64(ctx, &y, argv[1]);
  JS_ToFloat64(ctx, &w, argv[2]);
  JS_ToFloat64(ctx, &h, argv[3]);
  JS_ToInt32(ctx, &r, argv[4]);
  JS_ToInt32(ctx, &g, argv[5]);
  JS_ToInt32(ctx, &b, argv[6]);
  JS_ToInt32(ctx, &a, argv[7]);

  ImDrawList *dl = igGetWindowDrawList();
  if (!dl)
    dl = igGetForegroundDrawList_ViewportPtr(NULL);
  if (!dl)
    return JS_UNDEFINED;

  ImVec2_c p_min = {(float)x, (float)y};
  ImVec2_c p_max = {(float)(x + w), (float)(y + h)};
  ImU32 col = ((ImU32)a << 24) | ((ImU32)b << 16) | ((ImU32)g << 8) | (ImU32)r;
  ImDrawList_AddRectFilled(dl, p_min, p_max, col, 0.0f, 0);
  return JS_UNDEFINED;
}

static JSValue js_nk_draw_control_text(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv) {
  if (argc < 5)
    return JS_UNDEFINED;
  const char *text = JS_ToCString(ctx, argv[0]);
  if (!text)
    return JS_EXCEPTION;

  double x, y, w, h;
  JS_ToFloat64(ctx, &x, argv[1]);
  JS_ToFloat64(ctx, &y, argv[2]);
  JS_ToFloat64(ctx, &w, argv[3]);
  JS_ToFloat64(ctx, &h, argv[4]);

  ImDrawList *dl = igGetWindowDrawList();
  if (!dl)
    dl = igGetForegroundDrawList_ViewportPtr(NULL);

  if (dl) {

    ImU32 col = 0xFFFFFFFF;
    ImVec2_c pos = {(float)x, (float)y};
    ImDrawList_AddText_Vec2(dl, pos, col, text, NULL);
  }

  JS_FreeCString(ctx, text);
  return JS_UNDEFINED;
}

static JSValue js_nk_set_style_color(JSContext *ctx, JSValueConst this_val,
                                     int argc, JSValueConst *argv) {

  return JS_UNDEFINED;
}

static JSValue js_nk_get_style_color(JSContext *ctx, JSValueConst this_val,
                                     int argc, JSValueConst *argv) {

  JSValue arr = JS_NewArray(ctx);
  JS_SetPropertyUint32(ctx, arr, 0, JS_NewInt32(ctx, 255));
  JS_SetPropertyUint32(ctx, arr, 1, JS_NewInt32(ctx, 255));
  JS_SetPropertyUint32(ctx, arr, 2, JS_NewInt32(ctx, 255));
  JS_SetPropertyUint32(ctx, arr, 3, JS_NewInt32(ctx, 255));
  return arr;
}

static JSValue js_nk_begin_panel(JSContext *ctx, JSValueConst this_val,
                                 int argc, JSValueConst *argv) {
  const char *name = "panel";
  if (argc > 0) {
    name = JS_ToCString(ctx, argv[0]);
    if (!name)
      return JS_EXCEPTION;
  }
  ImVec2_c size = {0, 0};
  igBeginChild_Str(name, size, 0, 0);
  if (argc > 0)
    JS_FreeCString(ctx, name);
  return JS_UNDEFINED;
}

static JSValue js_nk_end_panel(JSContext *ctx, JSValueConst this_val, int argc,
                               JSValueConst *argv) {
  igEndChild();
  return JS_UNDEFINED;
}

static JSValue js_nk_is_any_hovered(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv) {
  if (!g_imgui.initialized)
    return JS_NewBool(ctx, 0);
  ImGuiIO *io = igGetIO_Nil();
  return JS_NewBool(ctx, io->WantCaptureMouse);
}

static JSValue js_imgui_show_demo_window(JSContext *ctx, JSValueConst this_val,
                                         int argc, JSValueConst *argv) {
  igShowDemoWindow(NULL);
  return JS_UNDEFINED;
}

static JSValue js_imgui_get_font_scale(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv) {
  ImGuiStyle *style = igGetStyle();
  return JS_NewFloat64(ctx, style->FontScaleMain);
}

static JSValue js_imgui_set_font_scale(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv) {
  if (argc < 1)
    return JS_UNDEFINED;
  double scale;
  JS_ToFloat64(ctx, &scale, argv[0]);
  ImGuiStyle *style = igGetStyle();
  style->FontScaleMain = (float)scale;
  return JS_UNDEFINED;
}

static JSValue js_imgui_want_capture_mouse(JSContext *ctx,
                                           JSValueConst this_val, int argc,
                                           JSValueConst *argv) {
  if (!g_imgui.initialized)
    return JS_NewBool(ctx, 0);
  ImGuiIO *io = igGetIO_Nil();
  return JS_NewBool(ctx, io->WantCaptureMouse);
}

static JSValue js_imgui_want_capture_keyboard(JSContext *ctx,
                                              JSValueConst this_val, int argc,
                                              JSValueConst *argv) {
  if (!g_imgui.initialized)
    return JS_NewBool(ctx, 0);
  ImGuiIO *io = igGetIO_Nil();
  return JS_NewBool(ctx, io->WantCaptureKeyboard);
}

static JSValue js_imgui_text_colored(JSContext *ctx, JSValueConst this_val,
                                     int argc, JSValueConst *argv) {
  if (argc < 5)
    return JS_UNDEFINED;
  double r, g, b, a;
  JS_ToFloat64(ctx, &r, argv[0]);
  JS_ToFloat64(ctx, &g, argv[1]);
  JS_ToFloat64(ctx, &b, argv[2]);
  JS_ToFloat64(ctx, &a, argv[3]);
  const char *str = JS_ToCString(ctx, argv[4]);
  if (!str)
    return JS_EXCEPTION;
  ImVec4_c col = {(float)r, (float)g, (float)b, (float)a};
  igTextColored(col, "%s", str);
  JS_FreeCString(ctx, str);
  return JS_UNDEFINED;
}

static JSValue js_imgui_begin(JSContext *ctx, JSValueConst this_val, int argc,
                              JSValueConst *argv) {
  const char *title = "Window";
  if (argc > 0) {
    title = JS_ToCString(ctx, argv[0]);
    if (!title)
      return JS_EXCEPTION;
  }
  int flags = 0;
  if (argc > 1)
    JS_ToInt32(ctx, &flags, argv[1]);
  bool open = igBegin(title, NULL, flags);
  if (argc > 0)
    JS_FreeCString(ctx, title);
  return JS_NewBool(ctx, open);
}

static JSValue js_imgui_end(JSContext *ctx, JSValueConst this_val, int argc,
                            JSValueConst *argv) {
  igEnd();
  return JS_UNDEFINED;
}

static JSValue js_imgui_separator(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv) {
  igSeparator();
  return JS_UNDEFINED;
}

static JSValue js_imgui_same_line(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv) {
  float offset = 0, spacing = -1;
  if (argc > 0) {
    double v;
    JS_ToFloat64(ctx, &v, argv[0]);
    offset = (float)v;
  }
  if (argc > 1) {
    double v;
    JS_ToFloat64(ctx, &v, argv[1]);
    spacing = (float)v;
  }
  igSameLine(offset, spacing);
  return JS_UNDEFINED;
}

static JSValue js_imgui_begin_table(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv) {
  if (argc < 2)
    return JS_NewBool(ctx, 0);
  const char *label = JS_ToCString(ctx, argv[0]);
  if (!label)
    return JS_EXCEPTION;
  int columns = 1;
  JS_ToInt32(ctx, &columns, argv[1]);
  int flags = 0;
  if (argc > 2)
    JS_ToInt32(ctx, &flags, argv[2]);
  ImVec2_c sz = {0, 0};
  bool open = igBeginTable(label, columns, flags, sz, 0);
  JS_FreeCString(ctx, label);
  return JS_NewBool(ctx, open);
}

static JSValue js_imgui_end_table(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv) {
  igEndTable();
  return JS_UNDEFINED;
}

static JSValue js_imgui_table_setup_column(JSContext *ctx,
                                           JSValueConst this_val, int argc,
                                           JSValueConst *argv) {
  const char *label = "";
  if (argc > 0) {
    label = JS_ToCString(ctx, argv[0]);
    if (!label)
      return JS_EXCEPTION;
  }
  int flags = 0;
  if (argc > 1)
    JS_ToInt32(ctx, &flags, argv[1]);
  float w = 0;
  if (argc > 2) {
    double v;
    JS_ToFloat64(ctx, &v, argv[2]);
    w = (float)v;
  }
  igTableSetupColumn(label, flags, w, 0);
  if (argc > 0)
    JS_FreeCString(ctx, label);
  return JS_UNDEFINED;
}

static JSValue js_imgui_table_headers_row(JSContext *ctx, JSValueConst this_val,
                                          int argc, JSValueConst *argv) {
  igTableHeadersRow();
  return JS_UNDEFINED;
}

static JSValue js_imgui_table_next_row(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv) {
  igTableNextRow(0, 0);
  return JS_UNDEFINED;
}

static JSValue js_imgui_table_next_column(JSContext *ctx, JSValueConst this_val,
                                          int argc, JSValueConst *argv) {
  return JS_NewBool(ctx, igTableNextColumn());
}

static JSValue js_imgui_collapsing_header(JSContext *ctx, JSValueConst this_val,
                                          int argc, JSValueConst *argv) {
  if (argc < 1)
    return JS_NewBool(ctx, 0);
  const char *label = JS_ToCString(ctx, argv[0]);
  if (!label)
    return JS_EXCEPTION;
  bool open = igCollapsingHeader_TreeNodeFlags(label, 0);
  JS_FreeCString(ctx, label);
  return JS_NewBool(ctx, open);
}

static JSValue js_imgui_tree_node(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv) {
  if (argc < 1)
    return JS_NewBool(ctx, 0);
  const char *label = JS_ToCString(ctx, argv[0]);
  if (!label)
    return JS_EXCEPTION;
  bool open = igTreeNode_Str(label);
  JS_FreeCString(ctx, label);
  return JS_NewBool(ctx, open);
}

static JSValue js_imgui_tree_pop(JSContext *ctx, JSValueConst this_val,
                                 int argc, JSValueConst *argv) {
  igTreePop();
  return JS_UNDEFINED;
}

static JSValue js_imgui_set_next_window_pos(JSContext *ctx,
                                            JSValueConst this_val, int argc,
                                            JSValueConst *argv) {
  if (argc < 2)
    return JS_UNDEFINED;
  double x, y;
  JS_ToFloat64(ctx, &x, argv[0]);
  JS_ToFloat64(ctx, &y, argv[1]);
  ImVec2_c pos = {(float)x, (float)y};
  ImVec2_c pivot = {0, 0};
  igSetNextWindowPos(pos, ImGuiCond_FirstUseEver, pivot);
  return JS_UNDEFINED;
}

static JSValue js_imgui_set_next_window_size(JSContext *ctx,
                                             JSValueConst this_val, int argc,
                                             JSValueConst *argv) {
  if (argc < 2)
    return JS_UNDEFINED;
  double w, h;
  JS_ToFloat64(ctx, &w, argv[0]);
  JS_ToFloat64(ctx, &h, argv[1]);
  ImVec2_c sz = {(float)w, (float)h};
  igSetNextWindowSize(sz, ImGuiCond_FirstUseEver);
  return JS_UNDEFINED;
}

static JSValue js_imgui_checkbox(JSContext *ctx, JSValueConst this_val,
                                 int argc, JSValueConst *argv) {
  if (argc < 2)
    return JS_UNDEFINED;
  const char *label = JS_ToCString(ctx, argv[0]);
  if (!label)
    return JS_EXCEPTION;
  bool checked = JS_ToBool(ctx, argv[1]);
  bool clicked = igCheckbox(label, &checked);
  JS_FreeCString(ctx, label);
  JSValue result = JS_NewObject(ctx);
  JS_SetPropertyStr(ctx, result, "clicked", JS_NewBool(ctx, clicked));
  JS_SetPropertyStr(ctx, result, "checked", JS_NewBool(ctx, checked));
  return result;
}

static JSValue js_imgui_slider_float(JSContext *ctx, JSValueConst this_val,
                                     int argc, JSValueConst *argv) {
  if (argc < 4)
    return JS_UNDEFINED;
  const char *label = JS_ToCString(ctx, argv[0]);
  if (!label)
    return JS_EXCEPTION;
  double val, vmin, vmax;
  JS_ToFloat64(ctx, &val, argv[1]);
  JS_ToFloat64(ctx, &vmin, argv[2]);
  JS_ToFloat64(ctx, &vmax, argv[3]);
  float fval = (float)val;
  bool changed =
      igSliderFloat(label, &fval, (float)vmin, (float)vmax, "%.2f", 0);
  JS_FreeCString(ctx, label);
  JSValue result = JS_NewObject(ctx);
  JS_SetPropertyStr(ctx, result, "changed", JS_NewBool(ctx, changed));
  JS_SetPropertyStr(ctx, result, "value", JS_NewFloat64(ctx, fval));
  return result;
}

static JSValue js_imgui_set_keyboard_focus_here(JSContext *ctx, JSValueConst this_val,
                                                int argc, JSValueConst *argv) {
  int offset = 0;
  if (argc > 0) JS_ToInt32(ctx, &offset, argv[0]);
  igSetKeyboardFocusHere(offset);
  return JS_UNDEFINED;
}

static JSValue js_imgui_input_text(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv) {
  if (argc < 2)
    return JS_UNDEFINED;
  const char *label = JS_ToCString(ctx, argv[0]);
  if (!label)
    return JS_EXCEPTION;
  const char *current = JS_ToCString(ctx, argv[1]);
  if (!current) {
    JS_FreeCString(ctx, label);
    return JS_EXCEPTION;
  }
  char buf[256];
  strncpy(buf, current, 255);
  buf[255] = '\0';
  bool changed = igInputText(label, buf, 256, 0, NULL, NULL);
  JS_FreeCString(ctx, label);
  JS_FreeCString(ctx, current);
  JSValue result = JS_NewObject(ctx);
  JS_SetPropertyStr(ctx, result, "changed", JS_NewBool(ctx, changed));
  JS_SetPropertyStr(ctx, result, "text", JS_NewString(ctx, buf));
  return result;
}

static void js_register_nk_constants(JSContext *ctx, JSValue obj) {

  JS_SetPropertyStr(ctx, obj, "OPT_ALIGNCENTER", JS_NewInt32(ctx, 0x01));
  JS_SetPropertyStr(ctx, obj, "OPT_ALIGNRIGHT", JS_NewInt32(ctx, 0x02));
  JS_SetPropertyStr(ctx, obj, "OPT_NOINTERACT", JS_NewInt32(ctx, 0x04));
  JS_SetPropertyStr(ctx, obj, "OPT_NOFRAME", JS_NewInt32(ctx, 0x08));
  JS_SetPropertyStr(ctx, obj, "OPT_NORESIZE", JS_NewInt32(ctx, 0x10));
  JS_SetPropertyStr(ctx, obj, "OPT_NOSCROLL", JS_NewInt32(ctx, 0x20));
  JS_SetPropertyStr(ctx, obj, "OPT_NOCLOSE", JS_NewInt32(ctx, 0x40));
  JS_SetPropertyStr(ctx, obj, "OPT_NOTITLE", JS_NewInt32(ctx, 0x80));

  JS_SetPropertyStr(ctx, obj, "KEY_RETURN", JS_NewInt32(ctx, 0));
  JS_SetPropertyStr(ctx, obj, "KEY_BACKSPACE", JS_NewInt32(ctx, 1));

  JS_SetPropertyStr(ctx, obj, "MOUSE_LEFT", JS_NewInt32(ctx, 0));
  JS_SetPropertyStr(ctx, obj, "MOUSE_RIGHT", JS_NewInt32(ctx, 1));

  JS_SetPropertyStr(ctx, obj, "COLOR_TEXT", JS_NewInt32(ctx, 0));
}

static void js_register_imgui_constants(JSContext *ctx, JSValue obj) {

  JS_SetPropertyStr(ctx, obj, "WINDOW_NONE", JS_NewInt32(ctx, 0));
  JS_SetPropertyStr(ctx, obj, "WINDOW_NO_TITLE_BAR",
                    JS_NewInt32(ctx, 1 << 0));
  JS_SetPropertyStr(ctx, obj, "WINDOW_NO_RESIZE", JS_NewInt32(ctx, 1 << 1));
  JS_SetPropertyStr(ctx, obj, "WINDOW_NO_MOVE", JS_NewInt32(ctx, 1 << 2));
  JS_SetPropertyStr(ctx, obj, "WINDOW_NO_SCROLLBAR",
                    JS_NewInt32(ctx, 1 << 3));
  JS_SetPropertyStr(ctx, obj, "WINDOW_NO_COLLAPSE", JS_NewInt32(ctx, 1 << 5));
  JS_SetPropertyStr(ctx, obj, "WINDOW_ALWAYS_AUTO_RESIZE",
                    JS_NewInt32(ctx, 1 << 6));

  JS_SetPropertyStr(ctx, obj, "COL_PLOT_HISTOGRAM", JS_NewInt32(ctx, ImGuiCol_PlotHistogram));
  JS_SetPropertyStr(ctx, obj, "COL_FRAME_BG", JS_NewInt32(ctx, ImGuiCol_FrameBg));

  JS_SetPropertyStr(ctx, obj, "TABLE_BORDERS",
                    JS_NewInt32(ctx, (1 << 7) | (1 << 8) | (1 << 9) | (1 << 10)));
  JS_SetPropertyStr(ctx, obj, "TABLE_ROW_BG", JS_NewInt32(ctx, 1 << 6));
  JS_SetPropertyStr(ctx, obj, "TABLE_RESIZABLE", JS_NewInt32(ctx, 1 << 0));
}

static JSValue js_imgui_progress_bar(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv) {
  double fraction = 0;
  if (argc > 0) JS_ToFloat64(ctx, &fraction, argv[0]);
  double w = -1, h = 0;
  if (argc > 1) JS_ToFloat64(ctx, &w, argv[1]);
  if (argc > 2) JS_ToFloat64(ctx, &h, argv[2]);
  ImVec2_c size = { (float)w, (float)h };
  const char *overlay = NULL;
  if (argc > 3) {
    overlay = JS_ToCString(ctx, argv[3]);
  }
  igProgressBar((float)fraction, size, overlay);
  if (overlay) JS_FreeCString(ctx, overlay);
  return JS_UNDEFINED;
}

static JSValue js_imgui_push_style_color(JSContext *ctx, JSValueConst this_val,
                                          int argc, JSValueConst *argv) {
  int idx = 0;
  double r = 0, g = 0, b = 0, a = 1;
  if (argc > 0) JS_ToInt32(ctx, &idx, argv[0]);
  if (argc > 1) JS_ToFloat64(ctx, &r, argv[1]);
  if (argc > 2) JS_ToFloat64(ctx, &g, argv[2]);
  if (argc > 3) JS_ToFloat64(ctx, &b, argv[3]);
  if (argc > 4) JS_ToFloat64(ctx, &a, argv[4]);
  ImVec4_c col = { (float)r, (float)g, (float)b, (float)a };
  igPushStyleColor_Vec4(idx, col);
  return JS_UNDEFINED;
}

static JSValue js_imgui_pop_style_color(JSContext *ctx, JSValueConst this_val,
                                         int argc, JSValueConst *argv) {
  int count = 1;
  if (argc > 0) JS_ToInt32(ctx, &count, argv[0]);
  igPopStyleColor(count);
  return JS_UNDEFINED;
}

static JSValue js_imgui_get_cursor_pos(JSContext *ctx, JSValueConst this_val,
                                        int argc, JSValueConst *argv) {
  ImVec2_c pos = igGetCursorScreenPos();
  JSValue obj = JS_NewObject(ctx);
  JS_SetPropertyStr(ctx, obj, "x", JS_NewFloat64(ctx, pos.x));
  JS_SetPropertyStr(ctx, obj, "y", JS_NewFloat64(ctx, pos.y));
  return obj;
}

static JSValue js_imgui_dummy(JSContext *ctx, JSValueConst this_val,
                               int argc, JSValueConst *argv) {
  double w = 0, h = 0;
  if (argc > 0) JS_ToFloat64(ctx, &w, argv[0]);
  if (argc > 1) JS_ToFloat64(ctx, &h, argv[1]);
  ImVec2_c size = { (float)w, (float)h };
  igDummy(size);
  return JS_UNDEFINED;
}

static JSValue js_imgui_begin_group(JSContext *ctx, JSValueConst this_val,
                                     int argc, JSValueConst *argv) {
  igBeginGroup();
  return JS_UNDEFINED;
}

static JSValue js_imgui_end_group(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv) {
  igEndGroup();
  return JS_UNDEFINED;
}

static JSValue js_imgui_selectable(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv) {
  const char *label = (argc > 0) ? JS_ToCString(ctx, argv[0]) : "??";
  int selected = (argc > 1) ? JS_ToBool(ctx, argv[1]) : 0;
  bool clicked = igSelectable_Bool(label, selected, 0, (ImVec2_c){0, 0});
  if (label) JS_FreeCString(ctx, label);
  return JS_NewBool(ctx, clicked);
}

static JSValue js_imgui_begin_drag_drop_source(JSContext *ctx, JSValueConst this_val,
                                                int argc, JSValueConst *argv) {
  int flags = (argc > 0) ? 0 : 0;
  if (argc > 0) JS_ToInt32(ctx, &flags, argv[0]);
  return JS_NewBool(ctx, igBeginDragDropSource(flags));
}

static JSValue js_imgui_set_drag_drop_payload(JSContext *ctx, JSValueConst this_val,
                                               int argc, JSValueConst *argv) {
  const char *type = (argc > 0) ? JS_ToCString(ctx, argv[0]) : "DEFAULT";
  int data = 0;
  if (argc > 1) JS_ToInt32(ctx, &data, argv[1]);
  bool result = igSetDragDropPayload(type, &data, sizeof(int), 0);
  if (type) JS_FreeCString(ctx, type);
  return JS_NewBool(ctx, result);
}

static JSValue js_imgui_end_drag_drop_source(JSContext *ctx, JSValueConst this_val,
                                              int argc, JSValueConst *argv) {
  igEndDragDropSource();
  return JS_UNDEFINED;
}

static JSValue js_imgui_begin_drag_drop_target(JSContext *ctx, JSValueConst this_val,
                                                int argc, JSValueConst *argv) {
  return JS_NewBool(ctx, igBeginDragDropTarget());
}

static JSValue js_imgui_accept_drag_drop_payload(JSContext *ctx, JSValueConst this_val,
                                                  int argc, JSValueConst *argv) {
  const char *type = (argc > 0) ? JS_ToCString(ctx, argv[0]) : "DEFAULT";
  const ImGuiPayload *payload = igAcceptDragDropPayload(type, 0);
  if (type) JS_FreeCString(ctx, type);
  if (payload && payload->Data) {
    int data = *(int *)payload->Data;
    return JS_NewInt32(ctx, data);
  }
  return JS_NULL;
}

static JSValue js_imgui_end_drag_drop_target(JSContext *ctx, JSValueConst this_val,
                                              int argc, JSValueConst *argv) {
  igEndDragDropTarget();
  return JS_UNDEFINED;
}

#define REG(obj, name, fn, nargs)                                              \
  JS_SetPropertyStr(ctx, obj, name, JS_NewCFunction(ctx, fn, name, nargs))

int js_init_imgui_module(JSContext *ctx) {
  JSValue global = JS_GetGlobalObject(ctx);

  JSValue imgui = JS_NewObject(ctx);

  REG(imgui, "init", js_nk_init, 0);
  REG(imgui, "shutdown", js_noop, 0);
  REG(imgui, "begin_frame", js_nk_begin_frame, 1);
  REG(imgui, "end_frame", js_nk_end_frame, 0);
  REG(imgui, "render", js_nk_render, 2);

  REG(imgui, "begin_window", js_nk_begin_window, 6);
  REG(imgui, "end_window", js_nk_end_window, 0);
  REG(imgui, "get_current_container", js_nk_get_current_container, 0);

  REG(imgui, "begin", js_imgui_begin, 2);
  REG(imgui, "end", js_imgui_end, 0);

  REG(imgui, "layout_row", js_nk_layout_row, 3);
  REG(imgui, "layout_set_next", js_nk_layout_set_next, 4);
  REG(imgui, "layout_next", js_nk_layout_next, 0);
  REG(imgui, "layout_begin_column", js_nk_layout_begin_column, 0);
  REG(imgui, "layout_end_column", js_nk_layout_end_column, 0);

  REG(imgui, "button", js_nk_button, 1);
  REG(imgui, "label", js_nk_label, 1);
  REG(imgui, "text", js_nk_text, 1);
  REG(imgui, "text_colored", js_imgui_text_colored, 5);
  REG(imgui, "separator", js_imgui_separator, 0);
  REG(imgui, "same_line", js_imgui_same_line, 2);
  REG(imgui, "checkbox", js_nk_checkbox, 2);
  REG(imgui, "get_checkbox_state", js_nk_get_checkbox_state, 1);
  REG(imgui, "set_checkbox_state", js_nk_set_checkbox_state, 2);
  REG(imgui, "slider", js_nk_slider, 5);
  REG(imgui, "set_slider_state", js_nk_set_slider_state, 2);
  REG(imgui, "slider_float", js_imgui_slider_float, 4);
  REG(imgui, "textbox", js_nk_textbox, 1);
  REG(imgui, "set_textbox_state", js_nk_set_textbox_state, 2);
  REG(imgui, "input_text", js_imgui_input_text, 2);
  REG(imgui, "set_keyboard_focus_here", js_imgui_set_keyboard_focus_here, 1);

  REG(imgui, "get_cursor_pos", js_imgui_get_cursor_pos, 0);
  REG(imgui, "dummy", js_imgui_dummy, 2);
  REG(imgui, "begin_group", js_imgui_begin_group, 0);
  REG(imgui, "end_group", js_imgui_end_group, 0);
  REG(imgui, "progress_bar", js_imgui_progress_bar, 4);
  REG(imgui, "push_style_color", js_imgui_push_style_color, 5);
  REG(imgui, "pop_style_color", js_imgui_pop_style_color, 1);

  REG(imgui, "draw_rect", js_nk_draw_rect, 8);
  REG(imgui, "draw_control_text", js_nk_draw_control_text, 7);
  REG(imgui, "draw_text", js_nk_draw_control_text, 7);

  REG(imgui, "set_style_color", js_nk_set_style_color, 5);
  REG(imgui, "get_style_color", js_nk_get_style_color, 1);

  REG(imgui, "begin_panel", js_nk_begin_panel, 1);
  REG(imgui, "end_panel", js_nk_end_panel, 0);

  REG(imgui, "begin_table", js_imgui_begin_table, 3);
  REG(imgui, "end_table", js_imgui_end_table, 0);
  REG(imgui, "table_setup_column", js_imgui_table_setup_column, 3);
  REG(imgui, "table_headers_row", js_imgui_table_headers_row, 0);
  REG(imgui, "table_next_row", js_imgui_table_next_row, 0);
  REG(imgui, "table_next_column", js_imgui_table_next_column, 0);

  REG(imgui, "collapsing_header", js_imgui_collapsing_header, 1);
  REG(imgui, "tree_node", js_imgui_tree_node, 1);
  REG(imgui, "tree_pop", js_imgui_tree_pop, 0);

  REG(imgui, "selectable", js_imgui_selectable, 2);

  REG(imgui, "begin_drag_drop_source", js_imgui_begin_drag_drop_source, 1);
  REG(imgui, "set_drag_drop_payload", js_imgui_set_drag_drop_payload, 2);
  REG(imgui, "end_drag_drop_source", js_imgui_end_drag_drop_source, 0);
  REG(imgui, "begin_drag_drop_target", js_imgui_begin_drag_drop_target, 0);
  REG(imgui, "accept_drag_drop_payload", js_imgui_accept_drag_drop_payload, 1);
  REG(imgui, "end_drag_drop_target", js_imgui_end_drag_drop_target, 0);

  REG(imgui, "set_next_window_pos", js_imgui_set_next_window_pos, 2);
  REG(imgui, "set_next_window_size", js_imgui_set_next_window_size, 2);
  REG(imgui, "show_demo_window", js_imgui_show_demo_window, 0);

  REG(imgui, "get_font_global_scale", js_imgui_get_font_scale, 0);
  REG(imgui, "set_font_global_scale", js_imgui_set_font_scale, 1);

  REG(imgui, "want_capture_mouse", js_imgui_want_capture_mouse, 0);
  REG(imgui, "want_capture_keyboard", js_imgui_want_capture_keyboard, 0);
  REG(imgui, "is_any_hovered", js_nk_is_any_hovered, 0);


  js_register_nk_constants(ctx, imgui);
  js_register_imgui_constants(ctx, imgui);

  JS_SetPropertyStr(ctx, global, "imgui", imgui);
  JS_FreeValue(ctx, global);

  return 0;
}
