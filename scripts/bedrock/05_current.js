
var EngineState = (function() {
    'use strict';

    var _root = null;
    var _session = null;

    return {

        get root() { return _root; },
        set root(value) {
            if (_root !== null && value !== null) {
                jtask.log.error("[EngineState] Root already exists, cannot replace");
                return;
            }
            _root = value;
            jtask.log("[EngineState] Root " + (value ? "created" : "destroyed"));
        },

        get session() { return _session; },
        set session(value) {
            if (_session !== null && value !== null) {
                jtask.log.error("[EngineState] Session already exists, cannot replace");
                return;
            }
            _session = value;
            jtask.log("[EngineState] Session " + (value ? "created" : "destroyed"));
        },

        get hasSession() { return _session !== null; },
        get hasRoot() { return _root !== null; }
    };
})();

globalThis.EngineState = EngineState;

jtask.log("[EngineState] Module loaded");
