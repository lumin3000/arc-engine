
var Current = (function() {
    'use strict';

    var _root = null;
    var _game = null;

    return {

        get root() { return _root; },
        set root(value) {
            if (_root !== null && value !== null) {
                jtask.log.error("[Current] Root already exists, cannot replace");
                return;
            }
            _root = value;
            jtask.log("[Current] Root " + (value ? "created" : "destroyed"));
        },

        get game() { return _game; },
        set game(value) {
            if (_game !== null && value !== null) {
                jtask.log.error("[Current] Game already exists, cannot replace");
                return;
            }
            _game = value;
            jtask.log("[Current] Game " + (value ? "created" : "destroyed"));
        },

        get hasGame() { return _game !== null; },
        get hasRoot() { return _root !== null; }
    };
})();

globalThis.Current = Current;

jtask.log("[Current] Module loaded");
