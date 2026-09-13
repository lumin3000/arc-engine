// audio_bindings.c - 音频文件播放 JS 绑定（单曲音乐最小面）
//
// API:
//   audio.music_play(path, volume?) - 请求播放一个音频文件（覆盖现曲，不叠播）
//   audio.music_stop()              - 请求停止当前音乐（幂等）
//   audio.music_status()            - { state, positionMs, lengthMs,
//                                       errorCode, errorContext, playSerial }
//                                     state: "idle"|"playing"|"ended"|"stopped"|"failed"
//
// 线程契约：三个函数任意 JS worker 可调；内部只操作 sound.c 的命令信箱/
// 状态快照，FMOD 调用全部由主线程在 sound_update 内执行（见 sound.h）。

#include "audio_bindings.h"
#include "../log.h"
#include "bedrock/sound/sound.h"
#include "quickjs.h"

static JSValue js_audio_music_play(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv) {
  (void)this_val;
  if (argc < 1 || !JS_IsString(argv[0]))
    return JS_ThrowTypeError(ctx, "music_play(path, volume?) requires string path");

  double volume = 1.0;
  if (argc >= 2 && !JS_IsUndefined(argv[1])) {
    if (JS_ToFloat64(ctx, &volume, argv[1]))
      return JS_ThrowTypeError(ctx, "music_play volume must be a number");
    if (!(volume >= 0.0 && volume <= 1.0))
      return JS_ThrowRangeError(ctx, "music_play volume must be in [0,1]");
  }

  const char *path = JS_ToCString(ctx, argv[0]);
  if (!path) return JS_EXCEPTION;
  if (!path[0]) {
    JS_FreeCString(ctx, path);
    return JS_ThrowTypeError(ctx, "music_play path must be non-empty");
  }
  // sound_music_request_play 内部复制路径，不跨帧借用 JS 内存
  sound_music_request_play(path, (float)volume);
  JS_FreeCString(ctx, path);
  return JS_UNDEFINED;
}

static JSValue js_audio_music_stop(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv) {
  (void)ctx; (void)this_val; (void)argc; (void)argv;
  sound_music_request_stop();
  return JS_UNDEFINED;
}

static const char *music_state_name(int state) {
  switch (state) {
    case SOUND_MUSIC_IDLE: return "idle";
    case SOUND_MUSIC_PLAYING: return "playing";
    case SOUND_MUSIC_ENDED: return "ended";
    case SOUND_MUSIC_STOPPED: return "stopped";
    case SOUND_MUSIC_FAILED: return "failed";
    default: return "unknown";
  }
}

static JSValue js_audio_music_status(JSContext *ctx, JSValueConst this_val,
                                     int argc, JSValueConst *argv) {
  (void)this_val; (void)argc; (void)argv;
  Sound_Music_Status s = sound_music_get_status();
  JSValue obj = JS_NewObject(ctx);
  JS_SetPropertyStr(ctx, obj, "state", JS_NewString(ctx, music_state_name(s.state)));
  JS_SetPropertyStr(ctx, obj, "positionMs", JS_NewUint32(ctx, s.position_ms));
  JS_SetPropertyStr(ctx, obj, "lengthMs", JS_NewUint32(ctx, s.length_ms));
  JS_SetPropertyStr(ctx, obj, "errorCode", JS_NewInt32(ctx, s.error_code));
  JS_SetPropertyStr(ctx, obj, "errorContext", JS_NewString(ctx, s.error_context));
  JS_SetPropertyStr(ctx, obj, "playSerial", JS_NewUint32(ctx, s.play_serial));
  return obj;
}

int js_init_audio_module(JSContext *ctx) {
  JSValue global = JS_GetGlobalObject(ctx);
  JSValue audio_obj = JS_NewObject(ctx);

  JS_SetPropertyStr(ctx, audio_obj, "music_play",
      JS_NewCFunction(ctx, js_audio_music_play, "music_play", 2));
  JS_SetPropertyStr(ctx, audio_obj, "music_stop",
      JS_NewCFunction(ctx, js_audio_music_stop, "music_stop", 0));
  JS_SetPropertyStr(ctx, audio_obj, "music_status",
      JS_NewCFunction(ctx, js_audio_music_status, "music_status", 0));

  JS_SetPropertyStr(ctx, global, "audio", audio_obj);
  JS_FreeValue(ctx, global);
  return 0;
}
