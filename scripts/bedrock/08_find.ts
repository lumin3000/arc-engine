
const _engineRefsFactory = function() {  // 消费者已登记成员经 EngineRefsExtension 声明（消费者 boundary_contracts extensions 生成）；索引签名仍开放，可容纳未声明键（例外登记）；引擎成员保持精确类型
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

        get currentMap(): EngineSessionExtension["currentMap"] | undefined {
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

        get cameraController(): CameraController | null | undefined {
            return EngineState.session?.cameraController;
        },

        set cameraController(value: CameraController | null) {
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

        set currentMap(value: EngineSessionExtension["currentMap"]) {
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
}; var EngineRefs = _engineRefsFactory() as ReturnType<typeof _engineRefsFactory> & EngineRefsExtension & { [key: string]: any };

globalThis.EngineRefs = EngineRefs;

jtask.log("[EngineRefs] Module loaded (read-only shortcuts)");
