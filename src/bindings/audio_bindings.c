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
//   audio.voice_play(path, gain, pitch, pan, loop?, fadeInMs?) -> id（0=信箱满）
//   audio.voice_set(id, gain, pitch, pan) / voice_stop(id, fadeOutMs?) -> bool
//   audio.voice_stop_all() / voice_results() -> [{id,kind,code,context}] / voice_stats()
//
// 线程契约：全部函数任意 JS worker 可调；内部只操作 sound.c 的命令信箱/
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

// ---- 声道（voice）：带 id 的样本实例，见 sound.h ----
static int voice_num(JSContext *ctx, JSValueConst v, double *out, const char *what) {
  if (JS_ToFloat64(ctx, out, v)) { JS_ThrowTypeError(ctx, "%s must be a number", what); return -1; }
  return 0;
}

// voice_play(path, gain, pitch, pan, loop?, fadeInMs?) -> id（0 = 信箱满被拒；参数无效同步抛错）
static JSValue js_audio_voice_play(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  (void)this_val;
  if (argc < 4 || !JS_IsString(argv[0]))
    return JS_ThrowTypeError(ctx, "voice_play(path, gain, pitch, pan, loop?, fadeInMs?) requires string path");
  double gain, pitch, pan, fade = 0;
  if (voice_num(ctx, argv[1], &gain, "voice_play gain") || voice_num(ctx, argv[2], &pitch, "voice_play pitch")
      || voice_num(ctx, argv[3], &pan, "voice_play pan")) return JS_EXCEPTION;
  if (!(gain >= 0.0 && gain <= 1.0) || !(pitch > 0.0 && pitch <= 4.0) || !(pan >= -1.0 && pan <= 1.0))
    return JS_ThrowRangeError(ctx, "voice_play gain∈[0,1] pitch∈(0,4] pan∈[-1,1] required");
  bool loop = argc >= 5 && JS_ToBool(ctx, argv[4]);
  if (argc >= 6 && !JS_IsUndefined(argv[5])) {
    if (voice_num(ctx, argv[5], &fade, "voice_play fadeInMs")) return JS_EXCEPTION;
    if (!(fade >= 0.0 && fade <= 60000.0)) return JS_ThrowRangeError(ctx, "voice_play fadeInMs must be in [0,60000]");
  }
  size_t len = 0;
  const char *path = JS_ToCStringLen(ctx, &len, argv[0]);
  if (!path) return JS_EXCEPTION;
  if (len == 0 || strlen(path) != len || len >= SOUND_MUSIC_PATH_MAX) {
    JS_FreeCString(ctx, path);
    return JS_ThrowRangeError(ctx, "voice_play path must be non-empty, NUL-free and < %d chars", SOUND_MUSIC_PATH_MAX);
  }
  unsigned int id = sound_voice_request_play(path, (float)gain, (float)pitch, (float)pan, loop, (unsigned int)fade);
  JS_FreeCString(ctx, path);
  return JS_NewUint32(ctx, id);
}

// voice_set(id, gain, pitch, pan) -> bool（false = 信箱满）
static JSValue js_audio_voice_set(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  (void)this_val;
  uint32_t id; double gain, pitch, pan;
  if (argc < 4 || JS_ToUint32(ctx, &id, argv[0])) return JS_ThrowTypeError(ctx, "voice_set(id, gain, pitch, pan)");
  if (voice_num(ctx, argv[1], &gain, "voice_set gain") || voice_num(ctx, argv[2], &pitch, "voice_set pitch")
      || voice_num(ctx, argv[3], &pan, "voice_set pan")) return JS_EXCEPTION;
  if (id == 0 || !(gain >= 0.0 && gain <= 1.0) || !(pitch > 0.0 && pitch <= 4.0) || !(pan >= -1.0 && pan <= 1.0))
    return JS_ThrowRangeError(ctx, "voice_set id>0 gain∈[0,1] pitch∈(0,4] pan∈[-1,1] required");
  return JS_NewBool(ctx, sound_voice_request_set(id, (float)gain, (float)pitch, (float)pan));
}

// voice_stop(id, fadeOutMs?) -> bool（false = 信箱满）
static JSValue js_audio_voice_stop(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  (void)this_val;
  uint32_t id; double fade = 0;
  if (argc < 1 || JS_ToUint32(ctx, &id, argv[0]) || id == 0) return JS_ThrowTypeError(ctx, "voice_stop(id>0, fadeOutMs?)");
  if (argc >= 2 && !JS_IsUndefined(argv[1])) {
    if (voice_num(ctx, argv[1], &fade, "voice_stop fadeOutMs")) return JS_EXCEPTION;
    if (!(fade >= 0.0 && fade <= 60000.0)) return JS_ThrowRangeError(ctx, "voice_stop fadeOutMs must be in [0,60000]");
  }
  return JS_NewBool(ctx, sound_voice_request_stop(id, (unsigned int)fade));
}

static JSValue js_audio_voice_stop_all(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  (void)ctx; (void)this_val; (void)argc; (void)argv;
  sound_voice_request_stop_all();
  return JS_UNDEFINED;
}

static const char *voice_kind_name(int k) {
  switch (k) {
    case SOUND_VOICE_STARTED: return "started";
    case SOUND_VOICE_ENDED: return "ended";
    case SOUND_VOICE_STOPPED: return "stopped";
    case SOUND_VOICE_DROPPED: return "dropped";
    case SOUND_VOICE_FAILED: return "failed";
    default: return "unknown";
  }
}

// voice_results() -> [{ id, kind, code, context }]（取走全部未取结果）
static JSValue js_audio_voice_results(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  (void)this_val; (void)argc; (void)argv;
  static Sound_Voice_Result buf[SOUND_VOICE_RESULT_CAP];
  int n = sound_voice_take_results(buf, SOUND_VOICE_RESULT_CAP);
  JSValue arr = JS_NewArray(ctx);
  for (int i = 0; i < n; i++) {
    JSValue o = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, o, "id", JS_NewUint32(ctx, buf[i].id));
    JS_SetPropertyStr(ctx, o, "kind", JS_NewString(ctx, voice_kind_name(buf[i].kind)));
    JS_SetPropertyStr(ctx, o, "code", JS_NewInt32(ctx, buf[i].code));
    JS_SetPropertyStr(ctx, o, "context", JS_NewString(ctx, buf[i].context));
    JS_SetPropertyUint32(ctx, arr, (uint32_t)i, o);
  }
  return arr;
}

static JSValue js_audio_voice_stats(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  (void)this_val; (void)argc; (void)argv;
  Sound_Voice_Stats s = sound_voice_get_stats();
  JSValue o = JS_NewObject(ctx);
  JS_SetPropertyStr(ctx, o, "activeVoices", JS_NewInt32(ctx, s.active_voices));
  JS_SetPropertyStr(ctx, o, "pendingCmds", JS_NewInt32(ctx, s.pending_cmds));
  JS_SetPropertyStr(ctx, o, "sampleCount", JS_NewInt32(ctx, s.sample_count));
  JS_SetPropertyStr(ctx, o, "sampleBytes", JS_NewUint32(ctx, s.sample_bytes));
  JS_SetPropertyStr(ctx, o, "resultsPending", JS_NewUint32(ctx, s.results_pending));
  JS_SetPropertyStr(ctx, o, "resultsOverflow", JS_NewUint32(ctx, s.results_overflow));
  JS_SetPropertyStr(ctx, o, "startedTotal", JS_NewUint32(ctx, s.started_total));
  JS_SetPropertyStr(ctx, o, "endedTotal", JS_NewUint32(ctx, s.ended_total));
  JS_SetPropertyStr(ctx, o, "stoppedTotal", JS_NewUint32(ctx, s.stopped_total));
  JS_SetPropertyStr(ctx, o, "droppedTotal", JS_NewUint32(ctx, s.dropped_total));
  JS_SetPropertyStr(ctx, o, "failedTotal", JS_NewUint32(ctx, s.failed_total));
  return o;
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

  JS_SetPropertyStr(ctx, audio_obj, "voice_play",
      JS_NewCFunction(ctx, js_audio_voice_play, "voice_play", 6));
  JS_SetPropertyStr(ctx, audio_obj, "voice_set",
      JS_NewCFunction(ctx, js_audio_voice_set, "voice_set", 4));
  JS_SetPropertyStr(ctx, audio_obj, "voice_stop",
      JS_NewCFunction(ctx, js_audio_voice_stop, "voice_stop", 2));
  JS_SetPropertyStr(ctx, audio_obj, "voice_stop_all",
      JS_NewCFunction(ctx, js_audio_voice_stop_all, "voice_stop_all", 0));
  JS_SetPropertyStr(ctx, audio_obj, "voice_results",
      JS_NewCFunction(ctx, js_audio_voice_results, "voice_results", 0));
  JS_SetPropertyStr(ctx, audio_obj, "voice_stats",
      JS_NewCFunction(ctx, js_audio_voice_stats, "voice_stats", 0));

  JS_SetPropertyStr(ctx, global, "audio", audio_obj);
  JS_FreeValue(ctx, global);
  return 0;
}
