
#include "platform.h"

#include "engine_main.h"
#include "engine_state.h"
#include "gfx/camera.h"
#include "gfx/render.h"
#include "helpers.h"
#include "input/input.h"
#include "js_runtime.h"
#include "js_runtime_internal.h"
#include "sound/sound.h"
#include "stdin_reader.h"
#include "utils/utils.h"
#include "../config.h"
#include "../types.h"

#include "../../external/sokol/c/sokol_app.h"
#include "../../external/sokol/c/sokol_gfx.h"
#include "../../external/sokol/c/sokol_glue.h"
#include "../../external/sokol/c/sokol_log.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#endif

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#if !defined(_WIN32)
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#endif

// ---------------------------------------------------------------------------
// Globals exposed to engine bindings via engine_state.h. engine_main.c is
// the sole owner; bindings and other TUs read through the accessors
// declared there.

static atomic_bool camera_controls_blocked = false;
static atomic_bool camera_wheel_blocked = false;

void engine_set_camera_controls_blocked(bool blocked) {
  atomic_store(&camera_controls_blocked, blocked);
}

void engine_set_camera_wheel_blocked(bool blocked) {
  atomic_store(&camera_wheel_blocked, blocked);
}

// ---------------------------------------------------------------------------
// Frame-rate cap (RW-style pacing). setting = Engine_Config.frame_cap_fps +
// runtime engine_set_frame_cap_fps; env = ARC_FRAME_CAP (-1 unset, wins over
// setting when >= 0); effective is what the frame pacer uses. 0 = uncapped.

static int s_frame_cap_setting = 0;
static int s_frame_cap_env = -1;

static int frame_cap_effective(void) {
  return s_frame_cap_env >= 0 ? s_frame_cap_env : s_frame_cap_setting;
}

void engine_set_frame_cap_fps(int fps) {
  s_frame_cap_setting = fps;
}

void engine_get_frame_cap(int *setting, int *effective, int *env_override) {
  if (setting) *setting = s_frame_cap_setting;
  if (effective) *effective = frame_cap_effective();
  if (env_override) *env_override = s_frame_cap_env;
}

static double monotonic_seconds(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

// JS 帧签发限速 (帧率上限真正作用点): js_runtime_render 每个引擎帧至多向
// render service 签发一个 "frame" 请求; 在此按 1/cap 间隔放行, 把 JS 渲染帧率
// 钉在 cap。引擎帧回调本身不降速 — 输入/相机/音频照常跑满, cap<=0 恒放行
// (零开销零行为变化)。__EMSCRIPTEN__ 同样生效: rAF 只管唤醒, 签发仍受限。
static double s_next_frame_issue = 0.0;
static double s_frame_beat_time = 0.0;
static double s_frame_beat_interval = 0.0;
static int s_last_frame_cap = -1;

bool engine_frame_rate_allow(void) {
  int cap = frame_cap_effective();
  if (cap != s_last_frame_cap) {
    s_next_frame_issue = 0.0;
    s_last_frame_cap = cap;
  }
  if (cap <= 0) return true;
  // Work performed by this beat must not move the pacing decision: varying
  // render duration otherwise turns a 60Hz cap into skipped display beats.
  double now = s_frame_beat_time;
  double interval = 1.0 / (double)cap;
  // Release on the nearest display beat, rather than falling a whole beat
  // behind when a callback arrives slightly early. Bound the allowance by
  // half the cap interval as well; missed deadlines never accumulate a burst.
  double tolerance = 0.5 * fmin(s_frame_beat_interval, interval);
  if (now + tolerance < s_next_frame_issue)
    return false;
  if (s_next_frame_issue == 0.0 || now - s_next_frame_issue > interval)
    s_next_frame_issue = now + interval;
  else
    s_next_frame_issue += interval;
  return true;
}

int window_w = DEFAULT_WINDOW_WIDTH;
int window_h = DEFAULT_WINDOW_HEIGHT;
int g_log_level = LOG_LEVEL_NORMAL;
const char *g_run_mode = "default";
float g_master_volume = 0.25f;

Core_Context ctx = {0};
static Game_State _game_state = {0};

// ---------------------------------------------------------------------------
// Arc_Engine_State singleton — see engine_state.h. Collects what were
// previously scattered file-scope statics (Engine_Config copy, saved
// argv, last_frame_time, JS runtime overrides) so their lifecycle is
// managed in one place.

static Arc_Engine_State g_engine_state = {0};
static Engine_Config    g_cfg = {0};

Arc_Engine_State *arc_engine_state(void) { return &g_engine_state; }

char **app_get_argv(void) { return g_engine_state.saved_argv; }
int    app_get_argc(void) { return g_engine_state.saved_argc; }

// ---------------------------------------------------------------------------
// sapp lifecycle.

#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
void engine_macos_window_to_main_display(int w, int h);  // screenshot.m
#endif

static void engine_on_init(void) {
#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
  // 测试确定性：窗口挪回主显示器（见 screenshot.m 注释），须早于下方 sapp_width() 采样
  if (getenv("ARC_WINDOW_MAIN_DISPLAY")) engine_macos_window_to_main_display(window_w, window_h);
#endif
  input_state = &_actual_input_state;

  sound_init();
  stdin_reader_init();

  _game_state.zoom_level = g_cfg.zoom_level > 0 ? g_cfg.zoom_level : 2.0f;
  _game_state.desired_zoom_level = _game_state.zoom_level;
  _game_state.zoom_min = MIN_ZOOM_LEVEL;
  _game_state.zoom_max = MAX_ZOOM_LEVEL;
  _game_state.zoom_to_cursor = 0;

  // 帧率上限: Engine_Config.frame_cap_fps 为配置值; ARC_FRAME_CAP 环境变量
  // 存在即覆盖 (非负整数, 非法值 fatal — 配置错误不容忍); 0=不限。
  s_frame_cap_setting = g_cfg.frame_cap_fps > 0 ? g_cfg.frame_cap_fps : 0;
  s_frame_cap_env = -1;
  const char *fc_env = getenv("ARC_FRAME_CAP");
  if (fc_env != NULL) {
    char *end = NULL;
    long v = strtol(fc_env, &end, 10);
    if (end == fc_env || *end != '\0' || v < 0 || v > 1000000) {
      fprintf(stderr,
              "[engine] FATAL: ARC_FRAME_CAP must be a nonnegative integer "
              "(got '%s')\n",
              fc_env);
      exit(1);
    }
    s_frame_cap_env = (int)v;
  }
  {
    int eff = frame_cap_effective();
    LOG_INFO("[engine] frame_cap: effective=%d source=%s\n", eff,
             s_frame_cap_env >= 0 ? "env ARC_FRAME_CAP"
                 : s_frame_cap_setting > 0 ? "config" : "none");
  }

  _game_state.cam_pos[0] = g_cfg.camera_initial_pos[0];
  _game_state.cam_pos[1] = g_cfg.camera_initial_pos[1];
  _game_state.cam_pos[2] = g_cfg.camera_initial_pos[2];

  _game_state.cam_rot[0] = g_cfg.camera_initial_rot[0];
  _game_state.cam_rot[1] = g_cfg.camera_initial_rot[1];
  _game_state.cam_rot[2] = g_cfg.camera_initial_rot[2];

  ctx.gs = &_game_state;

  // Game-side init runs BEFORE render_init so the game can register
  // sprites; render_init calls load_sprites_into_atlas which reads
  // sprite_data.
  if (g_cfg.on_init) g_cfg.on_init();

  render_init();

  if (g_cfg.on_render_ready) g_cfg.on_render_ready();

  camera_set_ortho_size(camera_get_main(),
                        g_cfg.base_ortho_size > 0.0f ? g_cfg.base_ortho_size
                                                     : 10.0f);

  float initial_aspect = (float)window_w / (float)window_h;
  camera_set_aspect(camera_get_main(), initial_aspect);

  if (g_cfg.jtask_bootstrap_path) {
    js_runtime_set_bootstrap_path(g_cfg.jtask_bootstrap_path);
  }
  if (g_cfg.js_main_script_path) {
    js_runtime_set_main_script_path(g_cfg.js_main_script_path);
  }
  // Forward binding registrars to Arc_Engine_State. Must happen BEFORE
  // js_runtime_init() because js_init_message_module consults the
  // singleton as soon as it's called for the bootstrap ctx, and jtask
  // also consults it for every subsequent worker ctx.
  arc_engine_state()->js_main_bindings_registrar = g_cfg.register_main_js_bindings;
  arc_engine_state()->js_render_bindings_registrar = g_cfg.register_render_js_bindings;
  arc_engine_state()->post_mesh_render_hook = g_cfg.on_post_mesh_render;
  arc_engine_state()->pre_pass_render_hook = g_cfg.on_pre_pass_render;
  arc_engine_state()->post_main_render_hook = g_cfg.on_post_main_render;
  arc_engine_state()->pass_begin_render_hook = g_cfg.on_pass_begin_render;
  if (g_cfg.on_register_js_bindings) {
    js_runtime_set_game_bindings_registrar(g_cfg.on_register_js_bindings);
  }

  LOG_INFO("[engine] init js_runtime\n");
  if (js_runtime_init() < 0) {
    LOG_ERROR("[engine] js_runtime_init failed\n");
  }

  LOG_INFO("[engine] init done\n");
}

static int g_exit_code;
#ifdef __EMSCRIPTEN__
static bool g_browser_quit_pending;
static void browser_finish_shutdown(void *unused) {
  (void)unused;
  // Run after Sokol has unregistered handlers and discarded its app state.
  EMSCRIPTEN_WEBGL_CONTEXT_HANDLE gl = emscripten_webgl_get_current_context();
  if (gl) emscripten_webgl_destroy_context(gl);
  LOG_INFO("[Lifecycle] host cleanup complete; exiting browser runtime\n");
  emscripten_force_exit(g_exit_code);
}
#endif
static void engine_host_event(const sapp_event *event) {
#ifdef __EMSCRIPTEN__
  if (event->type == SAPP_EVENTTYPE_QUIT_REQUESTED) {
    // Keep the Sokol/GPU host alive until all worker runtimes have relinquished it.
    sapp_cancel_quit();
    g_browser_quit_pending = true;
    LOG_INFO("[Lifecycle] quit requested; draining runtime\n");
    return;
  }
#endif
  event_callback(event);
}

static void engine_on_frame(void) {
  const double beat_time = monotonic_seconds();
  s_frame_beat_interval = s_frame_beat_time > 0.0 ? beat_time - s_frame_beat_time : 0.0;
  s_frame_beat_time = beat_time;
#ifdef __EMSCRIPTEN__
  if (g_browser_quit_pending) {
    js_runtime_shutdown();
    if (!js_runtime_get_context()) sapp_quit();
    return;
  }
#endif
  // Layout, screen-space projection and input share logical window units.
  // The swapchain retains its full physical size for crisp high-DPI rendering.
  window_w = (int)lroundf(sapp_width() / sapp_dpi_scale());
  window_h = (int)lroundf(sapp_height() / sapp_dpi_scale());

  float base_ortho = g_cfg.base_ortho_size > 0.0f ? g_cfg.base_ortho_size
                                                  : 10.0f;
  camera_set_ortho_size(camera_get_main(), base_ortho / ctx.gs->zoom_level);

  float aspect = (float)window_w / (float)window_h;
  camera_set_aspect(camera_get_main(), aspect);

  double now = seconds_since_init();
  double dt = now - g_engine_state.last_frame_time;
  g_engine_state.last_frame_time = now;
  if (dt > MIN_FRAME_TIME) dt = MIN_FRAME_TIME;

  ctx.delta_t = (float)dt;
  ctx.gs->ticks++;
  ctx.gs->game_time_elapsed = now;

  input_state = &_actual_input_state;
  input_apply_queued_scroll(input_state);   // 注入滚轮与 OS 滚轮同时序 (早于下方相机缩放读取)
  const float camera_scroll = input_state->camera_scroll_y;
  input_state->camera_scroll_y = 0.0f;
  if (key_pressed(KEY_ENTER) && key_down(KEY_LEFT_ALT)) {
    sapp_toggle_fullscreen();
  }

  if (!g_cfg.disable_default_camera_controls && !atomic_load(&camera_controls_blocked)) {
    // World Units (1 unit = 1 cell): 3.0 cells/sec is the engine
    // fallback. Games with large cell maps override via
    // Engine_Config.camera_pan_speed.
    float speed =
        g_cfg.camera_pan_speed > 0.0f ? g_cfg.camera_pan_speed : 3.0f;

    float dx = 0.0f, dz = 0.0f;
    if (key_down(KEY_W)) dz += 1.0f;
    if (key_down(KEY_S)) dz -= 1.0f;
    if (key_down(KEY_A)) dx -= 1.0f;
    if (key_down(KEY_D)) dx += 1.0f;

    if (dx != 0.0f && dz != 0.0f) {
      float inv_sqrt2 = 0.7071067811865476f;
      dx *= inv_sqrt2;
      dz *= inv_sqrt2;
    }
    {
      // pan along the screen axes: turn by the camera yaw (no-op at yaw 0)
      const float yaw = ctx.gs->cam_rot[1] * 3.14159265358979f / 180.0f;
      const float c = cosf(yaw), s = sinf(yaw);
      const float rx = c * dx + s * dz, rz = -s * dx + c * dz;
      dx = rx;
      dz = rz;
    }

    ctx.gs->cam_pos[0] += dx * speed * ctx.delta_t;
    ctx.gs->cam_pos[2] += dz * speed * ctx.delta_t;
  }

  camera_set_position(camera_get_main(), ctx.gs->cam_pos[0], ctx.gs->cam_pos[1],
                      ctx.gs->cam_pos[2]);
  camera_set_rotation(camera_get_main(), ctx.gs->cam_rot[0], ctx.gs->cam_rot[1],
                      ctx.gs->cam_rot[2]);

  if (!g_cfg.disable_default_camera_controls && !atomic_load(&camera_controls_blocked)) {
    // 键缩放在 -/= : Q/E 让位给消费者的游戏层快捷键 (滚轮缩放不变)。
    // 按时间而非按帧: 60fps 下与旧每帧倍率一致, 其他帧率下按住时长决定缩放量。
    const float zoom_before = ctx.gs->zoom_level;
    const float desired_before = ctx.gs->desired_zoom_level;
    float key_dt = 60.0f * ctx.delta_t;
    if (key_down(KEY_MINUS)) ctx.gs->desired_zoom_level *= powf(ZOOM_IN_MULTIPLIER, key_dt);
    if (key_down(KEY_EQUAL)) ctx.gs->desired_zoom_level *= powf(ZOOM_OUT_MULTIPLIER, key_dt);

    float scroll = camera_scroll;
    if (scroll != 0.0f && !atomic_load(&camera_wheel_blocked)) {
      float zoom_delta = scroll * SCROLL_ZOOM_RATE * ZOOM_SPEED / 35.0f;
      ctx.gs->desired_zoom_level *= (1.0f + zoom_delta);
    }

    if (ctx.gs->desired_zoom_level < ctx.gs->zoom_min)
      ctx.gs->desired_zoom_level = ctx.gs->zoom_min;
    if (ctx.gs->desired_zoom_level > ctx.gs->zoom_max)
      ctx.gs->desired_zoom_level = ctx.gs->zoom_max;

    // 平滑走 60Hz 固定步 (RW Verse/Gen.cs FixedTimeStepUpdate: buffer+=min(dt,1),
    // n=floor(buffer/step), buffer-=n*step; CameraDriver 缩放平滑同法), 帧率无关。
    // dt 已被 MIN_FRAME_TIME 封顶, 单帧最多补 3 步; 屏蔽期不累加不步进。
    static float zoom_step_buffer = 0.0f;
    const float zoom_step = 1.0f / 60.0f;
    zoom_step_buffer += ctx.delta_t;
    int zoom_steps = (int)floorf(zoom_step_buffer / zoom_step);
    zoom_step_buffer -= zoom_steps * zoom_step;
    if (ctx.gs->zoom_pixel_step > 0.0f) {
      // Stepped zoom: the wheel and keys still move desired_zoom_level continuously; the
      // camera takes the nearest whole level at once, so no frame is drawn between levels.
      // A level is held for ZOOM_STEP_HOLD so a continuous scroll reads as separate steps.
      static bool zoom_step_pending = false;
      static float zoom_step_held = ZOOM_STEP_HOLD;
      zoom_step_held += ctx.delta_t;
      if (ctx.gs->desired_zoom_level != desired_before) zoom_step_pending = true;
      else if (ctx.gs->desired_zoom_level == ctx.gs->zoom_level) zoom_step_pending = false;  // set by script
      if (zoom_step_pending && zoom_step_held >= ZOOM_STEP_HOLD) {
        const float level = ctx.gs->zoom_pixel_step * 2.0f * base_ortho / (float)sapp_height();
        float snapped = roundf(ctx.gs->desired_zoom_level / level) * level;
        if (snapped < ctx.gs->zoom_min) snapped = ceilf(ctx.gs->zoom_min / level) * level;
        if (snapped > ctx.gs->zoom_max) snapped = floorf(ctx.gs->zoom_max / level) * level;
        if (snapped < ctx.gs->zoom_min) snapped = ctx.gs->desired_zoom_level;  // range narrower than a level
        if (snapped != ctx.gs->zoom_level) zoom_step_held = 0.0f;
        ctx.gs->zoom_level = snapped;
        zoom_step_pending = false;
      }
    } else
    for (int i = 0; i < zoom_steps; i++) {
      ctx.gs->zoom_level +=
          (ctx.gs->desired_zoom_level - ctx.gs->zoom_level) * ZOOM_TIGHTNESS;
    }

    if (ctx.gs->zoom_level < ctx.gs->zoom_min)
      ctx.gs->zoom_level = ctx.gs->zoom_min;
    if (ctx.gs->zoom_level > ctx.gs->zoom_max)
      ctx.gs->zoom_level = ctx.gs->zoom_max;

    // 朝鼠标缩放 (RW Verse/CameraDriver.cs ZoomToMouse: 改前取鼠标世界点
    // P0、改后取 P1, cam_pos += P0-P1)。仅本帧 zoom_level 实际变化且光标
    // 不在被屏蔽 UI 区 (wheel_blocked: 侧栏/界面) 时锚定; 相机 controls
    // 被屏蔽时整块不执行。旧 ortho 求 P0、新 ortho 求 P1 (camera_set_ortho_
    // size 置 matrices_dirty, 下次取 VP 即重算, camera.c:128-133/175-195);
    // 用完恢复旧 ortho — 下一帧开头按新 zoom 重设, 本帧渲染保持旧取景一致。
    // cam_pos 经 ctx.gs 下一帧 camera_set_position 生效 (引擎移动在 JS
    // 相机界内夹取之前, 游戏侧夹取照常)。
    if (ctx.gs->zoom_to_cursor && ctx.gs->zoom_level != zoom_before
        && !atomic_load(&camera_wheel_blocked)) {
      Camera *cam = camera_get_main();
      float p0x = 0.0f, p0z = 0.0f, p1x = 0.0f, p1z = 0.0f;
      camera_screen_to_world_ground(cam, input_state->mouse_x,
                                    input_state->mouse_y, (float)window_w,
                                    (float)window_h, &p0x, &p0z);
      camera_set_ortho_size(cam, base_ortho / ctx.gs->zoom_level);
      camera_screen_to_world_ground(cam, input_state->mouse_x,
                                    input_state->mouse_y, (float)window_w,
                                    (float)window_h, &p1x, &p1z);
      ctx.gs->cam_pos[0] += p0x - p1x;
      ctx.gs->cam_pos[2] += p0z - p1z;
      camera_set_ortho_size(cam, base_ortho / zoom_before);
    }
  }

  char stdin_buf[1024];
  while (stdin_reader_poll(stdin_buf, sizeof(stdin_buf)) == 0) {
    js_runtime_handle_external_command(stdin_buf);
  }

  Vec2 listener = {ctx.gs->cam_pos[0], ctx.gs->cam_pos[2]};
  sound_update(listener, g_master_volume);

  if (g_cfg.on_frame) g_cfg.on_frame(ctx.delta_t);

  if (js_runtime_render()) reset_input_state(input_state);
  if (js_runtime_has_failed()) {
    g_exit_code = 1;
    sapp_set_exit_code(g_exit_code);
    sapp_request_quit();
  }
}

static void engine_on_cleanup(void) {
  js_runtime_shutdown();
  if (g_cfg.on_cleanup) g_cfg.on_cleanup();
  stdin_reader_shutdown();
  sound_shutdown();
  sg_shutdown();
#ifdef __EMSCRIPTEN__
  emscripten_async_call(browser_finish_shutdown, NULL, 0);
#endif
}

// ---------------------------------------------------------------------------
// Argument parsing.

static void parse_args(int argc, char *argv[]) {
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
      g_log_level = LOG_LEVEL_VERBOSE;
    } else if (strcmp(argv[i], "-q") == 0 || strcmp(argv[i], "--quiet") == 0) {
      g_log_level = LOG_LEVEL_QUIET;
    } else if (strcmp(argv[i], "--mode") == 0 && i + 1 < argc) {
      g_run_mode = argv[++i];
    } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
      printf("Usage: %s [options]\n", argv[0]);
      printf("Options:\n");
      printf("  --mode MODE    Set run mode\n");
      printf("  -v, --verbose  Enable verbose logging\n");
      printf("  -q, --quiet    Quiet mode (errors only)\n");
      printf("  -h, --help     Show this help\n");
      exit(0);
    }
  }
}

// ---------------------------------------------------------------------------
// Process singleton lock. Opt-in via Engine_Config.app_name. Enforces "no
// two arc-engine processes with the same app_name running simultaneously".
// Motivated by an automation incident where an AI loop launched 14
// concurrent blueprinter instances on the user's machine because nothing
// prevented it. The fd is held until process exit; kernel auto-releases
// flock on termination (including SIGKILL / crash) — this is not a leak.

#if !defined(_WIN32)
static int s_singleton_lock_fd = -1;

static bool acquire_singleton_lock(const Engine_Config *cfg, int argc,
                                   char **argv) {
  if (!cfg->app_name) return true;  // opt-in

  char path[256];
  snprintf(path, sizeof(path), "/tmp/%s.lock", cfg->app_name);

  int fd = open(path, O_RDWR | O_CREAT, 0600);
  if (fd < 0) {
    fprintf(stderr,
            "[singleton_lock] warn: open(%s) failed: %s — fail-open, "
            "allowing launch\n",
            path, strerror(errno));
    return true;  // fail-open
  }

  if (flock(fd, LOCK_EX | LOCK_NB) < 0) {
    // Someone else holds it. Read whatever they wrote so we can tell
    // the user who's already running.
    char buf[1024];
    ssize_t n = pread(fd, buf, sizeof(buf) - 1, 0);
    if (n < 0) n = 0;
    buf[n] = '\0';
    fprintf(stderr,
            "[singleton_lock] Another instance is already running:\n"
            "%s\n"
            "If stale: rm %s\n",
            buf, path);
    close(fd);
    return false;
  }

  // We own the lock. Overwrite the file with our diagnostic info.
  ftruncate(fd, 0);
  lseek(fd, 0, SEEK_SET);

  char cwd[512];
  if (!getcwd(cwd, sizeof(cwd))) {
    snprintf(cwd, sizeof(cwd), "<unknown>");
  }

  char argv_buf[1024];
  size_t off = 0;
  argv_buf[0] = '\0';
  for (int i = 0; i < argc && off < sizeof(argv_buf) - 1; i++) {
    int written = snprintf(argv_buf + off, sizeof(argv_buf) - off,
                           "%s%s", i == 0 ? "" : " ", argv[i]);
    if (written < 0) break;
    off += (size_t)written;
  }

  char info[2048];
  int len = snprintf(info, sizeof(info),
                     "pid=%d\n"
                     "started=%ld\n"
                     "cwd=%s\n"
                     "argv=%s\n",
                     (int)getpid(), (long)time(NULL), cwd, argv_buf);
  if (len > 0) {
    ssize_t w = write(fd, info, (size_t)len);
    (void)w;
    fsync(fd);
  }

  // Hold fd until process exit. kernel releases flock on terminate.
  s_singleton_lock_fd = fd;
  return true;
}
#else
static bool acquire_singleton_lock(const Engine_Config *cfg, int argc,
                                   char **argv) {
  (void)cfg; (void)argc; (void)argv;
  // Windows path lands with §S3R.5 (CreateMutex). For now opt-in is a no-op.
  return true;
}
#endif

// ---------------------------------------------------------------------------
// Public entry — object-style API. The engine is a process-level singleton
// (sapp_run is global), so the handle returned by arc_engine_create() is
// effectively a pointer to internal state. Calling create twice returns
// the same handle.

struct Arc_Engine {
  int initialized;  // 1 once arc_engine_create() has populated g_cfg
};

static struct Arc_Engine g_engine = {0};

Arc_Engine *arc_engine_create(const Engine_Config *cfg, int argc, char **argv) {
  if (!cfg || !cfg->window_title) {
    fprintf(stderr, "[arc_engine_create] cfg->window_title is required\n");
    return NULL;
  }

  if (!acquire_singleton_lock(cfg, argc, argv)) return NULL;

  g_cfg = *cfg;
  g_engine_state.saved_argc = argc;
  g_engine_state.saved_argv = argv;

  if (g_cfg.window_w > 0) window_w = g_cfg.window_w;
  if (g_cfg.window_h > 0) window_h = g_cfg.window_h;
  // ARC_WINDOW_SIZE=WxH: 启动时覆盖消费者给的窗口尺寸 (多分辨率验收/截图用)。
  // 格式非法即拒绝启动 (fail-fast), 不静默回落默认尺寸。
  const char *size_env = getenv("ARC_WINDOW_SIZE");
  if (size_env && size_env[0]) {
    int ew = 0, eh = 0;
    char tail = 0;
    if (sscanf(size_env, "%dx%d%c", &ew, &eh, &tail) != 2 || ew < 320 || eh < 240
        || ew > 8192 || eh > 8192) {
      fprintf(stderr, "[arc_engine_create] ARC_WINDOW_SIZE must be WxH (320..8192), got '%s'\n",
              size_env);
      return NULL;
    }
    window_w = ew;
    window_h = eh;
  }
  if (g_cfg.default_run_mode) g_run_mode = g_cfg.default_run_mode;
  if (g_cfg.master_volume > 0.0f) g_master_volume = g_cfg.master_volume;

  parse_args(argc, argv);

  g_engine.initialized = 1;
  return &g_engine;
}

int arc_engine_run(Arc_Engine *eng) {
  g_exit_code = 0;
  if (!eng || !eng->initialized) {
    fprintf(stderr, "[arc_engine_run] engine not created\n");
    return 1;
  }

  // Existing screenshot tests request fixed physical dimensions on the main
  // display. Explicit overrides also allow testing the high-DPI path there.
  bool high_dpi = g_cfg.high_dpi && !getenv("ARC_WINDOW_MAIN_DISPLAY");
  const char *dpi_env = getenv("ARC_HIGH_DPI");
  if (dpi_env) {
    if (strcmp(dpi_env, "0") != 0 && strcmp(dpi_env, "1") != 0) {
      fprintf(stderr, "[arc_engine_run] ARC_HIGH_DPI must be 0 or 1\n");
      return 1;
    }
    high_dpi = strcmp(dpi_env, "1") == 0;
  }

#if defined(_WIN32)
  ShowWindow(GetConsoleWindow(), SW_HIDE);
  // 测试确定性（与 macOS 的 engine_macos_window_to_main_display 同一开关）：
  // sokol 在 high_dpi=false 时把进程设成 DPI_UNAWARE，高缩放显示器（um560
  // Parsec 3840×2160 @300%）下逻辑屏只有 1280×720，带标题栏的 1280×720 窗口
  // 放不下被系统夹成 1280×701，金图 size mismatch。这里抢先把进程设成
  // per-monitor 感知；sokol 随后的 SetProcessDpiAwareness 失败即保持
  // window_scale=1，窗口/帧缓冲按物理像素精确 1280×720。仅测试开关下生效，
  // 正常 make arc 仍由系统按缩放放大。
  if (getenv("ARC_WINDOW_MAIN_DISPLAY")) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  }
#endif

  sapp_run(&(sapp_desc){
      .init_cb = engine_on_init,
      .frame_cb = engine_on_frame,
      .cleanup_cb = engine_on_cleanup,
      .event_cb = engine_host_event,
      .width = window_w,
      .height = window_h,
      .high_dpi = high_dpi,
      .window_title = g_cfg.window_title,
      .icon.sokol_default = true,
      .logger.func = slog_func,
      .enable_clipboard = true,
      .clipboard_size = 4096,
  });

#ifdef __EMSCRIPTEN__
  emscripten_exit_with_live_runtime();
#endif
  return g_exit_code;
}

JSContext *arc_engine_js_context(Arc_Engine *eng) {
  (void)eng;
  return js_runtime_get_context();
}
