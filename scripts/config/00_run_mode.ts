
// Run mode manifest is provided by the game at any point before a code
// path first calls into RunMode's getters. Game scripts set
// globalThis.__RUN_MODE_MANIFEST__ = { <mode>: {...}, ... }. The getters
// below read it lazily each access, so load order is not critical.

// 当前运行模式名由 C 启动参数注入；模式清单 __RUN_MODE_MANIFEST__ 由游戏脚本提供
declare global {
    var __RUN_MODE__: string | undefined;
}

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
};

jtask.log("[RunMode] Module loaded (mode resolved on first access)");
