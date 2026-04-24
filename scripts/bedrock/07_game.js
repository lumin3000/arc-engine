
var Game = (function() {
    'use strict';

    var _tickManager = null;
    var _signalManager = null;
    var _uniqueIDsManager = null;
    var _selector = null;
    var _cameraDriver = null;
    var _currentMap = null;
    var _maps = [];
    var _initialized = false;

    return {

        get tickManager() { return _tickManager; },
        get signalManager() { return _signalManager; },
        get uniqueIDsManager() { return _uniqueIDsManager; },
        get selector() { return _selector; },
        set selector(value) { _selector = value; },
        get cameraDriver() { return _cameraDriver; },
        set cameraDriver(value) { _cameraDriver = value; },
        get currentMap() { return _currentMap; },
        set currentMap(map) { _currentMap = map; },
        get maps() { return _maps; },
        get initialized() { return _initialized; },

        init: function() {
            if (_initialized) {
                jtask.log.error("[Game] Already initialized");
                return;
            }

            jtask.log("[Game] Initializing...");

            _maps = [];
            _currentMap = null;

            _initialized = true;
            jtask.log("[Game] Initialized");
        },

        initManagers: function() {
            if (!_tickManager && typeof TickManager !== 'undefined') {
                _tickManager = new TickManager();
            }
            if (!_signalManager && typeof SignalManager !== 'undefined') {
                _signalManager = new SignalManager();
            }
            if (!_uniqueIDsManager && typeof UniqueIDsManager !== 'undefined') {
                _uniqueIDsManager = new UniqueIDsManager();
            }
        },

        setCurrentMap: function(map) {
            _currentMap = map;
            if (map && _maps.indexOf(map) === -1) {
                _maps.push(map);

                if (_tickManager) {
                    _tickManager.registerMap(map);
                }
            }
            jtask.log("[Game] Current map set");
        },

        addMap: function(map) {
            if (_maps.indexOf(map) === -1) {
                _maps.push(map);

                if (_tickManager) {
                    _tickManager.registerMap(map);
                }
            }
        },

        update: function() {
            if (!_initialized) return;

            if (_tickManager && typeof _tickManager.doSingleTick === 'function') {
                _tickManager.doSingleTick();
            }
        },

        shutdown: function() {
            jtask.log("[Game] Shutting down...");

            _maps = [];
            _currentMap = null;

            _tickManager = null;
            _signalManager = null;
            _uniqueIDsManager = null;
            _selector = null;
            _cameraDriver = null;

            _initialized = false;
            jtask.log("[Game] Shutdown complete");
        }
    };
})();

globalThis.Game = Game;

jtask.log("[Game] Module loaded");
