
const _engineStateFactory = function() {  // 消费者可在其上挂自己的状态字段（索引签名开放），引擎成员保持精确类型
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
}; var EngineState = _engineStateFactory() as ReturnType<typeof _engineStateFactory> & { [key: string]: any };

globalThis.EngineState = EngineState;

jtask.log("[EngineState] Module loaded");
