
class CameraController {
    constructor() {
        this._cachedRect = null;
        this._lastTick = -1;
    }

    get currentViewRect() {

        const tileSize = 1.0;

        const bounds = coord.get_view_bounds(tileSize);

        return {
            minX: bounds.min_x,
            minZ: bounds.min_z,
            maxX: bounds.max_x,
            maxZ: bounds.max_z,

            *[Symbol.iterator]() {
                for (let z = this.minZ; z <= this.maxZ; z++) {
                    for (let x = this.minX; x <= this.maxX; x++) {
                        yield { x, z };
                    }
                }
            }
        };
    }

    update() {

    }
}

globalThis.CameraController = CameraController;

EngineBootstrap.onPhase(EngineBootstrap.Phase.GAME_STARTED, function() {
    if (!EngineRefs.cameraController) {
        EngineRefs.cameraController = new CameraController();
    }
});
