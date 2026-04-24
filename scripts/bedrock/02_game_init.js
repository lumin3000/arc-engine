
const GameInit = {

    Phase: {
        CLASSES_DEFINED: 1,
        DATA_LOADED: 2,
        MAP_READY: 3,
        GAME_STARTED: 4
    },

    _currentPhase: 0,
    _callbacks: {},
    _phaseNames: {
        1: 'CLASSES_DEFINED',
        2: 'DATA_LOADED',
        3: 'MAP_READY',
        4: 'GAME_STARTED'
    },

    onPhase(phase, callback) {
        if (typeof callback !== 'function') {
            jtask.log("[GameInit] ERROR: callback must be a function");
            return;
        }

        if (this._currentPhase >= phase) {

            try {
                callback();
            } catch (e) {
                jtask.log("[GameInit] Callback error: " + e.message + "\n" + (e.stack || ''));
            }
        } else {

            if (!this._callbacks[phase]) {
                this._callbacks[phase] = [];
            }
            this._callbacks[phase].push(callback);
        }
    },

    triggerPhase(phase) {
        if (phase <= this._currentPhase) {
            jtask.log("[GameInit] WARNING: Phase " + this._phaseNames[phase] + " already triggered");
            return;
        }

        this._currentPhase = phase;
        jtask.log("[GameInit] Phase: " + this._phaseNames[phase]);

        const callbacks = this._callbacks[phase] || [];
        for (const cb of callbacks) {
            try {
                cb();
            } catch (e) {
                jtask.log("[GameInit] Callback error in " + this._phaseNames[phase] + ": " + e.message + "\n" + (e.stack || ''));
            }
        }

        delete this._callbacks[phase];
    },

    get currentPhase() {
        return this._currentPhase;
    },

    isPhaseReached(phase) {
        return this._currentPhase >= phase;
    }
};

globalThis.GameInit = GameInit;

if (globalThis.__BP_VERBOSE__) {
    jtask.log("[GameInit] Module loaded");
}
