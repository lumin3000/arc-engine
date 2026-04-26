
const TickerType = Object.freeze({
    Never: 0, Normal: 1, Rare: 2, Long: 3,
    toString(v) { return ["Never","Normal","Rare","Long"][v] || "Unknown"; },
    fromString(s) { return {Never:0,Normal:1,Rare:2,Long:3}[s] ?? 0; }
});
const TimeSpeed = Object.freeze({
    Paused: 0, Normal: 1, Fast: 2, Superfast: 3, Ultrafast: 4,
    toString(v) { return ["Paused","Normal","Fast","Superfast","Ultrafast"][v] || "Unknown"; },
    fromString(s) { return {Paused:0,Normal:1,Fast:2,Superfast:3,Ultrafast:4}[s] ?? 0; }
});
globalThis.TickerType = TickerType;
globalThis.TimeSpeed = TimeSpeed;

class TickList {

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
            case TickerType.Normal:
                return 1;
            case TickerType.Rare:
                return 250;
            case TickerType.Long:
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

        for (let i = 0; i < this._buckets.length; i++) {
            const list = this._buckets[i];

            for (let j = list.length - 1; j >= 0; j--) {
                if (predicate(list[j])) {
                    list.splice(j, 1);
                }
            }
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

    register(tickable) {

        this._toRegister.push(tickable);
    }

    deregister(tickable) {

        this._toDeregister.push(tickable);
    }

    tick(ticksGame) {

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

        const currentBucket = this._buckets[ticksGame % this._tickInterval];

        for (let m = 0; m < currentBucket.length; m++) {
            const tickable = currentBucket[m];

            if (tickable.destroyed) {
                continue;
            }

            try {
                tickable.doTick();
            } catch (e) {

                const pos = tickable.spawned ? ` (at ${tickable.position?.x ?? '?'},${tickable.position?.z ?? '?'})` : "";
                const label = tickable.toStringSafe?.() ?? tickable.toString?.() ?? "Tickable";
                jtask.log.error(`Exception ticking ${label}${pos}: ${e}\n${e.stack || ''}`);
            }
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
            [TickerType.Never]: "Never",
            [TickerType.Normal]: "Normal",
            [TickerType.Rare]: "Rare",
            [TickerType.Long]: "Long"
        };
        return `TickList(${typeNames[this._tickType] ?? this._tickType})`;
    }
}

globalThis.TickList = TickList;

if (globalThis.__BP_VERBOSE__) jtask.log("[tick_list] TickList class loaded");
