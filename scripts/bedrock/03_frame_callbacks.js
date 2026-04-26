
globalThis.FrameStagePriority = {

    BLOCKING_TASK_CHECK: 10,

    INPUT_EARLY: 100,
    HOTKEY: 110,
    INPUT: 120,
    INPUT_LATE: 190,

    TICK_PRE: 200,
    TICK: 210,
    TICK_POST: 220,
    SELECTOR: 250,

    RENDER_SETUP: 300,
    COORD_CLIP_SPACE: 310,
    COORD_WORLD_SPACE: 320,

    WORLD_LAYER_1: 400,
    WORLD_LAYER_2: 500,
    WORLD_LAYER_3: 550,
    SELECTION_OVERLAY: 560,
    WORLD_LAYER_4: 570,

    COORD_SCREEN_SPACE: 600,

    HUD: 700,
    UI_TEXT: 750,
    GUI: 800,
    GUI_OVERLAY: 850,
    DEBUG_OVERLAY: 890,

    FRAME_END: 900,
    DIAGNOSTICS: 950,
};

FrameStagePriority.GROUPS = {
    BLOCKING:     { min: 0,   max: 99,  name: "Blocking" },
    INPUT:        { min: 100, max: 199, name: "Input" },
    LOGIC:        { min: 200, max: 299, name: "Logic" },
    RENDER_SETUP: { min: 300, max: 399, name: "RenderSetup" },
    WORLD_RENDER: { min: 400, max: 599, name: "WorldRender" },
    SCREEN_SETUP: { min: 600, max: 699, name: "ScreenSetup" },
    SCREEN_RENDER:{ min: 700, max: 899, name: "ScreenRender" },
    FRAME_END:    { min: 900, max: 999, name: "FrameEnd" },
};

globalThis.FrameStageCallbacks = {
    _list: [],
    _sorted: false,

    register: function(callback, priority, name) {
        if (typeof callback !== 'function') {
            jtask.log("[FrameStageCallbacks] ERROR: callback must be a function");
            return;
        }
        if (typeof priority !== 'number') {
            jtask.log("[FrameStageCallbacks] ERROR: priority must be a number");
            return;
        }
        this._list.push({ callback, priority, name: name || "(anonymous)" });
        this._sorted = false;
        if (globalThis.__BP_VERBOSE__) {
            jtask.log("[FrameStageCallbacks] +" + (name || "anon") + " @" + priority);
        }
    },

    unregister: function(callback) {
        const idx = this._list.findIndex(item => item.callback === callback);
        if (idx !== -1) {
            const removed = this._list.splice(idx, 1)[0];
            if (globalThis.__BP_VERBOSE__) {
                jtask.log("[FrameStageCallbacks] -" + removed.name);
            }
        }
    },

    _blocked: false,

    block: function() {
        this._blocked = true;
    },

    isBlocked: function() {
        return this._blocked;
    },

    runAll: function(dt) {
        this._blocked = false;

        if (!this._sorted) {
            this._list.sort((a, b) => a.priority - b.priority);
            this._sorted = true;
        }
        for (let i = 0; i < this._list.length; i++) {
            const item = this._list[i];
            try {
                item.callback(dt);

                if (this._blocked) {
                    if (globalThis.__BP_VERBOSE__) {
                        jtask.log("[FrameStageCallbacks] blocked by " + item.name);
                    }
                    return;
                }
            } catch (e) {
                jtask.log("[FrameStageCallbacks] ERROR " + item.name + ": " + e.message);
                if (e.stack) jtask.log(e.stack);
            }
        }
    },

    clear: function() {
        this._list = [];
        this._sorted = false;
    },

    dump: function() {
        if (!this._sorted) {
            this._list.sort((a, b) => a.priority - b.priority);
            this._sorted = true;
        }
        jtask.log("[FrameStageCallbacks] === " + this._list.length + " callbacks ===");
        for (const item of this._list) {
            let group = "?";
            for (const k in FrameStagePriority.GROUPS) {
                const g = FrameStagePriority.GROUPS[k];
                if (item.priority >= g.min && item.priority <= g.max) {
                    group = g.name;
                    break;
                }
            }
            jtask.log("  [" + item.priority + "] " + item.name + " (" + group + ")");
        }
    }
};

if (typeof BlockingTaskQueue !== 'undefined') {
    FrameStageCallbacks.register(function(dt) {
        if (BlockingTaskQueue.isBlocking) {
            coord.push_screen_space();
            BlockingTaskQueue.drawProgress();
            FrameStageCallbacks.block();
        }
    }, FrameStagePriority.BLOCKING_TASK_CHECK, "BlockingTaskQueue.BlockCheck");
}

// ============================================================================
// RenderFrameCallbacks — mainthread render-frame hooks. Defined here in
// bedrock so games can register callbacks at module-load time, before
// rt/render_service.js (which is bundled last) actually runs the loop.
// See rt/render_service.js for the consumer (calls runAll(dt) inside
// render_frame, before FrameStageCallbacks.runAll).
// ============================================================================
globalThis.RenderFrameCallbacks = {
  _list: [],
  register: function(fn, name) {
    if (typeof fn !== 'function') {
      throw new Error("[RenderFrameCallbacks] callback must be a function");
    }
    this._list.push({ fn: fn, name: name || "anonymous" });
  },
  runAll: function(dt) {
    for (const cb of this._list) {
      try {
        cb.fn(dt);
      } catch (e) {
        jtask.log.error("[RenderFrameCallbacks] '" + cb.name + "' threw: " + e.message + "\n" + (e.stack || ""));
      }
    }
  },
};

if (globalThis.__BP_VERBOSE__) {
    jtask.log("[FrameStageCallbacks] loaded");
}
