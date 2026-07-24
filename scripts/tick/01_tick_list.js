
const TickRate = Object.freeze({
    None: 0, Every: 1, Sparse: 2, Slow: 3,
    toString(v) { return ["None","Every","Sparse","Slow"][v] || "Unknown"; },
    fromString(s) { return {None:0,Every:1,Sparse:2,Slow:3}[s] ?? 0; }
});
const SimSpeed = Object.freeze({
    Paused: 0, Normal: 1, Fast: 2, Superfast: 3, Ultrafast: 4,
    toString(v) { return ["Paused","Normal","Fast","Superfast","Ultrafast"][v] || "Unknown"; },
    fromString(s) { return {Paused:0,Normal:1,Fast:2,Superfast:3,Ultrafast:4}[s] ?? 0; }
});
globalThis.TickRate = TickRate;
globalThis.SimSpeed = SimSpeed;

class TickBucket {

    constructor(tickType) {

        this._tickType = tickType;

        this._buckets = [];

        this._toRegister = [];

        this._toDeregister = [];

        const interval = this._tickInterval;
        for (let i = 0; i < interval; i++) {
            this._buckets.push([]);
        }
    }

    get _tickInterval() {
        switch (this._tickType) {
            case TickRate.Every:
                return 1;
            case TickRate.Sparse:
                return 250;
            case TickRate.Slow:
                return 2000;
            default:
                return -1;
        }
    }

    reset() {

        for (let i = 0; i < this._buckets.length; i++) {
            this._buckets[i].length = 0;
        }

        this._toRegister.length = 0;
        this._toDeregister.length = 0;
    }

    removeWhere(predicate) {
        for (const _ of this.removeWhereAsync(predicate)) { /* drain */ }
    }

    // 分帧版: 大批清除 (整图退役) 在阻塞过场内逐段让帧, 同步 removeWhere =
    // 原地 drain 同路径。桶间让帧, 单桶内不让 (桶均摊小)。
    *removeWhereAsync(predicate) {
        const BUCKET_BATCH = 64;
        for (let i = 0; i < this._buckets.length; i++) {
            const list = this._buckets[i];

            for (let j = list.length - 1; j >= 0; j--) {
                if (predicate(list[j])) {
                    list.splice(j, 1);
                }
            }
            if ((i + 1) % BUCKET_BATCH === 0) yield;
        }

        for (let j = this._toRegister.length - 1; j >= 0; j--) {
            if (predicate(this._toRegister[j])) {
                this._toRegister.splice(j, 1);
            }
        }

        for (let j = this._toDeregister.length - 1; j >= 0; j--) {
            if (predicate(this._toDeregister[j])) {
                this._toDeregister.splice(j, 1);
            }
        }
    }

    // 挂起队列只为 tick 迭代期防列表突变而设; 非 tick 期 (阻塞过场批量
    // spawn/deSpawn) 直接进出桶 — 否则恢复后首个 tick 一口气 flush 数十万
    // 挂起项 (768² 实测 380ms 单帧)。语义等价: 桶成员在下一 tick 前就位。
    register(tickable) {
        if (this._ticking) {
            this._toRegister.push(tickable);
            return;
        }
        this._bucketOf(tickable).push(tickable);
    }

    deregister(tickable) {
        if (this._ticking) {
            this._toDeregister.push(tickable);
            return;
        }
        const bucket = this._bucketOf(tickable);
        const idx = bucket.indexOf(tickable);
        if (idx > -1) bucket.splice(idx, 1);
    }

    tick(ticksSimulation) {

        for (let i = 0; i < this._toRegister.length; i++) {
            const tickable = this._toRegister[i];
            this._bucketOf(tickable).push(tickable);
        }
        this._toRegister.length = 0;

        for (let j = 0; j < this._toDeregister.length; j++) {
            const tickable = this._toDeregister[j];
            const bucket = this._bucketOf(tickable);
            const idx = bucket.indexOf(tickable);
            if (idx > -1) {
                bucket.splice(idx, 1);
            }
        }
        this._toDeregister.length = 0;

        const currentBucket = this._buckets[ticksSimulation % this._tickInterval];

        this._ticking = true; // 迭代期突变走挂起队列 (回调内 spawn/deSpawn)
        try {
            for (let m = 0; m < currentBucket.length; m++) {
                const tickable = currentBucket[m];

                if (tickable.destroyed) {
                    continue;
                }

                try {
                    // 入口=doTick 调度器 (对齐 RW TickList→Thing.DoTick), 无则回落
                    // 裸 tick (非 Thing 系 tickable 兼容)
                    if (tickable.doTick) tickable.doTick();
                    else tickable.tick();
                } catch (e) {

                    const pos = tickable.spawned ? ` (at ${tickable.position?.x ?? '?'},${tickable.position?.z ?? '?'})` : "";
                    const label = tickable.toStringSafe?.() ?? tickable.toString?.() ?? "Tickable";
                    jtask.log.error(`Exception ticking ${label}${pos}: ${e}\n${e.stack || ''}`);
                }
            }
        } finally {
            this._ticking = false;
        }
    }

    _bucketOf(tickable) {

        let num = tickable.getHashCode();

        if (num < 0) {
            num = -num;
        }

        const index = num % this._tickInterval;

        return this._buckets[index];
    }

    _count() {
        var n = 0;
        for (var i = 0; i < this._buckets.length; i++) n += this._buckets[i].length;
        return n;
    }

    getStats() {
        let total = 0;
        let nonEmptyBuckets = 0;
        let maxBucketSize = 0;

        for (const bucket of this._buckets) {
            total += bucket.length;
            if (bucket.length > 0) {
                nonEmptyBuckets++;
            }
            if (bucket.length > maxBucketSize) {
                maxBucketSize = bucket.length;
            }
        }

        return {
            tickType: this._tickType,
            tickInterval: this._tickInterval,
            totalBuckets: this._buckets.length,
            nonEmptyBuckets: nonEmptyBuckets,
            total: total,
            maxBucketSize: maxBucketSize,
            pendingRegister: this._toRegister.length,
            pendingDeregister: this._toDeregister.length
        };
    }

    toString() {
        const typeNames = {
            [TickRate.None]: "None",
            [TickRate.Every]: "Every",
            [TickRate.Sparse]: "Sparse",
            [TickRate.Slow]: "Slow"
        };
        return `TickBucket(${typeNames[this._tickType] ?? this._tickType})`;
    }
}

globalThis.TickBucket = TickBucket;

if (globalThis.__BP_VERBOSE__) jtask.log("[tick_list] TickBucket class loaded");
