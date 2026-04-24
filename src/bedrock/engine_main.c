
#if defined(_WIN32)
#include <windows.h>
#endif

#include "engine_main.h"
#include "gfx/camera.h"
#include "gfx/render.h"
#include "helpers.h"
#include "input/input.h"
#include "js_runtime.h"
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

// ---------------------------------------------------------------------------
// Globals exposed to engine bindings (engine_bindings.c reads these via extern).

int window_w = DEFAULT_WINDOW_WIDTH;
int window_h = DEFAULT_WINDOW_HEIGHT;
int g_log_level = LOG_LEVEL_NORMAL;
const char *g_run_mode = "default";
float g_master_volume = 0.25f;

typedef struct {
  uint64_t ticks;
  double game_time_elapsed;
  Vec3 cam_pos;
  Vec3 cam_rot;
  Vec3 cam_vel;
  float zoom_level;
  float desired_zoom_level;
} Game_State;

typedef struct {
  Game_State *gs;
  float delta_t;
} Core_Context;

Core_Context ctx = {0};
static Game_State _game_state = {0};

// ---------------------------------------------------------------------------
// Engine configuration and saved argv.

static Engine_Config g_cfg = {0};
static char **g_saved_argv = NULL;
static int g_saved_argc = 0;

char **app_get_argv(void) { return g_saved_argv; }
int app_get_argc(void) { return g_saved_argc; }

static double last_frame_time = 0.0;

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

  const float BASE_ORTHO_SIZE = 10.0f;
  camera_set_ortho_size(camera_get_main(), BASE_ORTHO_SIZE);

  float initial_aspect = (float)window_w / (float)window_h;
  camera_set_aspect(camera_get_main(), initial_aspect);

  if (g_cfg.jtask_bootstrap_path) {
    js_runtime_set_bootstrap_path(g_cfg.jtask_bootstrap_path);
  }
  if (g_cfg.js_main_script_path) {
    js_runtime_set_main_script_path(g_cfg.js_main_script_path);
  }

  LOG_INFO("[engine] init js_runtime\n");
  if (js_runtime_init() < 0) {
    LOG_ERROR("[engine] js_runtime_init failed\n");
  }

  // Game registers its own JS bindings now (engine bindings already
  // registered by js_runtime_init).
  if (g_cfg.on_register_js_bindings) {
    g_cfg.on_register_js_bindings(js_runtime_get_context());
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

  const float BASE_ORTHO_SIZE = 10.0f;
  float final_ortho_size = BASE_ORTHO_SIZE / ctx.gs->zoom_level;
  camera_set_ortho_size(camera_get_main(), final_ortho_size);

  float aspect = (float)window_w / (float)window_h;
  camera_set_aspect(camera_get_main(), aspect);

  double now = seconds_since_init();
  double dt = now - last_frame_time;
  last_frame_time = now;
  if (dt > MIN_FRAME_TIME) dt = MIN_FRAME_TIME;

  ctx.delta_t = (float)dt;
  ctx.gs->ticks++;
  ctx.gs->game_time_elapsed = now;

  input_state = &_actual_input_state;
  if (key_pressed(KEY_ENTER) && key_down(KEY_LEFT_ALT)) {
    sapp_toggle_fullscreen();
  }

  {
    float speed = 3.0f;
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

  {
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
// Public entry.

int engine_run(const Engine_Config *cfg, int argc, char **argv) {
#if defined(_WIN32)
  ShowWindow(GetConsoleWindow(), SW_HIDE);
#endif

  if (!cfg || !cfg->window_title) {
    fprintf(stderr, "[engine_run] cfg->window_title is required\n");
    return 1;
  }

  g_cfg = *cfg;
  g_saved_argc = argc;
  g_saved_argv = argv;

  if (g_cfg.window_w > 0) window_w = g_cfg.window_w;
  if (g_cfg.window_h > 0) window_h = g_cfg.window_h;
  if (g_cfg.default_run_mode) g_run_mode = g_cfg.default_run_mode;
  if (g_cfg.master_volume > 0.0f) g_master_volume = g_cfg.master_volume;

  parse_args(argc, argv);

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
