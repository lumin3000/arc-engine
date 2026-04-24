
// Run mode manifest is provided by the game at any point before a code
// path first calls into RunMode's getters. Game scripts set
// globalThis.__RUN_MODE_MANIFEST__ = { <mode>: {...}, ... }. The getters
// below read it lazily each access, so load order is not critical.

globalThis.RunMode = {
    get MODE() { return globalThis.__RUN_MODE__ || 'default'; },

    get _manifest() {
        return globalThis.__RUN_MODE_MANIFEST__ || {
            default: { ui: false, hud: false }
        };
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

jtask.log("[RunMode] Module loaded (mode resolved on first access)");
