
var _atlasData = null;
var _atlasTexId = -1;

globalThis.Atlas = {
    _loaded: false,

    load: function() {
        var jsonStr = std.loadFile("res/atlas/atlas_uv.json");
        if (!jsonStr) {
            throw new Error("[Atlas] failed to load res/atlas/atlas_uv.json");
        }
        _atlasData = JSON.parse(jsonStr);

        batch.load_atlas("res/atlas/atlas_page_0.png");

        this._loaded = true;
        jtask.log("[Atlas] loaded: " + Object.keys(_atlasData.frames).length + " entries");
    },

    getTexId: function() {
        return _atlasTexId;
    },

    getEntry: function(name) {
        return _atlasData.frames[name];
    },

    getFrameUV: function(name, frameIndex) {
        var entry = _atlasData.frames[name];
        if (!entry) return null;

        var raw;
        if (entry.type === "single") {
            raw = { u0: entry.u0, v0: entry.v0, u1: entry.u1, v1: entry.v1,
                    w: entry.w, h: entry.h,
                    pivotX: entry.pivot_x || 0, pivotY: entry.pivot_y || 0 };
        } else if (entry.type === "sub_anim") {
            var f = entry.frames[frameIndex];
            if (!f) return null;
            var parent = _atlasData.frames[entry.parent];
            var pivots = parent && parent.pivots;
            var linearIdx = f._linearIdx !== undefined ? f._linearIdx : frameIndex;
            var pv = pivots ? pivots[linearIdx] : null;
            raw = { u0: f.u0, v0: f.v0, u1: f.u1, v1: f.v1,
                    w: entry.cell_w, h: entry.cell_h,
                    pivotX: pv ? pv[0] : 0, pivotY: pv ? pv[1] : 0 };
        } else if (entry.type === "sheet") {
            var col = frameIndex % entry.cols;
            var row = (frameIndex / entry.cols) | 0;
            var cellU = entry.cell_w / _atlasData.atlas_size;
            var cellV = entry.cell_h / _atlasData.atlas_size;
            var u0 = entry.u0 + col * cellU;
            var v0 = entry.v0 + row * cellV;
            var linearIdx = row * entry.cols + col;
            var pv = entry.pivots ? entry.pivots[linearIdx] : null;
            raw = { u0: u0, v0: v0, u1: u0 + cellU, v1: v0 + cellV,
                    w: entry.cell_w, h: entry.cell_h,
                    pivotX: pv ? pv[0] : 0, pivotY: pv ? pv[1] : 0 };
        } else {
            return null;
        }

        // stbi flip=1: v-axis flipped (v=0 at bottom)
        var fv0 = 1.0 - raw.v1;
        var fv1 = 1.0 - raw.v0;
        return { u0: raw.u0, v0: fv0, u1: raw.u1, v1: fv1,
                 w: raw.w, h: raw.h,
                 pivotX: raw.pivotX, pivotY: raw.pivotY };
    },

    getTotalFrames: function(name) {
        var entry = _atlasData.frames[name];
        if (!entry) return 0;
        if (entry.type === "single") return 1;
        return entry.total_frames;
    },
};
