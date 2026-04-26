
#if defined(_WIN32)
#include <windows.h>
#endif

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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static void engine_on_init(void) {
  input_state = &_actual_input_state;

  sound_init();
  stdin_reader_init();

  _game_state.zoom_level = g_cfg.zoom_level > 0 ? g_cfg.zoom_level : 2.0f;
  _game_state.desired_zoom_level = _game_state.zoom_level;

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
  if (g_cfg.on_register_js_bindings) {
    js_runtime_set_game_bindings_registrar(g_cfg.on_register_js_bindings);
  }

  LOG_INFO("[engine] init js_runtime\n");
  if (js_runtime_init() < 0) {
    LOG_ERROR("[engine] js_runtime_init failed\n");
  }

  if (g_cfg.startup_ambiance_event) {
    Vec2 no_pos = {99999.0f, 99999.0f};
    sound_play(g_cfg.startup_ambiance_event, no_pos, 0.0f);
  }

  LOG_INFO("[engine] init done\n");
}

static void engine_on_frame(void) {
  window_w = sapp_width();
  window_h = sapp_height();

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
  if (key_pressed(KEY_ENTER) && key_down(KEY_LEFT_ALT)) {
    sapp_toggle_fullscreen();
  }

  if (!g_cfg.disable_default_camera_controls) {
    // Phase 6 World Units (1 unit = 1 cell): 3.0 cells/sec is too slow
    // for cell-based maps (mapgen 250x250). Game overrides via
    // Engine_Config.camera_pan_speed; fallback 3.0 keeps gunslinger feel.
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

    ctx.gs->cam_pos[0] += dx * speed * ctx.delta_t;
    ctx.gs->cam_pos[2] += dz * speed * ctx.delta_t;
  }

  camera_set_position(camera_get_main(), ctx.gs->cam_pos[0], ctx.gs->cam_pos[1],
                      ctx.gs->cam_pos[2]);
  camera_set_rotation(camera_get_main(), ctx.gs->cam_rot[0], ctx.gs->cam_rot[1],
                      ctx.gs->cam_rot[2]);

  if (!g_cfg.disable_default_camera_controls) {
    if (key_down(KEY_Q)) ctx.gs->desired_zoom_level *= ZOOM_IN_MULTIPLIER;
    if (key_down(KEY_E)) ctx.gs->desired_zoom_level *= ZOOM_OUT_MULTIPLIER;

    float scroll = input_state->scroll_y;
    if (scroll != 0.0f) {
      float zoom_delta = scroll * SCROLL_ZOOM_RATE * ZOOM_SPEED / 35.0f;
      ctx.gs->desired_zoom_level *= (1.0f + zoom_delta);
    }

    if (ctx.gs->desired_zoom_level < MIN_ZOOM_LEVEL)
      ctx.gs->desired_zoom_level = MIN_ZOOM_LEVEL;
    if (ctx.gs->desired_zoom_level > MAX_ZOOM_LEVEL)
      ctx.gs->desired_zoom_level = MAX_ZOOM_LEVEL;

    float zoom_diff =
        (ctx.gs->desired_zoom_level - ctx.gs->zoom_level) * ZOOM_TIGHTNESS;
    ctx.gs->zoom_level += zoom_diff;

    if (ctx.gs->zoom_level < MIN_ZOOM_LEVEL)
      ctx.gs->zoom_level = MIN_ZOOM_LEVEL;
    if (ctx.gs->zoom_level > MAX_ZOOM_LEVEL)
      ctx.gs->zoom_level = MAX_ZOOM_LEVEL;
  }

  char stdin_buf[1024];
  while (stdin_reader_poll(stdin_buf, sizeof(stdin_buf)) == 0) {
    js_runtime_handle_external_command(stdin_buf);
  }

  Vec2 listener = {ctx.gs->cam_pos[0], ctx.gs->cam_pos[2]};
  sound_update(listener, g_master_volume);

  if (g_cfg.on_frame) g_cfg.on_frame(ctx.delta_t);

  core_render_frame_start();
  js_runtime_render();
  core_render_frame_end();

  reset_input_state(input_state);
}

static void engine_on_cleanup(void) {
  if (g_cfg.on_cleanup) g_cfg.on_cleanup();
  stdin_reader_shutdown();
  sg_shutdown();
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
  if (g_cfg.default_run_mode) g_run_mode = g_cfg.default_run_mode;
  if (g_cfg.master_volume > 0.0f) g_master_volume = g_cfg.master_volume;

  parse_args(argc, argv);

  g_engine.initialized = 1;
  return &g_engine;
}

int arc_engine_run(Arc_Engine *eng) {
  if (!eng || !eng->initialized) {
    fprintf(stderr, "[arc_engine_run] engine not created\n");
    return 1;
  }

#if defined(_WIN32)
  ShowWindow(GetConsoleWindow(), SW_HIDE);
#endif

  sapp_run(&(sapp_desc){
      .init_cb = engine_on_init,
      .frame_cb = engine_on_frame,
      .cleanup_cb = engine_on_cleanup,
      .event_cb = event_callback,
      .width = window_w,
      .height = window_h,
      .window_title = g_cfg.window_title,
      .icon.sokol_default = true,
      .logger.func = slog_func,
      .enable_clipboard = true,
      .clipboard_size = 4096,
  });

  return 0;
}

JSContext *arc_engine_js_context(Arc_Engine *eng) {
  (void)eng;
  return js_runtime_get_context();
}
