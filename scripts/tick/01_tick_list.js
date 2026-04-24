
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

        this._thingLists = [];

        this._thingsToRegister = [];

        this._thingsToDeregister = [];

        const interval = this._tickInterval;
        for (let i = 0; i < interval; i++) {
            this._thingLists.push([]);
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

        for (let i = 0; i < this._thingLists.length; i++) {
            this._thingLists[i].length = 0;
        }

        this._thingsToRegister.length = 0;
        this._thingsToDeregister.length = 0;
    }

    removeWhere(predicate) {

        for (let i = 0; i < this._thingLists.length; i++) {
            const list = this._thingLists[i];

            for (let j = list.length - 1; j >= 0; j--) {
                if (predicate(list[j])) {
                    list.splice(j, 1);
                }
            }
        }

        for (let j = this._thingsToRegister.length - 1; j >= 0; j--) {
            if (predicate(this._thingsToRegister[j])) {
                this._thingsToRegister.splice(j, 1);
            }
        }

        for (let j = this._thingsToDeregister.length - 1; j >= 0; j--) {
            if (predicate(this._thingsToDeregister[j])) {
                this._thingsToDeregister.splice(j, 1);
            }
        }
    }

    registerMono(thing) {

        this._thingsToRegister.push(thing);
    }

    deregisterMono(thing) {

        this._thingsToDeregister.push(thing);
    }

    tick(ticksGame) {

        for (let i = 0; i < this._thingsToRegister.length; i++) {
            const thing = this._thingsToRegister[i];
            this._bucketOf(thing).push(thing);
        }
        this._thingsToRegister.length = 0;

        for (let j = 0; j < this._thingsToDeregister.length; j++) {
            const thing = this._thingsToDeregister[j];
            const bucket = this._bucketOf(thing);
            const idx = bucket.indexOf(thing);
            if (idx > -1) {
                bucket.splice(idx, 1);
            }
        }
        this._thingsToDeregister.length = 0;

        const currentBucket = this._thingLists[ticksGame % this._tickInterval];

        for (let m = 0; m < currentBucket.length; m++) {
            const thing = currentBucket[m];

            if (thing.destroyed) {
                continue;
            }

            try {
                thing.doTick();
            } catch (e) {

                const pos = thing.spawned ? ` (at ${thing.position?.x ?? '?'},${thing.position?.z ?? '?'})` : "";
                const thingStr = thing.toStringSafe?.() ?? thing.toString?.() ?? "Mono";
                jtask.log.error(`Exception ticking ${thingStr}${pos}: ${e}\n${e.stack || ''}`);
            }
        }
    }

    _bucketOf(thing) {

        let num = thing.getHashCode();

        if (num < 0) {
            num = -num;
        }

        const index = num % this._tickInterval;

        return this._thingLists[index];
    }

    _thingCount() {
        var n = 0;
        for (var i = 0; i < this._thingLists.length; i++) n += this._thingLists[i].length;
        return n;
    }

    getStats() {
        let totalMonos = 0;
        let nonEmptyBuckets = 0;
        let maxBucketSize = 0;

        for (const bucket of this._thingLists) {
            totalMonos += bucket.length;
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
            totalBuckets: this._thingLists.length,
            nonEmptyBuckets: nonEmptyBuckets,
            totalMonos: totalMonos,
            maxBucketSize: maxBucketSize,
            pendingRegister: this._thingsToRegister.length,
            pendingDeregister: this._thingsToDeregister.length
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
