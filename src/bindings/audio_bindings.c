// audio_bindings.c - 音频文件播放 JS 绑定（单曲音乐最小面）
//
// API:
//   audio.music_play(path, volume?) - 请求播放一个音频文件（覆盖现曲，不叠播）
//   audio.music_stop()              - 请求停止当前音乐（幂等）
//   audio.music_status()            - { state, positionMs, lengthMs,
//                                       errorCode, errorContext, playSerial }
//                                     state: "idle"|"playing"|"ended"|"stopped"|"failed"
//   audio.cue_play(path, volume?)   - 请求播放一次 2D 短声效；true=已入队，
//                                     false=队列满被拒（参数无效同步抛错）
//   audio.cue_status()              - { activeVoices, acceptedTotal, startedTotal,
//                                       endedTotal, rejectedQueueFullTotal,
//                                       droppedVoiceLimitTotal, failedTotal,
//                                       lastErrorCode, lastErrorContext }
//
// 线程契约：三个函数任意 JS worker 可调；内部只操作 sound.c 的命令信箱/
// 状态快照，后端调用全部由主线程在 sound_update 内执行（见 sound.h）。

#include "audio_bindings.h"
#include "../log.h"
#include "bedrock/sound/sound.h"
#include "quickjs.h"
#include <string.h>

static JSValue js_audio_music_play(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv) {
  (void)this_val;
  if (argc < 1 || !JS_IsString(argv[0]))
    return JS_ThrowTypeError(ctx, "music_play(path, volume?) requires string path");

  double volume = 1.0;
  if (argc >= 2 && !JS_IsUndefined(argv[1])) {
    if (JS_ToFloat64(ctx, &volume, argv[1]))
      return JS_ThrowTypeError(ctx, "music_play volume must be a number");
    // NaN/±Inf 与越界一并在此同步拒绝（NaN 使比较为假）
    if (!(volume >= 0.0 && volume <= 1.0))
      return JS_ThrowRangeError(ctx, "music_play volume must be a finite number in [0,1]");
  }

  size_t path_len = 0;
  const char *path = JS_ToCStringLen(ctx, &path_len, argv[0]);
  if (!path) return JS_EXCEPTION;
  if (path_len == 0) {
    JS_FreeCString(ctx, path);
    return JS_ThrowTypeError(ctx, "music_play path must be non-empty");
  }
  if (strlen(path) != path_len) {
    JS_FreeCString(ctx, path);
    return JS_ThrowTypeError(ctx, "music_play path must not contain NUL");
  }
  if (path_len >= SOUND_MUSIC_PATH_MAX) {
    JS_FreeCString(ctx, path);
    return JS_ThrowRangeError(ctx, "music_play path too long (max %d)",
                              SOUND_MUSIC_PATH_MAX - 1);
  }
  // sound_music_request_play 内部复制路径，不跨帧借用 JS 内存；
  // false = C 侧拒绝（防绑定与 C 校验漂移的双保险），同步抛出不改播放状态
  bool accepted = sound_music_request_play(path, (float)volume);
  JS_FreeCString(ctx, path);
  if (!accepted)
    return JS_ThrowInternalError(ctx, "music_play rejected by native validation");
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

static JSValue js_audio_cue_play(JSContext *ctx, JSValueConst this_val,
                                 int argc, JSValueConst *argv) {
  (void)this_val;
  if (argc < 1 || !JS_IsString(argv[0]))
    return JS_ThrowTypeError(ctx, "cue_play(path, volume?) requires string path");

  double volume = 1.0;
  if (argc >= 2 && !JS_IsUndefined(argv[1])) {
    if (JS_ToFloat64(ctx, &volume, argv[1]))
      return JS_ThrowTypeError(ctx, "cue_play volume must be a number");
    // NaN/±Inf 与越界一并在此同步拒绝（NaN 使比较为假）
    if (!(volume >= 0.0 && volume <= 1.0))
      return JS_ThrowRangeError(ctx, "cue_play volume must be a finite number in [0,1]");
  }

  size_t path_len = 0;
  const char *path = JS_ToCStringLen(ctx, &path_len, argv[0]);
  if (!path) return JS_EXCEPTION;
  if (path_len == 0) {
    JS_FreeCString(ctx, path);
    return JS_ThrowTypeError(ctx, "cue_play path must be non-empty");
  }
  if (strlen(path) != path_len) {
    JS_FreeCString(ctx, path);
    return JS_ThrowTypeError(ctx, "cue_play path must not contain NUL");
  }
  if (path_len >= SOUND_MUSIC_PATH_MAX) {
    JS_FreeCString(ctx, path);
    return JS_ThrowRangeError(ctx, "cue_play path too long (max %d)",
                              SOUND_MUSIC_PATH_MAX - 1);
  }
  // sound_cue_request_play 内部复制路径，不跨帧借用 JS 内存。
  // INVALID = 绑定与 C 校验漂移的双保险，同步抛出；QUEUE_FULL 是运行期
  // 拥塞，返回 false 由调用方决定（不是异常），已播实例与音乐都不受影响。
  Sound_Cue_Request_Result r = sound_cue_request_play(path, (float)volume);
  JS_FreeCString(ctx, path);
  if (r == SOUND_CUE_REJECT_INVALID)
    return JS_ThrowInternalError(ctx, "cue_play rejected by native validation");
  return JS_NewBool(ctx, r == SOUND_CUE_ACCEPTED);
}

static JSValue js_audio_cue_status(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv) {
  (void)this_val; (void)argc; (void)argv;
  Sound_Cue_Status s = sound_cue_get_status();
  JSValue obj = JS_NewObject(ctx);
  JS_SetPropertyStr(ctx, obj, "activeVoices", JS_NewInt32(ctx, s.active_voices));
  JS_SetPropertyStr(ctx, obj, "acceptedTotal", JS_NewUint32(ctx, s.accepted_total));
  JS_SetPropertyStr(ctx, obj, "startedTotal", JS_NewUint32(ctx, s.started_total));
  JS_SetPropertyStr(ctx, obj, "endedTotal", JS_NewUint32(ctx, s.ended_total));
  JS_SetPropertyStr(ctx, obj, "rejectedQueueFullTotal",
                    JS_NewUint32(ctx, s.rejected_queue_full_total));
  JS_SetPropertyStr(ctx, obj, "droppedVoiceLimitTotal",
                    JS_NewUint32(ctx, s.dropped_voice_limit_total));
  JS_SetPropertyStr(ctx, obj, "failedTotal", JS_NewUint32(ctx, s.failed_total));
  JS_SetPropertyStr(ctx, obj, "lastErrorCode", JS_NewInt32(ctx, s.last_error_code));
  JS_SetPropertyStr(ctx, obj, "lastErrorContext", JS_NewString(ctx, s.last_error_context));
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
  JS_SetPropertyStr(ctx, audio_obj, "cue_play",
      JS_NewCFunction(ctx, js_audio_cue_play, "cue_play", 2));
  JS_SetPropertyStr(ctx, audio_obj, "cue_status",
      JS_NewCFunction(ctx, js_audio_cue_status, "cue_status", 0));

  JS_SetPropertyStr(ctx, global, "audio", audio_obj);
  JS_FreeValue(ctx, global);
  return 0;
}
