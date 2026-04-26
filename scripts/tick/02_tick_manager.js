
class TickScheduler {

    constructor() {

        this._ticksSimInt = 0;

        this._realTimeToTickThrough = 0;

        this._curSimSpeed = SimSpeed.Normal;

        this.prePauseSimSpeed = SimSpeed.Normal;

        this._tickBucketEvery = new TickBucket(TickRate.Every);
        this._tickBucketSparse = new TickBucket(TickRate.Sparse);
        this._tickBucketSlow = new TickBucket(TickRate.Slow);

        this._ticksThisFrame = 0;

        this._lastTickTimeMs = 0;

        this._maps = [];
    }

    static get MinAllowedFps() {
        return 22.0;
    }

    static get MaxFrameBudgetMs() {
        return 1000.0 / TickScheduler.MinAllowedFps;
    }

    get ticksSimulation() {
        return this._ticksSimInt;
    }

    get ticksAbsolute() {
        return this._ticksSimInt;
    }

    get tickSpeedFactor() {

        switch (this._curSimSpeed) {
            case SimSpeed.Paused:
                return 0;
            case SimSpeed.Normal:
                return 1;
            case SimSpeed.Fast:
                return 3;
            case SimSpeed.Superfast:

                if (this._maps.length === 0) {
                    return 18;
                }
                return 6;
            case SimSpeed.Ultrafast:

                if (this._maps.length === 0) {
                    return 150;
                }
                return 15;
            default:
                return -1;
        }
    }

    get ticksThisFrame() {
        return this._ticksThisFrame;
    }

    get _curTimePerTick() {
        const multiplier = this.tickSpeedFactor;
        if (multiplier === 0) {
            return 0;
        }

        return 1.0 / (60.0 * multiplier);
    }

    get paused() {
        return this._curSimSpeed === SimSpeed.Paused;
    }

    get curSimSpeed() {
        return this._curSimSpeed;
    }

    set curSimSpeed(value) {
        this._curSimSpeed = value;
    }

    get meanTickTime() {
        return this._lastTickTimeMs;
    }

    togglePaused() {

        if (this._curSimSpeed !== SimSpeed.Paused) {

            this.prePauseSimSpeed = this._curSimSpeed;
            this._curSimSpeed = SimSpeed.Paused;
        } else if (this.prePauseSimSpeed !== this._curSimSpeed) {

            this._curSimSpeed = this.prePauseSimSpeed;
        } else {

            this._curSimSpeed = SimSpeed.Normal;
        }
    }

    pause() {
        if (this._curSimSpeed !== SimSpeed.Paused) {
            this.togglePaused();
        }
    }

    registerAllTickabilityFor(tickable) {
        const tickList = this._tickListFor(tickable);
        if (tickList !== null) {
            tickList.register(tickable);
        }
    }

    deRegisterAllTickabilityFor(tickable) {
        const tickList = this._tickListFor(tickable);
        if (tickList !== null) {
            tickList.deregister(tickable);
        }
    }

    tickManagerUpdate(deltaTimeSeconds) {

        this._ticksThisFrame = 0;

        if (this.paused) {
            return;
        }

        const curTimePerTick = this._curTimePerTick;

        if (Math.abs(deltaTimeSeconds - curTimePerTick) < curTimePerTick * 0.1) {
            this._realTimeToTickThrough += curTimePerTick;
        } else {
            this._realTimeToTickThrough += deltaTimeSeconds;
        }

        const tickSpeedFactor = this.tickSpeedFactor;

        const startTime = RealTime.realtimeSinceStartupMs();

        while (this._realTimeToTickThrough > 0 &&
               this._ticksThisFrame < tickSpeedFactor * 2) {

            const tickStartTime = RealTime.realtimeSinceStartupMs();

            this.doSingleTick();

            const tickEndTime = RealTime.realtimeSinceStartupMs();

            this._realTimeToTickThrough -= curTimePerTick;

            this._ticksThisFrame++;

            this._lastTickTimeMs = tickEndTime - tickStartTime;

            const elapsedMs = RealTime.realtimeSinceStartupMs() - startTime;
            if (this.paused || elapsedMs > TickScheduler.MaxFrameBudgetMs) {
                break;
            }
        }

        if (this._realTimeToTickThrough > 0) {

            this._realTimeToTickThrough = 0;
        }

        if (globalThis.__BP_VERBOSE__ && this._ticksSimInt % 60 === 0 && this._ticksThisFrame > 0) {
            jtask.log(`[TickScheduler] ticksThisFrame=${this._ticksThisFrame}, multiplier=${tickSpeedFactor}, speed=${this._curSimSpeed}`);
        }
    }

    doSingleTick() {
        var profiling = globalThis.ScaffoldProfiler && ScaffoldProfiler._enabled;
        var t0, t1;

        for (let i = 0; i < this._maps.length; i++) {
            const map = this._maps[i];
            if (map.mapPreTick) {
                map.mapPreTick();
            }
        }

        this._ticksSimInt++;

        if (profiling) t0 = RealTime.realtimeSinceStartupUs();
        this._tickBucketEvery.tick(this._ticksSimInt);
        if (profiling) {
            t1 = RealTime.realtimeSinceStartupUs();
            var normalUs = t1 - t0;
            t0 = t1;
        }

        this._tickBucketSparse.tick(this._ticksSimInt);
        if (profiling) {
            t1 = RealTime.realtimeSinceStartupUs();
            var rareUs = t1 - t0;
            t0 = t1;
        }

        this._tickBucketSlow.tick(this._ticksSimInt);
        if (profiling) {
            t1 = RealTime.realtimeSinceStartupUs();
            var longUs = t1 - t0;

            var totalUs = normalUs + rareUs + longUs;
            if (totalUs > 5000) {
                jtask.log("[TickProfiler] tick#" + this._ticksSimInt +
                    " Every=" + (normalUs / 1000).toFixed(1) + "ms" +
                    " Sparse=" + (rareUs / 1000).toFixed(1) + "ms" +
                    " Slow=" + (longUs / 1000).toFixed(1) + "ms" +
                    " (items: E=" + this._tickBucketEvery._count() +
                    " S=" + this._tickBucketSparse._count() +
                    " L=" + this._tickBucketSlow._count() + ")");
            }
        }

        const preMapHooks = globalThis.__engine_pre_session_post_tick_hooks__;
        if (preMapHooks) {
            for (let h = 0; h < preMapHooks.length; h++) {
                try { preMapHooks[h](this._ticksSimInt); }
                catch (e) { jtask.log(`[TickScheduler] preSessionPostTick hook ${h} error: ${e}`); }
            }
        }

        for (let j = 0; j < this._maps.length; j++) {
            const map = this._maps[j];
            if (map.mapPostTick) {
                map.mapPostTick();
            }
        }

        const postHooks = globalThis.__engine_post_tick_hooks__;
        if (postHooks) {
            for (let h = 0; h < postHooks.length; h++) {
                try { postHooks[h](this._ticksSimInt); }
                catch (e) { jtask.log(`[TickScheduler] postTick hook ${h} error: ${e}`); }
            }
        }

        if (typeof GameTimer !== 'undefined') {
            GameTimer.tick(this._ticksSimInt);
        }
    }

    removeAllFromMap(map) {
        const predicate = (tickable) => tickable.map === map;
        this._tickBucketEvery.removeWhere(predicate);
        this._tickBucketSparse.removeWhere(predicate);
        this._tickBucketSlow.removeWhere(predicate);
    }

    debugSetTicksSimulation(newTicks) {
        this._ticksSimInt = newTicks;
    }

    reset() {
        this._ticksSimInt = 0;
        this._realTimeToTickThrough = 0;
        this._ticksThisFrame = 0;
        this._curSimSpeed = SimSpeed.Normal;
        this._tickBucketEvery.reset();
        this._tickBucketSparse.reset();
        this._tickBucketSlow.reset();
    }

    registerMap(map) {
        if (this._maps.indexOf(map) === -1) {
            this._maps.push(map);
        }
    }

    deregisterMap(map) {
        const idx = this._maps.indexOf(map);
        if (idx > -1) {
            this._maps.splice(idx, 1);
        }
    }

    _tickListFor(tickable) {

        if (tickable.holdsChildren) {
            return this._tickBucketEvery;
        }

        let tickRate = tickable.def?.tickRate;
        if (tickRate === undefined || tickRate === null) {
            tickRate = TickRate.None;
        }

        if (typeof tickRate === 'string') {
            tickRate = TickRate.fromString(tickRate);
        }
        switch (tickRate) {
            case TickRate.None:
                return null;
            case TickRate.Every:
                return this._tickBucketEvery;
            case TickRate.Sparse:
                return this._tickBucketSparse;
            case TickRate.Slow:
                return this._tickBucketSlow;
            default:
                return null;
        }
    }

    exposeData() {
        return {
            ticksSimulation: this._ticksSimInt
        };
    }

    loadData(data) {
        if (data.ticksSimulation !== undefined) {
            this._ticksSimInt = data.ticksSimulation;
        }
    }

    getStats() {
        return {
            ticksSimulation: this._ticksSimInt,
            ticksThisFrame: this._ticksThisFrame,
            curSimSpeed: SimSpeed.toString(this._curSimSpeed),
            tickSpeedFactor: this.tickSpeedFactor,
            paused: this.paused,
            lastTickTimeMs: this._lastTickTimeMs,
            mapsCount: this._maps.length,
            tickBucketEvery: this._tickBucketEvery.getStats(),
            tickBucketSparse: this._tickBucketSparse.getStats(),
            tickBucketSlow: this._tickBucketSlow.getStats()
        };
    }

    toString() {
        return `TickScheduler(ticks=${this._ticksSimInt}, speed=${SimSpeed.toString(this._curSimSpeed)})`;
    }
}

globalThis.TickScheduler = TickScheduler;

if (typeof RealTime !== 'undefined' && RealTime.setTickStateProvider) {
    RealTime.setTickStateProvider(function() {
        const tm = globalThis.EngineRefs?.tickManager;
        if (!tm) return null;
        return {
            paused: tm.paused,
            multiplier: tm.tickSpeedFactor
        };
    });
}

if (typeof TickClock !== 'undefined' && TickClock.setTickDataProvider) {
    TickClock.setTickDataProvider(function() {
        const tm = globalThis.EngineRefs?.tickManager;
        if (!tm) return null;
        return {
            ticksAbsolute: tm.ticksAbsolute,
            ticksSimulation: tm.ticksSimulation
        };
    });
}

if (typeof FrameStageCallbacks !== 'undefined' && typeof FrameStagePriority !== 'undefined') {
    FrameStageCallbacks.register(function(dt) {
        const tm = globalThis.EngineRefs?.tickManager;
        if (tm) {

            tm.tickManagerUpdate(dt / 1000);
        }
    }, FrameStagePriority.TICK, "TickScheduler.update");
}

if (globalThis.__BP_VERBOSE__) jtask.log("[tick_manager] TickScheduler class loaded");
