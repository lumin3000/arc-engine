
globalThis.ScaffoldPriority = {

    LONG_EVENT_CHECK: 10,

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

    TERRAIN: 400,
    BUILDING: 500,
    DYNAMIC_THINGS: 550,
    SELECTION_OVERLAY: 560,
    GHOST_PREVIEW: 570,

    COORD_SCREEN_SPACE: 600,

    HUD: 700,
    UI_TEXT: 750,
    GUI: 800,
    GUI_OVERLAY: 850,
    DEBUG_OVERLAY: 890,

    FRAME_END: 900,
    DIAGNOSTICS: 950,
};

ScaffoldPriority.GROUPS = {
    BLOCKING:     { min: 0,   max: 99,  name: "Blocking" },
    INPUT:        { min: 100, max: 199, name: "Input" },
    LOGIC:        { min: 200, max: 299, name: "Logic" },
    RENDER_SETUP: { min: 300, max: 399, name: "RenderSetup" },
    WORLD_RENDER: { min: 400, max: 599, name: "WorldRender" },
    SCREEN_SETUP: { min: 600, max: 699, name: "ScreenSetup" },
    SCREEN_RENDER:{ min: 700, max: 899, name: "ScreenRender" },
    FRAME_END:    { min: 900, max: 999, name: "ScaffoldEnd" },
};

globalThis.ScaffoldCallbacks = {
    _list: [],
    _sorted: false,

    register: function(callback, priority, name) {
        if (typeof callback !== 'function') {
            jtask.log("[ScaffoldCallbacks] ERROR: callback must be a function");
            return;
        }
        if (typeof priority !== 'number') {
            jtask.log("[ScaffoldCallbacks] ERROR: priority must be a number");
            return;
        }
        this._list.push({ callback, priority, name: name || "(anonymous)" });
        this._sorted = false;
        if (globalThis.__BP_VERBOSE__) {
            jtask.log("[ScaffoldCallbacks] +" + (name || "anon") + " @" + priority);
        }
    },

    unregister: function(callback) {
        const idx = this._list.findIndex(item => item.callback === callback);
        if (idx !== -1) {
            const removed = this._list.splice(idx, 1)[0];
            if (globalThis.__BP_VERBOSE__) {
                jtask.log("[ScaffoldCallbacks] -" + removed.name);
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
                        jtask.log("[ScaffoldCallbacks] blocked by " + item.name);
                    }
                    return;
                }
            } catch (e) {
                jtask.log("[ScaffoldCallbacks] ERROR " + item.name + ": " + e.message);
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
        jtask.log("[ScaffoldCallbacks] === " + this._list.length + " callbacks ===");
        for (const item of this._list) {
            let group = "?";
            for (const k in ScaffoldPriority.GROUPS) {
                const g = ScaffoldPriority.GROUPS[k];
                if (item.priority >= g.min && item.priority <= g.max) {
                    group = g.name;
                    break;
                }
            }
            jtask.log("  [" + item.priority + "] " + item.name + " (" + group + ")");
        }
    }
};

if (typeof LongEventHandler !== 'undefined') {
    ScaffoldCallbacks.register(function(dt) {
        if (LongEventHandler.ShouldWaitForEvent) {
            coord.push_screen_space();
            LongEventHandler.OnGUI();
            ScaffoldCallbacks.block();
        }
    }, ScaffoldPriority.LONG_EVENT_CHECK, "LongEventHandler.BlockCheck");
}

if (globalThis.__BP_VERBOSE__) {
    jtask.log("[ScaffoldCallbacks] loaded");
}
