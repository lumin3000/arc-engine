declare var SignalManager: any; declare var UniqueIDsManager: any;  // 消费者可选提供，引擎只探测
// 消费者对象槽位（动态边界例外，逐项登记 tests/typecheck_strict_exceptions.json；负责批收紧为消费者真实类型）
type EngineSessionMapSlot = any;       // 地图（负责 B07）
type EngineSessionSelectorSlot = any;  // 选择器（负责 B22）
type EngineSessionManagerSlot = any;   // SignalManager/UniqueIDsManager 实例（负责 B20）
var EngineSession = (function() {
    'use strict';

    var _tickManager: TickScheduler | null = null;
    var _signalManager: EngineSessionManagerSlot = null;
    var _uniqueIDsManager: EngineSessionManagerSlot = null;
    var _selector: EngineSessionSelectorSlot = null;
    var _cameraController: CameraController | null = null;
    var _currentMap: EngineSessionMapSlot = null;
    var _maps: EngineSessionMapSlot[] = [];
    var _initialized = false;

    return {

        get tickManager() { return _tickManager; },
        get signalManager() { return _signalManager; },
        get uniqueIDsManager() { return _uniqueIDsManager; },
        get selector() { return _selector; },
        set selector(value) { _selector = value; },
        get cameraController() { return _cameraController; },
        set cameraController(value) { _cameraController = value; },
        get currentMap() { return _currentMap; },
        set currentMap(map) { _currentMap = map; },
        get maps() { return _maps; },
        get initialized() { return _initialized; },

        init: function() {
            if (_initialized) {
                jtask.log.error("[EngineSession] Already initialized");
                return;
            }

            jtask.log("[EngineSession] Initializing...");

            _maps = [];
            _currentMap = null;

            _initialized = true;
            jtask.log("[EngineSession] Initialized");
        },

        initManagers: function() {
            if (!_tickManager && typeof TickScheduler !== 'undefined') {
                _tickManager = new TickScheduler();
            }
            if (!_signalManager && typeof SignalManager !== 'undefined') {
                _signalManager = new SignalManager();
            }
            if (!_uniqueIDsManager && typeof UniqueIDsManager !== 'undefined') {
                _uniqueIDsManager = new UniqueIDsManager();
            }
        },

        setCurrentMap: function(map: EngineSessionMapSlot) {
            _currentMap = map;
            if (map && _maps.indexOf(map) === -1) {
                _maps.push(map);

                if (_tickManager) {
                    _tickManager.registerMap(map);
                }
            }
            jtask.log("[EngineSession] Current map set");
        },

        addMap: function(map: EngineSessionMapSlot) {
            if (_maps.indexOf(map) === -1) {
                _maps.push(map);

                if (_tickManager) {
                    _tickManager.registerMap(map);
                }
            }
        },

        // infmap P3: 图退役 (重建壳事务换图 / 多图休眠 L6)。tick 注销 + 会话摘除;
        // currentMap 若指向该图一并清空, 由调用方随后 setCurrentMap 新图。
        removeMap: function(map: EngineSessionMapSlot) {
            var idx = _maps.indexOf(map);
            if (idx > -1) {
                _maps.splice(idx, 1);
            }
            if (_tickManager && typeof _tickManager.deregisterMap === 'function') {
                _tickManager.deregisterMap(map);
            }
            if (_currentMap === map) {
                _currentMap = null;
            }
        },

        update: function() {
            if (!_initialized) return;

            if (_tickManager && typeof _tickManager.doSingleTick === 'function') {
                _tickManager.doSingleTick();
            }
        },

        shutdown: function() {
            jtask.log("[EngineSession] Shutting down...");

            _maps = [];
            _currentMap = null;

            _tickManager = null;
            _signalManager = null;
            _uniqueIDsManager = null;
            _selector = null;
            _cameraController = null;

            _initialized = false;
            jtask.log("[EngineSession] Shutdown complete");
        }
    };
})();

globalThis.EngineSession = EngineSession;

jtask.log("[EngineSession] Module loaded");
