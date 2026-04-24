
class Mesh {
    constructor() {
        this._cMeshId = null;
        this.name = "Unnamed Mesh";

        this.vertices = [];
        this.uvs = [];
        this.colors = [];
        this.triangles = [];

        this.isUploaded = false;
    }

    ensureUploaded() {

    }
}

const MeshPool = {
    _cache: new Map(),

    gridPlane(size, flipped = false) {
        const key = `plane_${size.x}_${size.y}_${flipped}`;
        if (this._cache.has(key)) {
            return this._cache.get(key);
        }

        const mesh = new Mesh();
        mesh.name = key;
        mesh.type = 'plane';
        mesh.size = { ...size };
        mesh.flipped = flipped;

        this._cache.set(key, mesh);
        return mesh;
    },

    getCustomMesh(name, generatorFn) {
        if (this._cache.has(name)) {
            return this._cache.get(name);
        }
        const mesh = new Mesh();
        mesh.name = name;
        if (generatorFn) generatorFn(mesh);
        this._cache.set(name, mesh);
        return mesh;
    },

    clear() {
        this._cache.clear();
    }
};

globalThis.Mesh = Mesh;
globalThis.MeshPool = MeshPool;
