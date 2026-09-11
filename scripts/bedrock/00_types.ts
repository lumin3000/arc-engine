
class IntVec3 {

    constructor(x = 0, y = 0, z = 0) {
        this.x = x | 0;
        this.y = y | 0;
        this.z = z | 0;
    }

    static _Zero = null;
    static _North = null;
    static _East = null;
    static _South = null;
    static _West = null;
    static _NorthWest = null;
    static _NorthEast = null;
    static _SouthWest = null;
    static _SouthEast = null;
    static _Invalid = null;

    static get Zero() { return IntVec3._Zero || (IntVec3._Zero = new IntVec3(0, 0, 0)); }
    static get North() { return IntVec3._North || (IntVec3._North = new IntVec3(0, 0, 1)); }
    static get East() { return IntVec3._East || (IntVec3._East = new IntVec3(1, 0, 0)); }
    static get South() { return IntVec3._South || (IntVec3._South = new IntVec3(0, 0, -1)); }
    static get West() { return IntVec3._West || (IntVec3._West = new IntVec3(-1, 0, 0)); }
    static get NorthWest() { return IntVec3._NorthWest || (IntVec3._NorthWest = new IntVec3(-1, 0, 1)); }
    static get NorthEast() { return IntVec3._NorthEast || (IntVec3._NorthEast = new IntVec3(1, 0, 1)); }
    static get SouthWest() { return IntVec3._SouthWest || (IntVec3._SouthWest = new IntVec3(-1, 0, -1)); }
    static get SouthEast() { return IntVec3._SouthEast || (IntVec3._SouthEast = new IntVec3(1, 0, -1)); }
    static get Invalid() { return IntVec3._Invalid || (IntVec3._Invalid = new IntVec3(-1000, -1000, -1000)); }
    static get invalid() { return IntVec3.Invalid; }

    static get adjacentCellsAround() {
        if (!this._adjacentCellsAround) {
            this._adjacentCellsAround = [
                new IntVec3(0, 0, 1),
                new IntVec3(1, 0, 0),
                new IntVec3(0, 0, -1),
                new IntVec3(-1, 0, 0),
                new IntVec3(-1, 0, 1),
                new IntVec3(1, 0, 1),
                new IntVec3(-1, 0, -1),
                new IntVec3(1, 0, -1)
            ];
        }
        return this._adjacentCellsAround;
    }

    get toIntVec2() {
        return { x: this.x, z: this.z };
    }

    get isValid() {
        return this.y >= 0;
    }

    static isValid(c) {
        if (!c) return false;

        if (c.x === -1000 || c.z === -1000) return false;

        if (c.y !== undefined && c.y < 0) return false;
        return true;
    }

    get lengthHorizontalSquared() {
        return this.x * this.x + this.z * this.z;
    }

    get lengthHorizontal() {
        return Math.sqrt(this.x * this.x + this.z * this.z);
    }

    get lengthManhattan() {
        return Math.abs(this.x) + Math.abs(this.z);
    }

    get isCardinal() {
        if (this.x !== 0) {
            return this.z === 0;
        }
        return true;
    }

    get magnitude() {
        return Math.sqrt(this.x * this.x + this.z * this.z);
    }

    get sqrMagnitude() {
        return this.x * this.x + this.z * this.z;
    }

    get angleFlat() {
        if (this.x === 0 && this.z === 0) {
            return 0;
        }

        const radians = Math.atan2(this.x, this.z);
        let degrees = radians * (180 / Math.PI);
        if (degrees < 0) degrees += 360;
        return degrees;
    }

    add(other) {
        return new IntVec3(this.x + other.x, this.y + other.y, this.z + other.z);
    }

    sub(other) {
        return new IntVec3(this.x - other.x, this.y - other.y, this.z - other.z);
    }

    mul(i) {
        return new IntVec3(this.x * i, this.y * i, this.z * i);
    }

    div(i) {
        return new IntVec3((this.x / i) | 0, (this.y / i) | 0, (this.z / i) | 0);
    }

    neg() {
        return new IntVec3(-this.x, -this.y, -this.z);
    }

    equals(other) {
        if (!other) return false;
        return this.x === other.x && this.y === other.y && this.z === other.z;
    }

    static equals(a, b) {
        if (a === b) return true;
        if (!a || !b) return false;
        return a.x === b.x && a.y === b.y && a.z === b.z;
    }

    static add(a, b) {
        return new IntVec3(
            a.x + b.x,
            (a.y || 0) + (b.y || 0),
            a.z + b.z
        );
    }

    static sub(a, b) {
        return new IntVec3(
            a.x - b.x,
            (a.y || 0) - (b.y || 0),
            a.z - b.z
        );
    }

    static getHashCode(v) {
        if (!v) return 0;
        if (v instanceof IntVec3 && typeof v.getHashCode === 'function') {
            return v.getHashCode();
        }
        let hash = 0;
        hash = ((hash << 5) - hash) + (v.x | 0);
        hash = ((hash << 5) - hash) + ((v.y ?? 0) | 0);
        hash = ((hash << 5) - hash) + (v.z | 0);
        return hash | 0;
    }

    toVector3() {
        return { x: this.x, y: this.y, z: this.z };
    }

    toVector3Shifted() {
        return { x: this.x + 0.5, y: this.y, z: this.z + 0.5 };
    }

    toVector3ShiftedWithAltitude(altitude) {
        return { x: this.x + 0.5, y: this.y + altitude, z: this.z + 0.5 };
    }

    toVector2() {
        return { x: this.x, y: this.z };
    }

    toString() {
        return `(${this.x}, ${this.y}, ${this.z})`;
    }

    inHorDistOf(other, maxDist) {
        const dx = this.x - other.x;
        const dz = this.z - other.z;
        return dx * dx + dz * dz <= maxDist * maxDist;
    }

    distanceToSquared(other) {
        const dx = this.x - other.x;
        const dz = this.z - other.z;
        return dx * dx + dz * dz;
    }

    distanceTo(other) {
        return Math.sqrt(this.distanceToSquared(other));
    }

    adjacentToCardinal(other) {
        if (!this.isValid) return false;
        if (other.z === this.z && (other.x === this.x + 1 || other.x === this.x - 1)) {
            return true;
        }
        if (other.x === this.x && (other.z === this.z + 1 || other.z === this.z - 1)) {
            return true;
        }
        return false;
    }

    adjacentToDiagonal(other) {
        if (!this.isValid) return false;
        return Math.abs(this.x - other.x) === 1 && Math.abs(this.z - other.z) === 1;
    }

    adjacentTo(other) {
        return this.adjacentToDiagonal(other) || this.adjacentToCardinal(other);
    }

    cardinalTo(other) {
        if (!this.isValid || !other.isValid) {
            return false;
        }
        if (other.z !== this.z) {
            return other.x === this.x;
        }
        return true;
    }

    clampInsideRect(rect) {
        return new IntVec3(
            Math.max(rect.minX, Math.min(rect.maxX, this.x)),
            0,
            Math.max(rect.minZ, Math.min(rect.maxZ, this.z))
        );
    }

    clampInsideMap(map) {
        return this.clampInsideRect(CellRect.wholeMap(map));
    }

    clampMagnitude(maxMagnitude) {
        const len = this.lengthHorizontal;
        if (len <= maxMagnitude) {
            return this;
        }
        const scale = maxMagnitude / len;
        return new IntVec3(
            Math.round(this.x * scale),
            this.y,
            Math.round(this.z * scale)
        );
    }

    static fromString(str) {
        if (!str) return IntVec3.Invalid;

        str = str.replace(/[()]/g, '').trim();
        const parts = str.split(',').map(s => parseInt(s.trim(), 10));

        if (parts.length === 2) {

            if (isNaN(parts[0]) || isNaN(parts[1])) return IntVec3.Invalid;
            return new IntVec3(parts[0], 0, parts[1]);
        } else if (parts.length === 3) {

            if (isNaN(parts[0]) || isNaN(parts[1]) || isNaN(parts[2])) return IntVec3.Invalid;
            return new IntVec3(parts[0], parts[1], parts[2]);
        }

        return IntVec3.Invalid;
    }

    static fromVector3(v, newY = 0) {
        return new IntVec3(v.x | 0, newY, v.z | 0);
    }

    static fromPolar(angle, distance) {
        const radians = angle * (Math.PI / 180);
        const x = Math.cos(radians) * distance;
        const z = Math.sin(radians) * distance;
        return new IntVec3(x | 0, 0, z | 0);
    }

    getHashCode() {
        let hash = 0;
        hash = ((hash << 5) - hash) + this.x;
        hash = ((hash << 5) - hash) + this.y;
        hash = ((hash << 5) - hash) + this.z;
        return hash | 0;
    }

    uniqueHashCode() {

        return BigInt(this.x) + BigInt(4096) * BigInt(this.z) + BigInt(16777216) * BigInt(this.y);
    }

    toIndex(mapSizeX) {
        return this.z * mapSizeX + this.x;
    }

    static fromIndex(index, mapSizeX) {
        return new IntVec3(index % mapSizeX, 0, (index / mapSizeX) | 0);
    }
}

class CellRect {

    constructor(minX = 0, minZ = 0, width = 0, height = 0) {
        this.minX = minX | 0;
        this.minZ = minZ | 0;
        this.maxX = (minX + width - 1) | 0;
        this.maxZ = (minZ + height - 1) | 0;
    }

    static get Empty() {
        return new CellRect(0, 0, 0, 0);
    }

    static fromLimits(minX, minZ, maxX, maxZ) {
        const rect = new CellRect();
        rect.minX = Math.min(minX, maxX);
        rect.minZ = Math.min(minZ, maxZ);
        rect.maxX = Math.max(maxX, minX);
        rect.maxZ = Math.max(maxZ, minZ);
        return rect;
    }

    static fromLimitsVec(first, second) {
        return CellRect.fromLimits(first.x, first.z, second.x, second.z);
    }

    static centeredOn(center, radius) {
        const rect = new CellRect();
        rect.minX = center.x - radius;
        rect.maxX = center.x + radius;
        rect.minZ = center.z - radius;
        rect.maxZ = center.z + radius;
        return rect;
    }

    static centeredOnWithSize(center, width, height) {
        const rect = new CellRect();
        rect.minX = center.x - ((width / 2) | 0);
        rect.minZ = center.z - ((height / 2) | 0);
        rect.maxX = rect.minX + width - 1;
        rect.maxZ = rect.minZ + height - 1;
        return rect;
    }

    static singleCell(c) {
        return new CellRect(c.x, c.z, 1, 1);
    }

    static wholeMap(map) {
        return new CellRect(0, 0, map.size.x, map.size.z);
    }

    get isEmpty() {
        return this.width <= 0 || this.height <= 0;
    }

    get area() {
        return this.width * this.height;
    }

    get width() {
        if (this.minX > this.maxX) return 0;
        return this.maxX - this.minX + 1;
    }

    set width(value) {
        this.maxX = this.minX + Math.max(value, 0) - 1;
    }

    get height() {
        if (this.minZ > this.maxZ) return 0;
        return this.maxZ - this.minZ + 1;
    }

    set height(value) {
        this.maxZ = this.minZ + Math.max(value, 0) - 1;
    }

    get centerCell() {
        return new IntVec3(
            this.minX + ((this.width / 2) | 0),
            0,
            this.minZ + ((this.height / 2) | 0)
        );
    }

    get centerVector3() {
        return {
            x: this.minX + this.width / 2,
            y: 0,
            z: this.minZ + this.height / 2
        };
    }

    get min() {
        return new IntVec3(this.minX, 0, this.minZ);
    }

    get max() {
        return new IntVec3(this.maxX, 0, this.maxZ);
    }

    contains(c) {
        return c.x >= this.minX && c.x <= this.maxX &&
            c.z >= this.minZ && c.z <= this.maxZ;
    }

    overlaps(other) {
        if (this.isEmpty || other.isEmpty) return false;
        return this.minX <= other.maxX && this.maxX >= other.minX &&
            this.maxZ >= other.minZ && this.minZ <= other.maxZ;
    }

    inBounds(map) {
        return this.minX >= 0 && this.minZ >= 0 &&
            this.maxX < map.size.x && this.maxZ < map.size.z;
    }

    isOnEdge(c) {
        if (c.x === this.minX && c.z >= this.minZ && c.z <= this.maxZ) return true;
        if (c.x === this.maxX && c.z >= this.minZ && c.z <= this.maxZ) return true;
        if (c.z === this.minZ && c.x >= this.minX && c.x <= this.maxX) return true;
        if (c.z === this.maxZ && c.x >= this.minX && c.x <= this.maxX) return true;
        return false;
    }

    isCorner(c) {
        return (c.x === this.minX || c.x === this.maxX) &&
            (c.z === this.minZ || c.z === this.maxZ);
    }

    clipInsideMap(map) {
        const result = new CellRect();
        result.minX = Math.max(0, this.minX);
        result.minZ = Math.max(0, this.minZ);
        result.maxX = Math.min(map.size.x - 1, this.maxX);
        result.maxZ = Math.min(map.size.z - 1, this.maxZ);
        return result;
    }

    clipInsideRect(other) {
        const result = new CellRect();
        result.minX = Math.max(other.minX, this.minX);
        result.maxX = Math.min(other.maxX, this.maxX);
        result.minZ = Math.max(other.minZ, this.minZ);
        result.maxZ = Math.min(other.maxZ, this.maxZ);
        return result;
    }

    expandedBy(dist) {
        const result = new CellRect();
        result.minX = this.minX - dist;
        result.minZ = this.minZ - dist;
        result.maxX = this.maxX + dist;
        result.maxZ = this.maxZ + dist;
        return result;
    }

    contractedBy(dist) {
        return this.expandedBy(-dist);
    }

    movedBy(offset) {
        const result = new CellRect();
        result.minX = this.minX + offset.x;
        result.minZ = this.minZ + offset.z;
        result.maxX = this.maxX + offset.x;
        result.maxZ = this.maxZ + offset.z;
        return result;
    }

    *cells() {
        for (let z = this.minZ; z <= this.maxZ; z++) {
            for (let x = this.minX; x <= this.maxX; x++) {
                yield new IntVec3(x, 0, z);
            }
        }
    }

    *edgeCells() {
        if (this.isEmpty) return;

        for (let x = this.minX; x <= this.maxX; x++) {
            yield new IntVec3(x, 0, this.minZ);
        }

        for (let z = this.minZ + 1; z <= this.maxZ; z++) {
            yield new IntVec3(this.maxX, 0, z);
        }

        for (let x = this.maxX - 1; x >= this.minX; x--) {
            yield new IntVec3(x, 0, this.maxZ);
        }

        for (let z = this.maxZ - 1; z > this.minZ; z--) {
            yield new IntVec3(this.minX, 0, z);
        }
    }

    *corners() {
        if (this.isEmpty) return;
        yield new IntVec3(this.minX, 0, this.minZ);
        if (this.height > 1) {
            yield new IntVec3(this.minX, 0, this.maxZ);
            if (this.width > 1) {
                yield new IntVec3(this.maxX, 0, this.maxZ);
            }
        }
        if (this.width > 1) {
            yield new IntVec3(this.maxX, 0, this.minZ);
        }
    }

    forEach(callback) {
        for (const cell of this.cells()) {
            callback(cell);
        }
    }

    equals(other) {
        if (!other) return false;
        return this.minX === other.minX && this.maxX === other.maxX &&
            this.minZ === other.minZ && this.maxZ === other.maxZ;
    }

    toString() {
        return `(${this.minX},${this.minZ},${this.maxX},${this.maxZ})`;
    }

    static fromString(str) {
        str = str.replace(/[()]/g, '').trim();
        const parts = str.split(',').map(s => parseInt(s.trim(), 10));
        if (parts.length !== 4 || parts.some(isNaN)) {
            return CellRect.Empty;
        }
        return CellRect.fromLimits(parts[0], parts[1], parts[2], parts[3]);
    }

    *getCellsOnEdge(rot) {
        if (this.isEmpty) return;

        if (rot === Rot4.North) {
            for (let x = this.minX; x <= this.maxX; x++) {
                yield new IntVec3(x, 0, this.maxZ);
            }
        } else if (rot === Rot4.East) {
            for (let z = this.minZ; z <= this.maxZ; z++) {
                yield new IntVec3(this.maxX, 0, z);
            }
        } else if (rot === Rot4.South) {
            for (let x = this.minX; x <= this.maxX; x++) {
                yield new IntVec3(x, 0, this.minZ);
            }
        } else if (rot === Rot4.West) {
            for (let z = this.minZ; z <= this.maxZ; z++) {
                yield new IntVec3(this.minX, 0, z);
            }
        }
    }
}

class Vector2 {
    constructor(x = 0, y = 0) {
        this.x = x;
        this.y = y;
    }

    static get zero() { return new Vector2(0, 0); }
    static get one() { return new Vector2(1, 1); }

    add(other) { return new Vector2(this.x + other.x, this.y + other.y); }
    sub(other) { return new Vector2(this.x - other.x, this.y - other.y); }
    mul(scalar) { return new Vector2(this.x * scalar, this.y * scalar); }

    get magnitude() { return Math.sqrt(this.x * this.x + this.y * this.y); }

    equals(other) {
        return other && this.x === other.x && this.y === other.y;
    }

    toString() { return `(${this.x}, ${this.y})`; }
}

class Color {
    constructor(r = 0, g = 0, b = 0, a = 1) {
        this.r = r;
        this.g = g;
        this.b = b;
        this.a = a;
    }

    static get white() { return new Color(1, 1, 1, 1); }
    static get black() { return new Color(0, 0, 0, 1); }
    static get gray() { return new Color(0.5, 0.5, 0.5, 1); }
    static get clear() { return new Color(0, 0, 0, 0); }
    static get red() { return new Color(1, 0, 0, 1); }
    static get green() { return new Color(0, 1, 0, 1); }
    static get blue() { return new Color(0, 0, 1, 1); }

    toArray() { return [this.r, this.g, this.b, this.a]; }

    toMu() {
        return [
            Math.floor(Math.max(0, Math.min(1, this.r)) * 255),
            Math.floor(Math.max(0, Math.min(1, this.g)) * 255),
            Math.floor(Math.max(0, Math.min(1, this.b)) * 255),
            Math.floor(Math.max(0, Math.min(1, this.a)) * 255)
        ];
    }

    equals(other) {
        return other && this.r === other.r && this.g === other.g &&
            this.b === other.b && this.a === other.a;
    }

    toString() { return `RGBA(${this.r}, ${this.g}, ${this.b}, ${this.a})`; }
}

class Rect {
    constructor(x = 0, y = 0, width = 0, height = 0) {
        this.x = x;
        this.y = y;
        this.width = width;
        this.height = height;
    }

    get xMin() { return this.x; }
    set xMin(value) { this.width += this.x - value; this.x = value; }

    get yMin() { return this.y; }
    set yMin(value) { this.height += this.y - value; this.y = value; }

    get xMax() { return this.x + this.width; }
    set xMax(value) { this.width = value - this.x; }

    get yMax() { return this.y + this.height; }
    set yMax(value) { this.height = value - this.y; }

    get center() {
        return new Vector2(this.x + this.width / 2, this.y + this.height / 2);
    }
    set center(value) {
        this.x = value.x - this.width / 2;
        this.y = value.y - this.height / 2;
    }

    get position() { return new Vector2(this.x, this.y); }
    set position(value) { this.x = value.x; this.y = value.y; }

    get size() { return new Vector2(this.width, this.height); }
    set size(value) { this.width = value.x; this.height = value.y; }

    Contains(point) {
        return point.x >= this.x && point.x < this.xMax &&
            point.y >= this.y && point.y < this.yMax;
    }

    Overlaps(other) {
        return this.xMax > other.x && this.x < other.xMax &&
            this.yMax > other.y && this.y < other.yMax;
    }

    ContractedBy(margin) {
        return new Rect(this.x + margin, this.y + margin,
            this.width - margin * 2, this.height - margin * 2);
    }

    ExpandedBy(margin) {
        return new Rect(this.x - margin, this.y - margin,
            this.width + margin * 2, this.height + margin * 2);
    }

    AtZero() {
        return new Rect(0, 0, this.width, this.height);
    }

    LeftPart(pct) {
        return new Rect(this.x, this.y, this.width * pct, this.height);
    }

    RightPart(pct) {
        const w = this.width * pct;
        return new Rect(this.x + this.width - w, this.y, w, this.height);
    }

    TopPart(pct) {
        return new Rect(this.x, this.y, this.width, this.height * pct);
    }

    BottomPart(pct) {
        const h = this.height * pct;
        return new Rect(this.x, this.y + this.height - h, this.width, h);
    }

    LeftHalf() { return this.LeftPart(0.5); }
    RightHalf() { return this.RightPart(0.5); }
    TopHalf() { return this.TopPart(0.5); }
    BottomHalf() { return this.BottomPart(0.5); }

    get w() { return this.width; }
    set w(v) { this.width = v; }
    get h() { return this.height; }
    set h(v) { this.height = v; }

    equals(other) {
        return other && this.x === other.x && this.y === other.y &&
            this.width === other.width && this.height === other.height;
    }

    toString() {
        return `(x:${this.x}, y:${this.y}, w:${this.width}, h:${this.height})`;
    }
}

globalThis.Vector2 = Vector2;
globalThis.Color = Color;
globalThis.Rect = Rect;
globalThis.IntVec3 = IntVec3;
globalThis.CellRect = CellRect;

if (globalThis.__BP_VERBOSE__) jtask.log("[types] IntVec3, CellRect, Vector2, Color, Rect loaded");
