
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

        get worldGrid() {
            return Current.game?.world?.grid ?? null;
        },

        get worldObjects() {
            return Current.game?.world?.worldObjects ?? null;
        },

        get mapDrawer() {
            return Current.game?.currentMap?.mapDrawer;
        },

        get thingGrid() {
            return Current.game?.currentMap?.thingGrid;
        },

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
