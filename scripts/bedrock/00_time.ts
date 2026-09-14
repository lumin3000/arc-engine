
// 时间源：C 每帧传入 dt；暂停/倍速与模拟刻由消费者（tick 调度）经 provider 回调提供，引擎不持有调度对象
type EngineTickStateProvider = () => { paused: boolean; multiplier: number } | null;
type EngineTickDataProvider = () => { ticksAbsolute: number; ticksSimulation: number } | null;
interface EngineGameTimer { id: number; startTick: number; durationTicks: number; callback: () => void; repeat: boolean; interval: number; }

const RealTime = {

    deltaTime: 0,

    realDeltaTime: 0,

    frameCount: 0,

    _lastRealTime: 0,

    _unpausedTime: 0,

    _tickStateProvider: null as EngineTickStateProvider | null,

    realtimeSinceStartup() {
        return jtask.counter_us() / 1_000_000;
    },

    realtimeSinceStartupMs() {
        return jtask.counter_us() / 1000;
    },

    realtimeSinceStartupUs() {
        return jtask.counter_us();
    },

    setTickStateProvider(provider: EngineTickStateProvider) {
        this._tickStateProvider = provider;
    },

    update(deltaTFromC: number) {
        this.frameCount++;
        this.deltaTime = deltaTFromC;

        const now = this.realtimeSinceStartup();
        this.realDeltaTime = now - this._lastRealTime;
        this._lastRealTime = now;

        if (this._tickStateProvider) {
            const state = this._tickStateProvider();
            if (state && !state.paused) {
                this._unpausedTime += this.deltaTime * (state.multiplier ?? 1);
            }
        }
    },

    get lastRealTime() {
        return this._lastRealTime;
    },

    get unpausedRealTime() {
        return this._unpausedTime;
    }
};

const _TickClockLiteral = {  // 消费者成员经 TickClockExtension 声明（消费者 boundary_contracts extensions 生成），引擎成员保持精确类型

    _tickDataProvider: null as EngineTickDataProvider | null,

    setTickDataProvider(provider: EngineTickDataProvider) {
        this._tickDataProvider = provider;
    },

    TICKS_PER_REAL_SECOND: 60,

    TICK_RARE_INTERVAL: 250,

    TICK_LONG_INTERVAL: 2000,

    SECONDS_PER_TICK: 1 / 60,

    get ticksAbsolute() {
        if (this._tickDataProvider) {
            const data = this._tickDataProvider();
            return data?.ticksAbsolute ?? 0;
        }
        return 0;
    },

    get ticksSimulation() {
        if (this._tickDataProvider) {
            const data = this._tickDataProvider();
            return data?.ticksSimulation ?? 0;
        }
        return 0;
    },

    ticksToSeconds(numTicks: number) {
        return numTicks / this.TICKS_PER_REAL_SECOND;
    },

    secondsToTicks(numSeconds: number) {
        return Math.round(numSeconds * this.TICKS_PER_REAL_SECOND);
    },

    toStringSecondsFromTicks(numTicks: number) {
        return this.ticksToSeconds(numTicks).toFixed(1) + "s";
    },

    isTickInterval(period: number) {
        return this.ticksSimulation % period === 0;
    },

    isTickIntervalWithOffset(offset: number, period: number) {
        return (this.ticksSimulation + offset) % period === 0;
    },

    getTickIntervalOffset(index: number, count: number, period: number) {
        return Math.ceil((period / count) * index) % period;
    }
}; const TickClock = _TickClockLiteral as typeof _TickClockLiteral & TickClockExtension;

const GameTimer = {

    _nextId: 1,

    _timers: [] as EngineGameTimer[],

    setTimeout(callback: () => void, delayTicks: number) {
        const id = this._nextId++;
        const currentTick = TickClock.ticksSimulation;

        this._timers.push({
            id: id,
            startTick: currentTick,
            durationTicks: delayTicks,
            callback: callback,
            repeat: false,
            interval: 0
        });

        return id;
    },

    clearTimeout(id: number) {
        const idx = this._timers.findIndex(t => t.id === id);
        if (idx !== -1) {
            this._timers.splice(idx, 1);
        }
    },

    setInterval(callback: () => void, intervalTicks: number) {
        const id = this._nextId++;
        const currentTick = TickClock.ticksSimulation;

        this._timers.push({
            id: id,
            startTick: currentTick,
            durationTicks: intervalTicks,
            callback: callback,
            repeat: true,
            interval: intervalTicks
        });

        return id;
    },

    clearInterval(id: number) {
        this.clearTimeout(id);
    },

    setTimeoutSeconds(callback: () => void, delaySeconds: number) {
        return this.setTimeout(callback, TickClock.secondsToTicks(delaySeconds));
    },

    setIntervalSeconds(callback: () => void, intervalSeconds: number) {
        return this.setInterval(callback, TickClock.secondsToTicks(intervalSeconds));
    },

    tick(currentTick: number) {

        for (let i = this._timers.length - 1; i >= 0; i--) {
            const timer = this._timers[i];
            const elapsed = currentTick - timer.startTick;

            if (elapsed >= timer.durationTicks) {

                try {
                    timer.callback();
                } catch (e) {
                    jtask.log.error(`[GameTimer] Callback error: ${(e as Error).message}`);
                }

                if (timer.repeat) {

                    timer.startTick = currentTick;
                } else {

                    this._timers.splice(i, 1);
                }
            }
        }
    },

    clearAll() {
        this._timers.length = 0;
    },

    get activeCount() {
        return this._timers.length;
    }
};

globalThis.RealTime = RealTime;
globalThis.TickClock = TickClock;
globalThis.GameTimer = GameTimer;

if (globalThis.__BP_VERBOSE__) {
    jtask.log("[time] RealTime, TickClock, GameTimer loaded");
}
