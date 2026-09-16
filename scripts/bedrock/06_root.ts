
var EngineRoot = (function() {
    'use strict';

    // soundRoot/uiRoot are currently null; windowStack is supplied by the consumer extension.
    type EngineRootWindowStackSlot = EngineRootExtension["windowStack"];
    var _soundRoot: null = null;
    var _uiRoot: null = null;
    var _windowStack: EngineRootWindowStackSlot = null;
    var _initialized = false;

    return {

        get soundRoot() { return _soundRoot; },
        get uiRoot() { return _uiRoot; },
        get windowStack() { return _windowStack; },
        get initialized() { return _initialized; },

        init: function() {
            if (_initialized) {
                jtask.log.error("[EngineRoot] Already initialized");
                return;
            }

            jtask.log("[EngineRoot] Initializing...");

            _initialized = true;
            jtask.log("[EngineRoot] Initialized");
        },

        initWindowStack: function() {
            if (_windowStack) return;
            if (typeof WindowStack !== 'undefined') {

                _windowStack = WindowStack;

                jtask.log("[EngineRoot] WindowStack attached (lazy init)");
            }
        },

        update: function() {
            if (!_initialized) return;

            if (_windowStack && typeof _windowStack.windowStackOnGUI === 'function') {
                _windowStack.windowStackOnGUI();
            }

        },

        shutdown: function() {
            jtask.log("[EngineRoot] Shutting down...");
            _windowStack = null;
            _soundRoot = null;
            _uiRoot = null;
            _initialized = false;
        }
    };
})();

globalThis.EngineRoot = EngineRoot;

jtask.log("[EngineRoot] Module loaded");
