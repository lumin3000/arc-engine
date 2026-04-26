
const BlockingTaskQueue = {

    _eventQueue: [],

    _currentEvent: null,

    _currentIterator: null,

    _loadingText: "",
    _loadingProgress: -1,
    _dotsTimer: 0,
    _dots: "",

    enqueueBlockingTask(action, textKey, doAsynchronously, exceptionHandler, callback) {
        this._eventQueue.push({
            action: action,
            textKey: textKey || "Loading...",
            doAsync: doAsynchronously,
            exceptionHandler: exceptionHandler,
            callback: callback
        });
    },

    tick() {

        if (!this._currentEvent && this._eventQueue.length > 0) {
            this._currentEvent = this._eventQueue.shift();
            this._loadingText = this._currentEvent.textKey;

            if (this._currentEvent.doAsync) {
                try {

                    this._currentIterator = this._currentEvent.action();
                    if (!this._currentIterator || typeof this._currentIterator.next !== 'function') {

                        if (typeof this._currentIterator === 'undefined') {

                            this._completeCurrentEvent();
                            return;
                        }
                    }
                } catch (e) {
                    this._handleException(e);
                }
            } else {

                try {
                    this._currentEvent.action();
                    this._completeCurrentEvent();
                } catch (e) {
                    this._handleException(e);
                }
            }
        }

        if (this._currentEvent && this._currentEvent.doAsync && this._currentIterator) {
            try {

                const res = this._currentIterator.next();

                if (res.value && typeof res.value === 'object') {
                    if (res.value.status) this._loadingText = res.value.status;
                    if (typeof res.value.progress === 'number') this._loadingProgress = res.value.progress;
                }
                if (res.done) {
                    this._completeCurrentEvent();
                }
            } catch (e) {
                this._handleException(e);
            }
        }

        if (this._currentEvent) {
            this._dotsTimer++;
            if (this._dotsTimer > 20) {
                this._dotsTimer = 0;
                if (this._dots.length >= 3) this._dots = "";
                else this._dots += ".";
            }
        }
    },

    _completeCurrentEvent() {
        jtask.log("[BlockingTaskQueue] Completing event: " + (this._currentEvent ? this._currentEvent.textKey : "null"));
        if (this._currentEvent && this._currentEvent.callback) {
            try {
                this._currentEvent.callback();
            } catch (e) {
                jtask.log.error("[BlockingTaskQueue] Callback error: " + e.message);
            }
        }
        this._currentEvent = null;
        this._currentIterator = null;
        this._dots = "";
        this._loadingText = "";
        this._loadingProgress = -1;
    },

    _handleException(e) {
        jtask.log.error(`[BlockingTaskQueue] Error in ${this._currentEvent?.textKey}: ${e.message}\n${e.stack}`);
        if (this._currentEvent && this._currentEvent.exceptionHandler) {
            this._currentEvent.exceptionHandler(e);
        }
        this._currentEvent = null;
        this._currentIterator = null;
    },

    get isBlocking() {
        const waiting = this._currentEvent !== null || this._eventQueue.length > 0;
        if (waiting && globalThis.__BP_VERBOSE__ && RealTime.frameCount % 60 === 0) {

            jtask.log("[BlockingTaskQueue] Blocking: Event=" + (this._currentEvent ? this._currentEvent.textKey : "Vacant") + " Q=" + this._eventQueue.length);
        }
        return waiting;
    },

    drawProgress() {
        if (!this.isBlocking) return;

        draw.rect(0, 0, 480, 270, {
            col: [0.15, 0.15, 0.15, 1.0],
            z_layer: draw.ZLAYER_GUI_TOP + 100
        });

        const fullText = (this._loadingText || "Loading") + this._dots;
        draw.text(240, 135, fullText, {
            pivot: draw.PIVOT_CENTER,
            size: 24,
            col: [0.9, 0.9, 0.9, 1.0],
            z_layer: draw.ZLAYER_GUI_TOP + 101,
            outline: 2.0,
            outline_col: [0.0, 0.0, 0.0, 1.0]
        });

        if (typeof this._loadingProgress === 'number' && this._loadingProgress > 0) {
            const barW = 200, barH = 8;
            const barX = 240 - barW / 2, barY = 155;

            draw.rect(barX, barY, barW, barH, {
                col: [0.3, 0.3, 0.3, 1.0],
                z_layer: draw.ZLAYER_GUI_TOP + 101
            });

            draw.rect(barX, barY, barW * this._loadingProgress, barH, {
                col: [0.2, 0.7, 0.3, 1.0],
                z_layer: draw.ZLAYER_GUI_TOP + 102
            });
        }
    }
};

globalThis.BlockingTaskQueue = BlockingTaskQueue;

if (globalThis.__BP_VERBOSE__) {
    jtask.log("[BlockingTaskQueue] Initialized. Ready to queue events.");
}
