
class Matrix4x4 {
    constructor() {

        this.m = new Float32Array(16);
    }

    static get identity() {
        const r = new Matrix4x4();
        r.m[0] = 1; r.m[5] = 1; r.m[10] = 1; r.m[15] = 1;
        return r;
    }

    static TRS(pos, q, s) {
        const r = new Matrix4x4();
        const m = r.m;

        const x = q.x, y = q.y, z = q.z, w = q.w;
        const x2 = x + x, y2 = y + y, z2 = z + z;
        const xx = x * x2, xy = x * y2, xz = x * z2;
        const yy = y * y2, yz = y * z2, zz = z * z2;
        const wx = w * x2, wy = w * y2, wz = w * z2;

        const sx = s.x, sy = s.y, sz = s.z;

        m[0]  = (1 - (yy + zz)) * sx;
        m[1]  = (xy + wz) * sx;
        m[2]  = (xz - wy) * sx;
        m[3]  = 0;

        m[4]  = (xy - wz) * sy;
        m[5]  = (1 - (xx + zz)) * sy;
        m[6]  = (yz + wx) * sy;
        m[7]  = 0;

        m[8]  = (xz + wy) * sz;
        m[9]  = (yz - wx) * sz;
        m[10] = (1 - (xx + yy)) * sz;
        m[11] = 0;

        m[12] = pos.x;
        m[13] = pos.y;
        m[14] = pos.z;
        m[15] = 1;

        return r;
    }

    static Translate(v) {
        const r = Matrix4x4.identity;
        r.m[12] = v.x;
        r.m[13] = v.y;
        r.m[14] = v.z;
        return r;
    }

    static Scale(v) {
        const r = new Matrix4x4();
        r.m[0] = v.x;
        r.m[5] = v.y;
        r.m[10] = v.z;
        r.m[15] = 1;
        return r;
    }

    static Rotate(q) {
        return Matrix4x4.TRS({ x: 0, y: 0, z: 0 }, q, { x: 1, y: 1, z: 1 });
    }

    Position() {
        return { x: this.m[12], y: this.m[13], z: this.m[14] };
    }

    mul(other) {
        const r = new Matrix4x4();
        const a = this.m, b = other.m, o = r.m;

        for (let col = 0; col < 4; col++) {
            const bc0 = b[col * 4];
            const bc1 = b[col * 4 + 1];
            const bc2 = b[col * 4 + 2];
            const bc3 = b[col * 4 + 3];

            o[col * 4]     = a[0] * bc0 + a[4] * bc1 + a[8]  * bc2 + a[12] * bc3;
            o[col * 4 + 1] = a[1] * bc0 + a[5] * bc1 + a[9]  * bc2 + a[13] * bc3;
            o[col * 4 + 2] = a[2] * bc0 + a[6] * bc1 + a[10] * bc2 + a[14] * bc3;
            o[col * 4 + 3] = a[3] * bc0 + a[7] * bc1 + a[11] * bc2 + a[15] * bc3;
        }

        return r;
    }

    MultiplyPoint3x4(p) {
        const m = this.m;
        return {
            x: m[0] * p.x + m[4] * p.y + m[8]  * p.z + m[12],
            y: m[1] * p.x + m[5] * p.y + m[9]  * p.z + m[13],
            z: m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14],
        };
    }

    get m00() { return this.m[0]; }  set m00(v) { this.m[0] = v; }
    get m10() { return this.m[1]; }  set m10(v) { this.m[1] = v; }
    get m20() { return this.m[2]; }  set m20(v) { this.m[2] = v; }
    get m30() { return this.m[3]; }  set m30(v) { this.m[3] = v; }
    get m01() { return this.m[4]; }  set m01(v) { this.m[4] = v; }
    get m11() { return this.m[5]; }  set m11(v) { this.m[5] = v; }
    get m21() { return this.m[6]; }  set m21(v) { this.m[6] = v; }
    get m31() { return this.m[7]; }  set m31(v) { this.m[7] = v; }
    get m02() { return this.m[8]; }  set m02(v) { this.m[8] = v; }
    get m12() { return this.m[9]; }  set m12(v) { this.m[9] = v; }
    get m22() { return this.m[10]; } set m22(v) { this.m[10] = v; }
    get m32() { return this.m[11]; } set m32(v) { this.m[11] = v; }
    get m03() { return this.m[12]; } set m03(v) { this.m[12] = v; }
    get m13() { return this.m[13]; } set m13(v) { this.m[13] = v; }
    get m23() { return this.m[14]; } set m23(v) { this.m[14] = v; }
    get m33() { return this.m[15]; } set m33(v) { this.m[15] = v; }
}

globalThis.Matrix4x4 = Matrix4x4;

jtask.log("[render] Matrix4x4 loaded");
