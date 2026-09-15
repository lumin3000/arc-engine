
const _engineStateFactory = function() {  // 消费者状态字段经 EngineStateExtension 声明，引擎成员保持精确类型
    'use strict';

    // world 为唯一可缺成员：99_startup.ts:57 以无 world 键的 EngineSession 建会话，世界生成 world/core/08_world_generator.ts:1286/:1344 才写入；currentMap 由 EngineSession 自身声明（EngineSessionMapSlot）
    type EngineStateSessionSlot = (typeof EngineSession & Partial<Pick<EngineSessionExtension, "world">>) | null;
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
