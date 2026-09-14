
// 阻塞任务：同步 action 直接执行；异步 action 返回迭代器，每帧推进一步，产出值可带进度文本/比例
type EngineBlockingYield = { status?: string; progress?: number } | null | undefined | void;
interface EngineBlockingEvent {
    action: () => Iterator<EngineBlockingYield> | void; textKey: string; doAsync: boolean;
    exceptionHandler: ((e: Error) => void) | null | undefined; callback: (() => void) | null | undefined;
}

const BlockingTaskQueue = {

    _eventQueue: [] as EngineBlockingEvent[],

    _currentEvent: null as EngineBlockingEvent | null,

    _currentIterator: null as Iterator<EngineBlockingYield> | void | null,

    _loadingText: "",
    _loadingProgress: -1,
    _dotsTimer: 0,
    _dots: "",

    // 阻塞段取证 (2026-09-11 黑闪案): 每段 begin/end 各一行日志, 帧号/阻塞帧数/毫秒/任务名;
    // history 留最近 16 段供消费者在屏上回显; blockedFrameHooks 在每个阻塞帧的进度 UI 之后调用
    _segStartFrame: -1,
    _segStartMs: 0,
    blockedFrames: 0,          // 当前段已被截断的帧数 (drawProgress 每阻塞帧 +1)
    history: [] as { name: string; blockedFrames: number; ms: number; endFrame: number; endMs: number; outcome: string }[],
    blockedFrameHooks: [] as (() => void)[],     // 消费者钩子 (阻塞帧标志等), 与 customProgressUI 无关
    get currentName() { return this._currentEvent ? this._currentEvent.textKey : null; },

    enqueueBlockingTask(action: EngineBlockingEvent["action"], textKey: string | null | undefined, doAsynchronously: boolean,
                        exceptionHandler?: EngineBlockingEvent["exceptionHandler"], callback?: EngineBlockingEvent["callback"]) {
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
            this._currentEvent = this._eventQueue.shift() as EngineBlockingEvent;  // 上行已判 length > 0
            this._loadingText = this._currentEvent.textKey;
            this._segStartFrame = RealTime.frameCount;
            this._segStartMs = Date.now();
            this.blockedFrames = 0;
            jtask.log("[BlockingTaskQueue] begin '" + this._currentEvent.textKey
                + "' frame=" + this._segStartFrame + " queued=" + this._eventQueue.length);

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
                // 慢步探针: 每帧恰一步, 单步耗时即该帧阻塞成本 — 超阈值打
                // 步耗时+任务名+上一步/本步 status, 长帧归因直接可读
                const _stepT0 = Number(RealTime.realtimeSinceStartupUs());
                const res = this._currentIterator.next();
                const _stepMs = (Number(RealTime.realtimeSinceStartupUs()) - _stepT0) / 1000;
                if (_stepMs > 150) {
                    jtask.log("[BlockingTaskQueue] slow step " + _stepMs.toFixed(0)
                        + "ms in '" + this._currentEvent.textKey
                        + "' prevStatus='" + (this._loadingText || "-")
                        + "' nextStatus='" + (res.value && res.value.status ? res.value.status : "-") + "'");
                }

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

    _recordSegmentEnd(outcome: string) {
        if (!this._currentEvent) return;
        const now = Date.now();
        const rec = { name: this._currentEvent.textKey, blockedFrames: this.blockedFrames,
            ms: now - this._segStartMs, endFrame: RealTime.frameCount, endMs: now, outcome };
        this.history.push(rec);
        if (this.history.length > 16) this.history.shift();
        jtask.log("[BlockingTaskQueue] end '" + rec.name + "' blockedFrames=" + rec.blockedFrames
            + " ms=" + rec.ms + " frame=" + rec.endFrame + " outcome=" + outcome);
    },

    _completeCurrentEvent() {
        this._recordSegmentEnd("done");
        jtask.log("[BlockingTaskQueue] Completing event: " + (this._currentEvent ? this._currentEvent.textKey : "null"));
        if (this._currentEvent && this._currentEvent.callback) {
            try {
                this._currentEvent.callback();
            } catch (e) {
                jtask.log.error("[BlockingTaskQueue] Callback error: " + (e as Error).message);
            }
        }
        this._currentEvent = null;
        this._currentIterator = null;
        this._dots = "";
        this._loadingText = "";
        this._loadingProgress = -1;
    },

    _handleException(e: unknown) {
        this._recordSegmentEnd("error");
        jtask.log.error(`[BlockingTaskQueue] Error in ${this._currentEvent?.textKey}: ${(e as Error).message}\n${(e as Error).stack}`);
        if (this._currentEvent && this._currentEvent.exceptionHandler) {
            this._currentEvent.exceptionHandler(e as Error);
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

    // 消费者可接管阻塞期 UI: 置为函数后, 阻塞帧不再画下面的默认灰框,
    // 由该函数全权负责本帧的加载期画面 (帧链在 BLOCKING_TASK_CHECK 截断,
    // 这是阻塞期间唯一还在跑的 UI 出口)。
    customProgressUI: null as (() => void) | null,

    drawProgress() {
        if (!this.isBlocking) return;

        this.blockedFrames++;
        if (typeof this.customProgressUI === 'function') {
            this.customProgressUI();
            this._runBlockedFrameHooks();
            return;
        }

        draw.rect(0, 0, 480, 270, {
            col: [0.15, 0.15, 0.15, 1.0],
            z_layer: draw.ZLAYER_UI
        });

        const fullText = (this._loadingText || "Loading") + this._dots;
        draw.text(240, 135, fullText, {
            pivot: draw.PIVOT_CENTER,
            size: 24,
            col: [0.9, 0.9, 0.9, 1.0],
            z_layer: draw.ZLAYER_UI,
            outline: 2.0,
            outline_col: [0.0, 0.0, 0.0, 1.0]
        });

        if (typeof this._loadingProgress === 'number' && this._loadingProgress > 0) {
            const barW = 200, barH = 8;
            const barX = 240 - barW / 2, barY = 155;

            draw.rect(barX, barY, barW, barH, {
                col: [0.3, 0.3, 0.3, 1.0],
                z_layer: draw.ZLAYER_UI
            });

            draw.rect(barX, barY, barW * this._loadingProgress, barH, {
                col: [0.2, 0.7, 0.3, 1.0],
                z_layer: draw.ZLAYER_UI
            });
        }
        this._runBlockedFrameHooks();
    },

    _runBlockedFrameHooks() {
        for (const h of this.blockedFrameHooks) {
            try { h(); } catch (e) { jtask.log("[BlockingTaskQueue] blockedFrameHook error: " + (e as Error).message); }
        }
    }
};

globalThis.BlockingTaskQueue = BlockingTaskQueue;

if (globalThis.__BP_VERBOSE__) {
    jtask.log("[BlockingTaskQueue] Initialized. Ready to queue events.");
}
