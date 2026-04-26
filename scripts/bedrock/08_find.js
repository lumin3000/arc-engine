
var Find = (function() {
    'use strict';

    return {

        get root() {
            return Current.root;
        },

        get windowStack() {
            return Current.root?.windowStack;
        },

        get soundRoot() {
            return Current.root?.soundRoot;
        },

        get game() {
            return Current.game;
        },

        get tickManager() {
            return Current.game?.tickManager;
        },

        get signalManager() {
            return Current.game?.signalManager;
        },

        get uniqueIDsManager() {
            return Current.game?.uniqueIDsManager;
        },

        get maps() {
            return Current.game?.maps;
        },

        get currentMap() {
            return Current.game?.currentMap;
        },

        get selector() {
            return Current.game?.selector;
        },

        set selector(value) {
            if (Current.game) {
                Current.game.selector = value;
            }
        },

        get cameraDriver() {
            return Current.game?.cameraDriver;
        },

        set cameraDriver(value) {
            if (Current.game) {
                Current.game.cameraDriver = value;
            }
        },

        get world() {
            return Current.game?.world ?? null;
        },

        // Engine `Find` only exposes engine-owned references (root, game,
        // currentMap, tickManager, cameraDriver, etc.). Games extend by
        // assigning `Find.<name>` from their own scripts.

        get hasGame() {
            return Current.hasGame;
        },

        get hasRoot() {
            return Current.hasRoot;
        },

        set currentMap(value) {
            if (Current.game) {
                Current.game.setCurrentMap(value);
            } else {
                jtask.log("[Find] WARN: Game not initialized, cannot set currentMap");
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

globalThis.Find = Find;

jtask.log("[Find] Module loaded (read-only shortcuts)");
