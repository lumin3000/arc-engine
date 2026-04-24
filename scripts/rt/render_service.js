
const jtask = globalThis.jtask;
globalThis.__BP_VERBOSE__ = false;

let plant_sway_head = 0.0;
const WIND_SPEED = 0.5;

function updateFloraSwayHead() {
  plant_sway_head += Math.min(WIND_SPEED, 1.0);
  if (typeof render !== 'undefined' && render.set_sway_head) {
    render.set_sway_head(plant_sway_head);
  }
}

const S = {
  _gc: {
    finalize() {
      jtask.log("[render] exit");
    },
  },
};

let frame_count = 0;
let last_frame_time = 0;

let terrain_atlas_uv_array = null;
let terrain_textures_preloaded = false;
let plant_textures_preloaded = false;
let linked_textures_preloaded = false;
let atlas_needs_upload = false;
let atlas_prebaked_need_upload = false;
const PREBAKED_ATLAS_DIR = "data/atlas";

function* preloadAllAtlasTextures() {
  jtask.log("[atlas] preloadAllAtlasTextures: starting");

  const atlas = getTextureAtlas();

  yield { status: "Reading prebaked atlas...", progress: 0.1 };
  yield;

  const slotIds = atlas.loadPrebakedRead(PREBAKED_ATLAS_DIR);
  if (!slotIds) {
    throw new Error("[atlas] FATAL: Prebaked atlas not found at " + PREBAKED_ATLAS_DIR + ". Run 'make atlas-prebake' first.");
  }

  if (typeof atlas.applyModPack === 'function') {
    yield { status: "Packing mod textures...", progress: 0.3 };
    atlas.applyModPack();
  }

  terrain_atlas_uv_array = atlas.getTerrainUvMapForC();
  terrain_textures_preloaded = true;
  plant_textures_preloaded = true;
  linked_textures_preloaded = true;

  atlas_prebaked_need_upload = true;
  atlas_needs_upload = false;

  yield { status: "Uploading atlas to GPU...", progress: 0.5 };

  while (atlas_prebaked_need_upload) {
    yield;
  }

  yield { status: "Prebaked atlas loaded", progress: 1.0 };
  jtask.log("[atlas] preloadAllAtlasTextures: prebaked loaded (split-frame)");
}

globalThis.preloadAllAtlasTextures = preloadAllAtlasTextures;

globalThis.markAtlasDesiresUpload = function () {
  atlas_needs_upload = true;
};

function register_system_callbacks() {
  if (typeof ScaffoldCallbacks === 'undefined') {
    return;
  }

  ScaffoldCallbacks.register(function(dt) {
  }, ScaffoldPriority.RENDER_SETUP, "RenderSetup");

  ScaffoldCallbacks.register(function(dt) {
    coord.push_clip_space();
  }, ScaffoldPriority.COORD_CLIP_SPACE, "Coord.ClipSpace");

  ScaffoldCallbacks.register(function(dt) {
    coord.push_world_space();
  }, ScaffoldPriority.COORD_WORLD_SPACE, "Coord.WorldSpace");

  ScaffoldCallbacks.register(function(dt) {
    coord.push_screen_space();
  }, ScaffoldPriority.COORD_SCREEN_SPACE, "Coord.ScreenSpace");

  ScaffoldCallbacks.register(function(dt) {
    var dcTotal = (typeof diag !== 'undefined' && diag.numDraw) ? diag.numDraw() : 0;
    draw.text(4, 480, "Frame: " + frame_count + "  DC: " + dcTotal, {
      pivot: draw.PIVOT_TOP_LEFT,
      z_layer: draw.ZLAYER_UI,
      col: [0.0, 1.0, 1.0, 1.0],
      font_size: 18
    });
  }, ScaffoldPriority.HUD, "HUD.FrameCounter");

  if (globalThis.__BP_VERBOSE__) {
    jtask.log("[render_service] system callbacks registered");
  }
}

function render_frame() {
  try {
    const now = Number(message.now()) / 1000;
    if (last_frame_time === 0) last_frame_time = now;
    const dt = now - last_frame_time;
    last_frame_time = now;

    frame_count++;
    RealTime.update(dt);

    if (typeof imgui !== 'undefined' && imgui.begin_frame) {
      imgui.begin_frame();
    }

    updateFloraSwayHead();

    if (atlas_prebaked_need_upload) {
      const atlas = getTextureAtlas();
      if (atlas.finishPrebakedUpload()) {
        jtask.log("[render] Prebaked atlas GPU upload done");
      } else {
        jtask.log.error("[render] Prebaked atlas GPU upload FAILED");
      }
      atlas_prebaked_need_upload = false;
    }

    if (atlas_needs_upload) {
      getTextureAtlas().upload();
      atlas_needs_upload = false;
      jtask.log("[render] Atlas uploaded to GPU (1 slot)");
    }

    ScaffoldCallbacks.runAll(dt);

  } catch (e) {
    jtask.log.error("[render_frame] CRITICAL ERROR: " + e.message + "\n" + e.stack);
  }
}

let rtt_frame_count = 0;
let rtt_total_us = 0;

S.frame = function (msg) {
  rtt_frame_count++;

  const frame_t0 = RealTime.realtimeSinceStartupUs();

  if (typeof LongEventHandler !== "undefined") {
    try {
      LongEventHandler.Update();
    } catch (e) {
      jtask.log.error("[S.frame] LongEventHandler.Update() threw: " + e.message + "\n" + (e.stack || ""));
    }
  }

  const logic_t1 = RealTime.realtimeSinceStartupUs();
  const logic_us = logic_t1 - frame_t0;
  if (globalThis.__BP_VERBOSE__ && logic_us > 50000) {
    jtask.log("[S.frame] SLOW logic: " + (logic_us / 1000).toFixed(1) + " ms");
  }

  const rtt_start = RealTime.realtimeSinceStartupUs();
  const ret = jtask.mainthread_run(render_frame);
  const rtt_end = RealTime.realtimeSinceStartupUs();

  const rtt_us = rtt_end - rtt_start;
  rtt_total_us += rtt_us;

  if (globalThis.__BP_VERBOSE__ && rtt_frame_count % 60 === 0) {
    let mem = "N/A";
    if (jtask.memory_usage) {
      const m = jtask.memory_usage();
      mem = (m.memory_used_size / 1024 / 1024).toFixed(2) + "MB";
    }
    jtask.log.info("[RTT] Scaffold " + rtt_frame_count + " | RTT: " + rtt_us + "us | Avg: " + (rtt_total_us / rtt_frame_count).toFixed(1) + "us | Mem: " + mem);
  }

  if (!ret.success) {
    jtask.log.error("[render] mainthread_run failed: " + ret.error);
  }
};

S.SERVICE_ID = 0;

S.external = function (msg) {
  if (msg.cmd === "frame") {
    S.frame(msg);
    return;
  }
};

S.stdin_command = function (msg) {
  let parsed = msg;
  if (typeof msg === "string") {
    try {
      parsed = JSON.parse(msg);
    } catch (e) {
      return;
    }
  }
  if (parsed && parsed.cmd && typeof G.handleExternalCommand === "function") {
    G.handleExternalCommand(parsed);
  }
};

S.query_damage = function (weapon, armor) {};

S.file_loaded = function (msg) {
  if (typeof G.loader_onFileLoaded === "function") {
    G.loader_onFileLoaded(msg);
  }
};

const self_id = jtask.self();

jtask.fork(function () {
  message.register_service("render", self_id);
  message.register_service("game", self_id);

  jtask.mainthread_run(function () {
    if (typeof message.inject_render_modules === 'function') {
      message.inject_render_modules();
    }
    if (typeof message.load_mod_scripts === 'function') {
      message.load_mod_scripts();
    }
  });

  register_system_callbacks();

  const ret = jtask.mainthread_run(function () {
    render_frame();
  });

  if (!ret.success) {
    jtask.log.error("[game_service] init mainthread_run failed: " + ret.error);
  }

  if (globalThis.__BP_VERBOSE__) {
    jtask.log("[game_service] render ready, id=" + self_id);
  }
});

G.__SERVICE__ = S;
