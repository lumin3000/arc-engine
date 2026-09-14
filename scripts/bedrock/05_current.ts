
const _engineStateFactory = function() {  // 消费者状态字段经 EngineStateExtension 声明，引擎成员保持精确类型
    'use strict';

    // session 上挂有消费者世界字段（world/storyteller…）且大量调用点未判空（动态边界例外，负责 B12）
    type EngineStateSessionSlot = any;
    var _root: typeof EngineRoot | null = null;
    var _session: EngineStateSessionSlot = null;

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
}; var EngineState = _engineStateFactory() as ReturnType<typeof _engineStateFactory> & EngineStateExtension;

globalThis.EngineState = EngineState;

jtask.log("[EngineState] Module loaded");
