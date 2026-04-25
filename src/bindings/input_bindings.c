// input_bindings.c - 输入系统 JS 绑定
//
// 暴露鼠标/键盘输入给 JS 层
//
// API:
//   input.mouse_pos()         - 鼠标位置 (当前坐标空间，自动转换)
//   input.mouse_pos_raw()     - 鼠标位置 (窗口像素，原始值)
//   input.key_pressed(code)   - 按键刚按下
//   input.key_released(code)  - 按键刚释放
//   input.key_down(code)      - 按键持续按下
//   input.mouse_pressed(btn)  - 鼠标按钮刚按下 (0=左, 1=中, 2=右)
//   input.mouse_down(btn)     - 鼠标按钮持续按下

#include "input_bindings.h"
#include "../log.h"
#include "bedrock/helpers.h"
#include "bedrock/input/input.h"
#include "quickjs.h"
#include <stdio.h>

// ============================================================================
// input.mouse_x() -> float
// ============================================================================

static JSValue js_input_mouse_x(JSContext *ctx, JSValueConst this_val, int argc,
                                JSValueConst *argv) {
  (void)this_val;
  (void)argc;
  (void)argv;
  if (!input_state)
    return JS_NewFloat64(ctx, 0.0);
  return JS_NewFloat64(ctx, input_state->mouse_x);
}

// ============================================================================
// input.mouse_y() -> float
// ============================================================================

static JSValue js_input_mouse_y(JSContext *ctx, JSValueConst this_val, int argc,
                                JSValueConst *argv) {
  (void)this_val;
  (void)argc;
  (void)argv;
  if (!input_state)
    return JS_NewFloat64(ctx, 0.0);
  return JS_NewFloat64(ctx, input_state->mouse_y);
}

// ============================================================================
// input.mouse_pos() -> {x, y} (当前坐标空间)
// 自动根据当前 push 的坐标空间转换鼠标位置
// ============================================================================

static JSValue js_input_mouse_pos(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv) {
  (void)this_val;
  (void)argc;
  (void)argv;

  // 使用 C 层的坐标空间转换函数
  Vec2 pos = {0, 0};
  if (input_state) {
    mouse_pos_in_current_space(pos);
  }

  JSValue ret = JS_NewObject(ctx);
  JS_SetPropertyStr(ctx, ret, "x", JS_NewFloat64(ctx, pos[0]));
  JS_SetPropertyStr(ctx, ret, "y", JS_NewFloat64(ctx, pos[1]));
  return ret;
}

// ============================================================================
// input.mouse_pos_raw() -> {x, y} (窗口像素)
// 返回原始窗口像素坐标，不做任何转换
// ============================================================================

static JSValue js_input_mouse_pos_raw(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv) {
  (void)this_val;
  (void)argc;
  (void)argv;
  JSValue ret = JS_NewObject(ctx);
  if (input_state) {
    JS_SetPropertyStr(ctx, ret, "x", JS_NewFloat64(ctx, input_state->mouse_x));
    JS_SetPropertyStr(ctx, ret, "y", JS_NewFloat64(ctx, input_state->mouse_y));
  } else {
    JS_SetPropertyStr(ctx, ret, "x", JS_NewFloat64(ctx, 0));
    JS_SetPropertyStr(ctx, ret, "y", JS_NewFloat64(ctx, 0));
  }
  return ret;
}

// ============================================================================
// input.key_pressed(code) -> bool
// ============================================================================

static JSValue js_input_key_pressed(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv) {
  if (argc < 1) {
    return JS_ThrowTypeError(ctx, "input.key_pressed requires key code");
  }

  int32_t code;
  if (JS_ToInt32(ctx, &code, argv[0]) < 0)
    return JS_EXCEPTION;
  if (!input_state)
    return JS_NewBool(ctx, false);

  return JS_NewBool(ctx, key_pressed((Key_Code)code));
}

// ============================================================================
// input.key_released(code) -> bool
// ============================================================================

static JSValue js_input_key_released(JSContext *ctx, JSValueConst this_val,
                                     int argc, JSValueConst *argv) {
  if (argc < 1) {
    return JS_ThrowTypeError(ctx, "input.key_released requires key code");
  }

  int32_t code;
  if (JS_ToInt32(ctx, &code, argv[0]) < 0)
    return JS_EXCEPTION;
  if (!input_state)
    return JS_NewBool(ctx, false);

  return JS_NewBool(ctx, key_released((Key_Code)code));
}

// ============================================================================
// input.key_down(code) -> bool
// ============================================================================

static JSValue js_input_key_down(JSContext *ctx, JSValueConst this_val,
                                 int argc, JSValueConst *argv) {
  if (argc < 1) {
    return JS_ThrowTypeError(ctx, "input.key_down requires key code");
  }

  int32_t code;
  if (JS_ToInt32(ctx, &code, argv[0]) < 0)
    return JS_EXCEPTION;
  if (!input_state)
    return JS_NewBool(ctx, false);

  return JS_NewBool(ctx, key_down((Key_Code)code));
}

// ============================================================================
// input.mouse_pressed(button) -> bool
// button: 0=左, 1=中, 2=右
// ============================================================================

static JSValue js_input_mouse_pressed(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv) {
  if (argc < 1) {
    return JS_ThrowTypeError(
        ctx, "input.mouse_pressed requires button (0=left, 1=middle, 2=right)");
  }

  int32_t button;
  if (JS_ToInt32(ctx, &button, argv[0]) < 0)
    return JS_EXCEPTION;

  Key_Code code;
  switch (button) {
  case 0:
    code = KEY_LEFT_MOUSE;
    break;
  case 1:
    code = KEY_MIDDLE_MOUSE;
    break;
  case 2:
    code = KEY_RIGHT_MOUSE;
    break;
  default:
    return JS_NewBool(ctx, false);
  }

  if (!input_state)
    return JS_NewBool(ctx, false);
  return JS_NewBool(ctx, key_pressed(code));
}

// ============================================================================
// input.mouse_released(button) -> bool
// ============================================================================

static JSValue js_input_mouse_released(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv) {
  if (argc < 1) {
    return JS_ThrowTypeError(ctx, "input.mouse_released requires button");
  }

  int32_t button;
  if (JS_ToInt32(ctx, &button, argv[0]) < 0)
    return JS_EXCEPTION;

  Key_Code code;
  switch (button) {
  case 0:
    code = KEY_LEFT_MOUSE;
    break;
  case 1:
    code = KEY_MIDDLE_MOUSE;
    break;
  case 2:
    code = KEY_RIGHT_MOUSE;
    break;
  default:
    return JS_NewBool(ctx, false);
  }

  if (!input_state)
    return JS_NewBool(ctx, false);
  return JS_NewBool(ctx, key_released(code));
}

// ============================================================================
// input.mouse_down(button) -> bool
// ============================================================================

static JSValue js_input_mouse_down(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv) {
  if (argc < 1) {
    return JS_ThrowTypeError(ctx, "input.mouse_down requires button");
  }

  int32_t button;
  if (JS_ToInt32(ctx, &button, argv[0]) < 0)
    return JS_EXCEPTION;

  Key_Code code;
  switch (button) {
  case 0:
    code = KEY_LEFT_MOUSE;
    break;
  case 1:
    code = KEY_MIDDLE_MOUSE;
    break;
  case 2:
    code = KEY_RIGHT_MOUSE;
    break;
  default:
    return JS_NewBool(ctx, false);
  }

  if (!input_state)
    return JS_NewBool(ctx, false);
  return JS_NewBool(ctx, key_down(code));
}

// ============================================================================
// input.scroll() -> {x, y}
// ============================================================================

static JSValue js_input_scroll(JSContext *ctx, JSValueConst this_val, int argc,
                               JSValueConst *argv) {
  (void)this_val;
  (void)argc;
  (void)argv;
  JSValue ret = JS_NewObject(ctx);
  if (input_state) {
    JS_SetPropertyStr(ctx, ret, "x", JS_NewFloat64(ctx, input_state->scroll_x));
    JS_SetPropertyStr(ctx, ret, "y", JS_NewFloat64(ctx, input_state->scroll_y));
  } else {
    JS_SetPropertyStr(ctx, ret, "x", JS_NewFloat64(ctx, 0));
    JS_SetPropertyStr(ctx, ret, "y", JS_NewFloat64(ctx, 0));
  }
  return ret;
}

// ============================================================================
// input.scroll_y() -> float
// ============================================================================

static JSValue js_input_scroll_y(JSContext *ctx, JSValueConst this_val, int argc,
                                 JSValueConst *argv) {
  (void)this_val;
  (void)argc;
  (void)argv;
  if (!input_state)
    return JS_NewFloat64(ctx, 0.0);
  return JS_NewFloat64(ctx, input_state->scroll_y);
}

// ============================================================================
// input.get_text_input() -> string
// 返回本帧输入的字符，用于 microui 文本框
// ============================================================================

static JSValue js_input_get_text_input(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv) {
  (void)this_val;
  (void)argc;
  (void)argv;
  if (!input_state || input_state->text_input_len == 0)
    return JS_NewString(ctx, "");
  return JS_NewStringLen(ctx, input_state->text_input,
                         input_state->text_input_len);
}

// ============================================================================
// Test Mock API - 注入假鼠标事件 (用于自动化测试)
// ============================================================================

// input.inject_mouse_pos(x, y) — 直接设置鼠标屏幕坐标
static JSValue js_input_inject_mouse_pos(JSContext *ctx, JSValueConst this_val,
                                          int argc, JSValueConst *argv) {
  (void)this_val;
  if (argc < 2) {
    return JS_ThrowTypeError(ctx, "inject_mouse_pos requires 2 arguments (x, y)");
  }
  if (!input_state) return JS_UNDEFINED;

  double x, y;
  JS_ToFloat64(ctx, &x, argv[0]);
  JS_ToFloat64(ctx, &y, argv[1]);
  input_state->mouse_x = (float)x;
  input_state->mouse_y = (float)y;
  return JS_UNDEFINED;
}

// input.inject_mouse_down(btn) — 模拟鼠标按钮按下
// btn: 0=左, 1=中, 2=右
static JSValue js_input_inject_mouse_down(JSContext *ctx, JSValueConst this_val,
                                           int argc, JSValueConst *argv) {
  (void)this_val;
  if (argc < 1) {
    return JS_ThrowTypeError(ctx, "inject_mouse_down requires button (0=left, 1=middle, 2=right)");
  }
  if (!input_state) return JS_UNDEFINED;

  int32_t button;
  if (JS_ToInt32(ctx, &button, argv[0]) < 0) return JS_EXCEPTION;

  Key_Code code;
  switch (button) {
    case 0: code = KEY_LEFT_MOUSE; break;
    case 1: code = KEY_MIDDLE_MOUSE; break;
    case 2: code = KEY_RIGHT_MOUSE; break;
    default: return JS_UNDEFINED;
  }

  input_state->keys[code] |= INPUT_FLAG_DOWN | INPUT_FLAG_PRESSED;
  return JS_UNDEFINED;
}

// input.inject_mouse_up(btn) — 模拟鼠标按钮释放
static JSValue js_input_inject_mouse_up(JSContext *ctx, JSValueConst this_val,
                                         int argc, JSValueConst *argv) {
  (void)this_val;
  if (argc < 1) {
    return JS_ThrowTypeError(ctx, "inject_mouse_up requires button (0=left, 1=middle, 2=right)");
  }
  if (!input_state) return JS_UNDEFINED;

  int32_t button;
  if (JS_ToInt32(ctx, &button, argv[0]) < 0) return JS_EXCEPTION;

  Key_Code code;
  switch (button) {
    case 0: code = KEY_LEFT_MOUSE; break;
    case 1: code = KEY_MIDDLE_MOUSE; break;
    case 2: code = KEY_RIGHT_MOUSE; break;
    default: return JS_UNDEFINED;
  }

  input_state->keys[code] &= ~(INPUT_FLAG_DOWN | INPUT_FLAG_PRESSED);
  input_state->keys[code] |= INPUT_FLAG_RELEASED;
  return JS_UNDEFINED;
}

// input.inject_clear() — 清除所有注入的 transient 状态 (pressed/released)
static JSValue js_input_inject_clear(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv) {
  (void)this_val;
  (void)argc;
  (void)argv;
  if (!input_state) return JS_UNDEFINED;

  // 只清除 transient flags，保留 DOWN
  for (int i = 0; i < MAX_KEYCODES; i++) {
    input_state->keys[i] &= INPUT_FLAG_DOWN;  // 只保留持续按下状态
  }
  return JS_UNDEFINED;
}

// ============================================================================
// 模块初始化
// ============================================================================

int js_init_input_module(JSContext *ctx) {
  JSValue global = JS_GetGlobalObject(ctx);
  JSValue obj = JS_NewObject(ctx);

  // 鼠标位置
  JS_SetPropertyStr(ctx, obj, "mouse_x",
                    JS_NewCFunction(ctx, js_input_mouse_x, "mouse_x", 0));
  JS_SetPropertyStr(ctx, obj, "mouse_y",
                    JS_NewCFunction(ctx, js_input_mouse_y, "mouse_y", 0));
  JS_SetPropertyStr(ctx, obj, "mouse_pos",
                    JS_NewCFunction(ctx, js_input_mouse_pos, "mouse_pos", 0));
  JS_SetPropertyStr(
      ctx, obj, "mouse_pos_raw",
      JS_NewCFunction(ctx, js_input_mouse_pos_raw, "mouse_pos_raw", 0));
  JS_SetPropertyStr(ctx, obj, "scroll",
                    JS_NewCFunction(ctx, js_input_scroll, "scroll", 0));
  JS_SetPropertyStr(ctx, obj, "scroll_y",
                    JS_NewCFunction(ctx, js_input_scroll_y, "scroll_y", 0));
  JS_SetPropertyStr(ctx, obj, "get_text_input",
                    JS_NewCFunction(ctx, js_input_get_text_input, "get_text_input", 0));

  // 键盘
  JS_SetPropertyStr(
      ctx, obj, "key_pressed",
      JS_NewCFunction(ctx, js_input_key_pressed, "key_pressed", 1));
  JS_SetPropertyStr(
      ctx, obj, "key_released",
      JS_NewCFunction(ctx, js_input_key_released, "key_released", 1));
  JS_SetPropertyStr(ctx, obj, "key_down",
                    JS_NewCFunction(ctx, js_input_key_down, "key_down", 1));

  // 鼠标按钮
  JS_SetPropertyStr(
      ctx, obj, "mouse_pressed",
      JS_NewCFunction(ctx, js_input_mouse_pressed, "mouse_pressed", 1));
  JS_SetPropertyStr(
      ctx, obj, "mouse_released",
      JS_NewCFunction(ctx, js_input_mouse_released, "mouse_released", 1));
  JS_SetPropertyStr(ctx, obj, "mouse_down",
                    JS_NewCFunction(ctx, js_input_mouse_down, "mouse_down", 1));

  // 键码常量
  JS_SetPropertyStr(ctx, obj, "KEY_SPACE", JS_NewInt32(ctx, KEY_SPACE));
  JS_SetPropertyStr(ctx, obj, "KEY_ESC", JS_NewInt32(ctx, KEY_ESC));
  JS_SetPropertyStr(ctx, obj, "KEY_ENTER", JS_NewInt32(ctx, KEY_ENTER));
  JS_SetPropertyStr(ctx, obj, "KEY_TAB", JS_NewInt32(ctx, KEY_TAB));
  JS_SetPropertyStr(ctx, obj, "KEY_BACKSPACE", JS_NewInt32(ctx, KEY_BACKSPACE));
  JS_SetPropertyStr(ctx, obj, "KEY_DELETE", JS_NewInt32(ctx, KEY_DELETE));
  JS_SetPropertyStr(ctx, obj, "KEY_RIGHT", JS_NewInt32(ctx, KEY_RIGHT));
  JS_SetPropertyStr(ctx, obj, "KEY_LEFT", JS_NewInt32(ctx, KEY_LEFT));
  JS_SetPropertyStr(ctx, obj, "KEY_DOWN", JS_NewInt32(ctx, KEY_DOWN));
  JS_SetPropertyStr(ctx, obj, "KEY_UP", JS_NewInt32(ctx, KEY_UP));

  // 字母键
  JS_SetPropertyStr(ctx, obj, "KEY_A", JS_NewInt32(ctx, KEY_A));
  JS_SetPropertyStr(ctx, obj, "KEY_B", JS_NewInt32(ctx, KEY_B));
  JS_SetPropertyStr(ctx, obj, "KEY_C", JS_NewInt32(ctx, KEY_C));
  JS_SetPropertyStr(ctx, obj, "KEY_D", JS_NewInt32(ctx, KEY_D));
  JS_SetPropertyStr(ctx, obj, "KEY_E", JS_NewInt32(ctx, KEY_E));
  JS_SetPropertyStr(ctx, obj, "KEY_F", JS_NewInt32(ctx, KEY_F));
  JS_SetPropertyStr(ctx, obj, "KEY_G", JS_NewInt32(ctx, KEY_G));
  JS_SetPropertyStr(ctx, obj, "KEY_H", JS_NewInt32(ctx, KEY_H));
  JS_SetPropertyStr(ctx, obj, "KEY_I", JS_NewInt32(ctx, KEY_I));
  JS_SetPropertyStr(ctx, obj, "KEY_J", JS_NewInt32(ctx, KEY_J));
  JS_SetPropertyStr(ctx, obj, "KEY_K", JS_NewInt32(ctx, KEY_K));
  JS_SetPropertyStr(ctx, obj, "KEY_L", JS_NewInt32(ctx, KEY_L));
  JS_SetPropertyStr(ctx, obj, "KEY_M", JS_NewInt32(ctx, KEY_M));
  JS_SetPropertyStr(ctx, obj, "KEY_N", JS_NewInt32(ctx, KEY_N));
  JS_SetPropertyStr(ctx, obj, "KEY_O", JS_NewInt32(ctx, KEY_O));
  JS_SetPropertyStr(ctx, obj, "KEY_P", JS_NewInt32(ctx, KEY_P));
  JS_SetPropertyStr(ctx, obj, "KEY_Q", JS_NewInt32(ctx, KEY_Q));
  JS_SetPropertyStr(ctx, obj, "KEY_R", JS_NewInt32(ctx, KEY_R));
  JS_SetPropertyStr(ctx, obj, "KEY_S", JS_NewInt32(ctx, KEY_S));
  JS_SetPropertyStr(ctx, obj, "KEY_T", JS_NewInt32(ctx, KEY_T));
  JS_SetPropertyStr(ctx, obj, "KEY_U", JS_NewInt32(ctx, KEY_U));
  JS_SetPropertyStr(ctx, obj, "KEY_V", JS_NewInt32(ctx, KEY_V));
  JS_SetPropertyStr(ctx, obj, "KEY_W", JS_NewInt32(ctx, KEY_W));
  JS_SetPropertyStr(ctx, obj, "KEY_X", JS_NewInt32(ctx, KEY_X));
  JS_SetPropertyStr(ctx, obj, "KEY_Y", JS_NewInt32(ctx, KEY_Y));
  JS_SetPropertyStr(ctx, obj, "KEY_Z", JS_NewInt32(ctx, KEY_Z));

  // 数字键
  JS_SetPropertyStr(ctx, obj, "KEY_0", JS_NewInt32(ctx, KEY_0));
  JS_SetPropertyStr(ctx, obj, "KEY_1", JS_NewInt32(ctx, KEY_1));
  JS_SetPropertyStr(ctx, obj, "KEY_2", JS_NewInt32(ctx, KEY_2));
  JS_SetPropertyStr(ctx, obj, "KEY_3", JS_NewInt32(ctx, KEY_3));
  JS_SetPropertyStr(ctx, obj, "KEY_4", JS_NewInt32(ctx, KEY_4));
  JS_SetPropertyStr(ctx, obj, "KEY_5", JS_NewInt32(ctx, KEY_5));
  JS_SetPropertyStr(ctx, obj, "KEY_6", JS_NewInt32(ctx, KEY_6));
  JS_SetPropertyStr(ctx, obj, "KEY_7", JS_NewInt32(ctx, KEY_7));
  JS_SetPropertyStr(ctx, obj, "KEY_8", JS_NewInt32(ctx, KEY_8));
  JS_SetPropertyStr(ctx, obj, "KEY_9", JS_NewInt32(ctx, KEY_9));

  // 特殊键
  JS_SetPropertyStr(ctx, obj, "KEY_GRAVE_ACCENT",
                    JS_NewInt32(ctx, KEY_GRAVE_ACCENT));

  // 功能键
  JS_SetPropertyStr(ctx, obj, "KEY_F1", JS_NewInt32(ctx, KEY_F1));
  JS_SetPropertyStr(ctx, obj, "KEY_F2", JS_NewInt32(ctx, KEY_F2));
  JS_SetPropertyStr(ctx, obj, "KEY_F3", JS_NewInt32(ctx, KEY_F3));
  JS_SetPropertyStr(ctx, obj, "KEY_F4", JS_NewInt32(ctx, KEY_F4));
  JS_SetPropertyStr(ctx, obj, "KEY_F5", JS_NewInt32(ctx, KEY_F5));
  JS_SetPropertyStr(ctx, obj, "KEY_F6", JS_NewInt32(ctx, KEY_F6));
  JS_SetPropertyStr(ctx, obj, "KEY_F7", JS_NewInt32(ctx, KEY_F7));
  JS_SetPropertyStr(ctx, obj, "KEY_F8", JS_NewInt32(ctx, KEY_F8));
  JS_SetPropertyStr(ctx, obj, "KEY_F9", JS_NewInt32(ctx, KEY_F9));
  JS_SetPropertyStr(ctx, obj, "KEY_F10", JS_NewInt32(ctx, KEY_F10));
  JS_SetPropertyStr(ctx, obj, "KEY_F11", JS_NewInt32(ctx, KEY_F11));
  JS_SetPropertyStr(ctx, obj, "KEY_F12", JS_NewInt32(ctx, KEY_F12));

  // 修饰键 (用于 microui 输入处理)
  JS_SetPropertyStr(ctx, obj, "KEY_LEFT_SHIFT", JS_NewInt32(ctx, KEY_LEFT_SHIFT));
  JS_SetPropertyStr(ctx, obj, "KEY_RIGHT_SHIFT", JS_NewInt32(ctx, KEY_RIGHT_SHIFT));
  JS_SetPropertyStr(ctx, obj, "KEY_LEFT_CONTROL", JS_NewInt32(ctx, KEY_LEFT_CONTROL));
  JS_SetPropertyStr(ctx, obj, "KEY_RIGHT_CONTROL", JS_NewInt32(ctx, KEY_RIGHT_CONTROL));
  JS_SetPropertyStr(ctx, obj, "KEY_LEFT_ALT", JS_NewInt32(ctx, KEY_LEFT_ALT));
  JS_SetPropertyStr(ctx, obj, "KEY_RIGHT_ALT", JS_NewInt32(ctx, KEY_RIGHT_ALT));

  // 鼠标按钮常量
  JS_SetPropertyStr(ctx, obj, "MOUSE_LEFT", JS_NewInt32(ctx, 0));
  JS_SetPropertyStr(ctx, obj, "MOUSE_MIDDLE", JS_NewInt32(ctx, 1));
  JS_SetPropertyStr(ctx, obj, "MOUSE_RIGHT", JS_NewInt32(ctx, 2));

  // Test Mock API
  JS_SetPropertyStr(ctx, obj, "inject_mouse_pos",
                    JS_NewCFunction(ctx, js_input_inject_mouse_pos, "inject_mouse_pos", 2));
  JS_SetPropertyStr(ctx, obj, "inject_mouse_down",
                    JS_NewCFunction(ctx, js_input_inject_mouse_down, "inject_mouse_down", 1));
  JS_SetPropertyStr(ctx, obj, "inject_mouse_up",
                    JS_NewCFunction(ctx, js_input_inject_mouse_up, "inject_mouse_up", 1));
  JS_SetPropertyStr(ctx, obj, "inject_clear",
                    JS_NewCFunction(ctx, js_input_inject_clear, "inject_clear", 0));

  JS_SetPropertyStr(ctx, global, "input", obj);
  JS_FreeValue(ctx, global);

  LOG_VERBOSE("[input_bindings] module loaded\n");
  return 0;
}
