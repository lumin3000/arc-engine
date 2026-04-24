
class TickManager {

    constructor() {

        this._ticksGameInt = 0;

        this.gameStartAbsTick = 1;

        this._realTimeToTickThrough = 0;

        this._curTimeSpeed = TimeSpeed.Normal;

        this.prePauseTimeSpeed = TimeSpeed.Normal;

        this._startingYearInt = 5500;

        this._lastSettleTicksInt = 0;

        this._tickListNormal = new TickList(TickerType.Normal);
        this._tickListRare = new TickList(TickerType.Rare);
        this._tickListLong = new TickList(TickerType.Long);

        this._ticksThisScaffold = 0;

        this._lastNothingHappeningCheckTick = -1;

        this._nothingHappeningCached = false;

        this._lastTickTimeMs = 0;

        this._maps = [];

        this._forcedNormalSpeed = false;
        this._forcedNormalSpeedUntil = 0;
    }

    static get WorstAllowedFPS() {
        return 22.0;
    }

    static get MaxScaffoldTimeMs() {
        return 1000.0 / TickManager.WorstAllowedFPS;
    }

    get ticksGame() {
        return this._ticksGameInt;
    }

    get ticksAbs() {

        if (this.gameStartAbsTick === 0) {
            jtask.log.error("Accessing TicksAbs but gameStartAbsTick is not set yet");
            return this._ticksGameInt;
        }

        return this._ticksGameInt + this.gameStartAbsTick;
    }

    get ticksSinceSettle() {
        return this._ticksGameInt - this._lastSettleTicksInt;
    }

    get settleTick() {
        return this._lastSettleTicksInt;
    }

    get startingYear() {
        return this._startingYearInt;
    }

    get tickRateMultiplier() {

        if (this._isForcedNormalSpeed()) {
            if (this._curTimeSpeed === TimeSpeed.Paused) {
                return 0;
            }
            return 1;
        }

        switch (this._curTimeSpeed) {
            case TimeSpeed.Paused:
                return 0;
            case TimeSpeed.Normal:
                return 1;
            case TimeSpeed.Fast:
                return 3;
            case TimeSpeed.Superfast:

                if (this._maps.length === 0) {
                    return 18;
                }
                if (this._nothingHappeningInGame()) {
                    return 12;
                }
                return 6;
            case TimeSpeed.Ultrafast:

                if (this._maps.length === 0) {
                    return 150;
                }
                return 15;
            default:
                return -1;
        }
    }

    get ticksThisScaffold() {
        return this._ticksThisScaffold;
    }

    get _curTimePerTick() {
        const multiplier = this.tickRateMultiplier;
        if (multiplier === 0) {
            return 0;
        }

        return 1.0 / (60.0 * multiplier);
    }

    get paused() {
        if (this._curTimeSpeed !== TimeSpeed.Paused) {
            return this._forcePaused;
        }
        return true;
    }

    get _forcePaused() {

        return false;
    }

    get curTimeSpeed() {
        return this._curTimeSpeed;
    }

    set curTimeSpeed(value) {
        this._curTimeSpeed = value;
    }

    get hasSettledNewColony() {
        return this._lastSettleTicksInt > 0;
    }

    get meanTickTime() {
        return this._lastTickTimeMs;
    }

    togglePaused() {

        if (this._curTimeSpeed !== TimeSpeed.Paused) {

            this.prePauseTimeSpeed = this._curTimeSpeed;
            this._curTimeSpeed = TimeSpeed.Paused;
        } else if (this.prePauseTimeSpeed !== this._curTimeSpeed) {

            this._curTimeSpeed = this.prePauseTimeSpeed;
        } else {

            this._curTimeSpeed = TimeSpeed.Normal;
        }
    }

    pause() {
        if (this._curTimeSpeed !== TimeSpeed.Paused) {
            this.togglePaused();
        }
    }

    registerAllTickabilityFor(thing) {
        const tickList = this._tickListFor(thing);
        if (tickList !== null) {
            tickList.registerMono(thing);
        }
    }

    deRegisterAllTickabilityFor(thing) {
        const tickList = this._tickListFor(thing);
        if (tickList !== null) {
            tickList.deregisterMono(thing);
        }
    }

    tickManagerUpdate(deltaTimeSeconds) {

        this._ticksThisScaffold = 0;

        if (this.paused) {
            return;
        }

        const curTimePerTick = this._curTimePerTick;

        if (Math.abs(deltaTimeSeconds - curTimePerTick) < curTimePerTick * 0.1) {
            this._realTimeToTickThrough += curTimePerTick;
        } else {
            this._realTimeToTickThrough += deltaTimeSeconds;
        }

        const tickRateMultiplier = this.tickRateMultiplier;

        const startTime = RealTime.realtimeSinceStartupMs();

        while (this._realTimeToTickThrough > 0 &&
               this._ticksThisScaffold < tickRateMultiplier * 2) {

            const tickStartTime = RealTime.realtimeSinceStartupMs();

            this.doSingleTick();

            const tickEndTime = RealTime.realtimeSinceStartupMs();

            this._realTimeToTickThrough -= curTimePerTick;

            this._ticksThisScaffold++;

            this._lastTickTimeMs = tickEndTime - tickStartTime;

            const elapsedMs = RealTime.realtimeSinceStartupMs() - startTime;
            if (this.paused || elapsedMs > TickManager.MaxScaffoldTimeMs) {
                break;
            }
        }

        if (this._realTimeToTickThrough > 0) {

            this._realTimeToTickThrough = 0;
        }

        if (globalThis.__BP_VERBOSE__ && this._ticksGameInt % 60 === 0 && this._ticksThisScaffold > 0) {
            jtask.log(`[TickManager] ticksThisScaffold=${this._ticksThisScaffold}, multiplier=${tickRateMultiplier}, speed=${this._curTimeSpeed}`);
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

        this._ticksGameInt++;

        if (profiling) t0 = RealTime.realtimeSinceStartupUs();
        this._tickListNormal.tick(this._ticksGameInt);
        if (profiling) {
            t1 = RealTime.realtimeSinceStartupUs();
            var normalUs = t1 - t0;
            t0 = t1;
        }

        this._tickListRare.tick(this._ticksGameInt);
        if (profiling) {
            t1 = RealTime.realtimeSinceStartupUs();
            var rareUs = t1 - t0;
            t0 = t1;
        }

        this._tickListLong.tick(this._ticksGameInt);
        if (profiling) {
            t1 = RealTime.realtimeSinceStartupUs();
            var longUs = t1 - t0;

            var totalUs = normalUs + rareUs + longUs;
            if (totalUs > 5000) {
                jtask.log("[TickProfiler] tick#" + this._ticksGameInt +
                    " Normal=" + (normalUs / 1000).toFixed(1) + "ms" +
                    " Rare=" + (rareUs / 1000).toFixed(1) + "ms" +
                    " Long=" + (longUs / 1000).toFixed(1) + "ms" +
                    " (items: N=" + this._tickListNormal._thingCount() +
                    " R=" + this._tickListRare._thingCount() +
                    " L=" + this._tickListLong._thingCount() + ")");
            }
        }

        try { globalThis.Find?.dateNotifier?.dateNotifierTick?.(); } catch (e) { jtask.log(`[TickManager] DateNotifier error: ${e}`); }
        try { globalThis.Find?.scenario?.tickPrologue?.(); } catch (e) { jtask.log(`[TickManager] Prologue error: ${e}`); }
        try { globalThis.Find?.world?.worldTick?.(); } catch (e) { jtask.log(`[TickManager] World error: ${e}`); }
        try { globalThis.Find?.storyWatcher?.storyWatcherTick?.(); } catch (e) { jtask.log(`[TickManager] StoryWatcher error: ${e}`); }
        try { globalThis.Find?.gameEnder?.gameEndTick?.(); } catch (e) { jtask.log(`[TickManager] GameEnder error: ${e}`); }
        try { globalThis.Find?.storyteller?.storytellerTick?.(); } catch (e) { jtask.log(`[TickManager] Storyteller error: ${e}`); }

        try { globalThis.RaidTrigger?.tick?.(); } catch (e) { jtask.log(`[TickManager] RaidTrigger error: ${e}`); }
        try { globalThis.Find?.taleManager?.taleManagerTick?.(); } catch (e) { jtask.log(`[TickManager] TaleManager error: ${e}`); }
        try { globalThis.Find?.questManager?.questManagerTick?.(); } catch (e) { jtask.log(`[TickManager] QuestManager error: ${e}`); }
        try { globalThis.Find?.world?.worldPostTick?.(); } catch (e) { jtask.log(`[TickManager] WorldPostTick error: ${e}`); }

        for (let j = 0; j < this._maps.length; j++) {
            const map = this._maps[j];
            if (map.mapPostTick) {
                map.mapPostTick();
            }
        }

        try { globalThis.Find?.history?.historyTick?.(); } catch (e) { jtask.log(`[TickManager] History error: ${e}`); }
        try { globalThis.GameComponentUtility?.gameComponentTick?.(); } catch (e) { jtask.log(`[TickManager] GameComponent error: ${e}`); }
        try { globalThis.Find?.letterStack?.letterStackTick?.(); } catch (e) { jtask.log(`[TickManager] LetterStack error: ${e}`); }
        try { globalThis.Find?.autosaver?.autosaverTick?.(); } catch (e) { jtask.log(`[TickManager] Autosaver error: ${e}`); }
        try { globalThis.GrimeMonitor?.filthMonitorTick?.(); } catch (e) { jtask.log(`[TickManager] FilthMonitor error: ${e}`); }
        try { globalThis.Find?.transportShipManager?.shipObjectsTick?.(); } catch (e) { jtask.log(`[TickManager] TransportShipManager error: ${e}`); }

        if (typeof GameTimer !== 'undefined') {
            GameTimer.tick(this._ticksGameInt);
        }
    }

    removeAllFromMap(map) {
        const predicate = (thing) => thing.map === map;
        this._tickListNormal.removeWhere(predicate);
        this._tickListRare.removeWhere(predicate);
        this._tickListLong.removeWhere(predicate);
    }

    debugSetTicksGame(newTicksGame) {
        this._ticksGameInt = newTicksGame;
    }

    notify_GeneratedPotentiallyHostileMap() {
        this.pause();
        this._signalForceNormalSpeedShort();
    }

    resetSettlementTicks() {
        this._lastSettleTicksInt = this._ticksGameInt;
    }

    reset() {
        this._ticksGameInt = 0;
        this._realTimeToTickThrough = 0;
        this._ticksThisScaffold = 0;
        this._curTimeSpeed = TimeSpeed.Normal;
        this._tickListNormal.reset();
        this._tickListRare.reset();
        this._tickListLong.reset();
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

    _tickListFor(thing) {

        if (thing.isMonoHolder) {
            return this._tickListNormal;
        }

        let tickerType = thing.def?.tickerType;
        if (tickerType === undefined || tickerType === null) {
            tickerType = TickerType.Never;
        }

        if (typeof tickerType === 'string') {
            tickerType = TickerType.fromString(tickerType);
        }
        switch (tickerType) {
            case TickerType.Never:
                return null;
            case TickerType.Normal:
                return this._tickListNormal;
            case TickerType.Rare:
                return this._tickListRare;
            case TickerType.Long:
                return this._tickListLong;
            default:
                return null;
        }
    }

    _nothingHappeningInGame() {

        if (this._lastNothingHappeningCheckTick === this._ticksGameInt) {
            return this._nothingHappeningCached;
        }

        this._nothingHappeningCached = false;
        this._lastNothingHappeningCheckTick = this._ticksGameInt;

        return this._nothingHappeningCached;
    }

    _isForcedNormalSpeed() {
        if (this._forcedNormalSpeedUntil > this._ticksGameInt) {
            return true;
        }
        return this._forcedNormalSpeed;
    }

    _signalForceNormalSpeedShort() {

        this._forcedNormalSpeedUntil = this._ticksGameInt + 600;
    }

    exposeData() {
        return {
            ticksGame: this._ticksGameInt,
            gameStartAbsTick: this.gameStartAbsTick,
            startingYear: this._startingYearInt,
            lastSettleTicks: this._lastSettleTicksInt
        };
    }

    loadData(data) {
        if (data.ticksGame !== undefined) {
            this._ticksGameInt = data.ticksGame;
        }
        if (data.gameStartAbsTick !== undefined) {
            this.gameStartAbsTick = data.gameStartAbsTick;
        }
        if (data.startingYear !== undefined) {
            this._startingYearInt = data.startingYear;
        }
        if (data.lastSettleTicks !== undefined) {
            this._lastSettleTicksInt = data.lastSettleTicks;
        }
    }

    getStats() {
        return {
            ticksGame: this._ticksGameInt,
            ticksThisScaffold: this._ticksThisScaffold,
            curTimeSpeed: TimeSpeed.toString(this._curTimeSpeed),
            tickRateMultiplier: this.tickRateMultiplier,
            paused: this.paused,
            lastTickTimeMs: this._lastTickTimeMs,
            mapsCount: this._maps.length,
            tickListNormal: this._tickListNormal.getStats(),
            tickListRare: this._tickListRare.getStats(),
            tickListLong: this._tickListLong.getStats()
        };
    }

    toString() {
        return `TickManager(ticks=${this._ticksGameInt}, speed=${TimeSpeed.toString(this._curTimeSpeed)})`;
    }
}

globalThis.TickManager = TickManager;

if (typeof RealTime !== 'undefined' && RealTime.setTickStateProvider) {
    RealTime.setTickStateProvider(function() {
        const tm = globalThis.Find?.tickManager;
        if (!tm) return null;
        return {
            paused: tm.paused,
            multiplier: tm.tickRateMultiplier
        };
    });
}

if (typeof GenTicks !== 'undefined' && GenTicks.setTickDataProvider) {
    GenTicks.setTickDataProvider(function() {
        const tm = globalThis.Find?.tickManager;
        if (!tm) return null;
        return {
            ticksAbs: tm.ticksAbs,
            ticksGame: tm.ticksGame
        };
    });
}

if (typeof GenTicks !== 'undefined' && GenTicks.setCameraDataProvider) {
    GenTicks.setCameraDataProvider(function() {
        return {
            currentMap: globalThis.Find?.currentMap,
            cameraDriver: globalThis.Find?.cameraDriver
        };
    });
}

if (typeof ScaffoldCallbacks !== 'undefined' && typeof ScaffoldPriority !== 'undefined') {
    ScaffoldCallbacks.register(function(dt) {
        const tm = globalThis.Find?.tickManager;
        if (tm) {

            tm.tickManagerUpdate(dt / 1000);
        }
    }, ScaffoldPriority.TICK, "TickManager.update");
}

if (globalThis.__BP_VERBOSE__) jtask.log("[tick_manager] TickManager class loaded");
