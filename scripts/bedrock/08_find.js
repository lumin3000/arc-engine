
var EngineRefs = (function() {
    'use strict';

    return {

        get root() {
            return EngineState.root;
        },

        get windowStack() {
            return EngineState.root?.windowStack;
        },

        get soundRoot() {
            return EngineState.root?.soundRoot;
        },

        get session() {
            return EngineState.session;
        },

        get tickManager() {
            return EngineState.session?.tickManager;
        },

        get signalManager() {
            return EngineState.session?.signalManager;
        },

        get uniqueIDsManager() {
            return EngineState.session?.uniqueIDsManager;
        },

        get maps() {
            return EngineState.session?.maps;
        },

        get currentMap() {
            return EngineState.session?.currentMap;
        },

        get selector() {
            return EngineState.session?.selector;
        },

        set selector(value) {
            if (EngineState.session) {
                EngineState.session.selector = value;
            }
        },

        get cameraController() {
            return EngineState.session?.cameraController;
        },

        set cameraController(value) {
            if (EngineState.session) {
                EngineState.session.cameraController = value;
            }
        },

        get world() {
            return EngineState.session?.world ?? null;
        },

        // EngineRefs only exposes engine-owned references (root, session,
        // currentMap, tickManager, cameraController, etc.). Consumers extend
        // by assigning EngineRefs.<name> from their own scripts.

        get hasSession() {
            return EngineState.hasSession;
        },

        get hasRoot() {
            return EngineState.hasRoot;
        },

        set currentMap(value) {
            if (EngineState.session) {
                EngineState.session.setCurrentMap(value);
            } else {
                jtask.log("[EngineRefs] WARN: Session not initialized, cannot set currentMap");
            }
        },

        set maps(value) {

        },

        set tickManager(value) {

        },

        _dataReady: false,
        get dataReady() { return this._dataReady; },
        set dataReady(value) { this._dataReady = value; }
    };
})();

globalThis.EngineRefs = EngineRefs;

jtask.log("[EngineRefs] Module loaded (read-only shortcuts)");
