// arc-engine render service.
//
// The frame loop only. Owns:
//   - jtask service registration ("render" + "game")
//   - LongEventHandler.Update() per frame
//   - mainthread_run(render_frame), which dispatches to:
//       RenderFrameCallbacks.runAll(dt)   — game-side mainthread work
//       ScaffoldCallbacks.runAll(dt)      — game-side worker-thread chain
//   - imgui.begin_frame() if imgui is available
//   - RealTime.update(dt)
//   - file_loaded forwarding to G.loader_onFileLoaded
//   - stdin_command forwarding to G.handleExternalCommand
//
// Everything game-specific (atlas preload, plant sway, terrain section
// mesh, HUD, RenderSetup / Coord.* / etc.) lives in the consumer's
// scripts/game/ tree and registers via:
//   - StartupFlow.registerStartupSteps([...])  for staged startup work
//   - RenderFrameCallbacks.register(fn, name)  for per-frame mainthread
//   - ScaffoldCallbacks.register(fn, prio, name) for per-frame worker
//
// See: arc-mapgen/scripts/game/arc/00z_mapgen_render.js,
//      sokol-javascript-game-gunslinger/scripts/game/gunslinger/01_default_callbacks.js

const jtask = globalThis.jtask;
globalThis.__BP_VERBOSE__ = false;

const S = {
  _gc: {
    finalize() {
      jtask.log("[render] exit");
    },
  },
};

let frame_count = 0;
let last_frame_time = 0;

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

    RenderFrameCallbacks.runAll(dt);

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
