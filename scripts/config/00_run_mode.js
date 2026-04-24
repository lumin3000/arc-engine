
// Run mode manifest is provided by the game layer before this module runs.
// A game project should set globalThis.__RUN_MODE_MANIFEST__ = {...} in
// its own early-loaded script.

globalThis.RunMode = {
    MODE: globalThis.__RUN_MODE__ || 'default',

    _manifest: globalThis.__RUN_MODE_MANIFEST__ || {
        default: { ui: false, hud: false }
    },

    get config() {
        var m = this._manifest[this.MODE];
        if (!m) throw new Error("[RunMode] Unknown mode: " + this.MODE);
        return m;
    },
    get showUI()       { return !!this.config.ui; },
    get showHUD()      { return !!this.config.hud; },
    get forceMapGen()  { return !!this.config.forceMapGen; },
    get spawnActors()  { return !!this.config.spawnActors; },
    get testMessages() { return !!this.config.testMessages; },
};

jtask.log("[RunMode] Mode = " + globalThis.RunMode.MODE);
