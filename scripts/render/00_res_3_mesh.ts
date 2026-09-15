
class Mesh { declare _cMeshId: number | null; declare name: string; declare vertices: unknown[]; declare uvs: unknown[]; declare colors: unknown[]; declare triangles: number[]; declare isUploaded: boolean; declare type: string; declare size: { x: number; y: number }; declare flipped: boolean;
    constructor() {
        this._cMeshId = null;
        this.name = "Unnamed Mesh";

        this.vertices = [];
        this.uvs = [];
        this.colors = [];
        this.triangles = [];

        this.isUploaded = false;
    }

    ensureUploaded(): void {

    }
}

const MeshPool = {
    _cache: new Map<string, Mesh>(),

    gridPlane(size: { x: number; y: number }, flipped = false): Mesh | undefined {
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

    getCustomMesh(name: string, generatorFn?: ((mesh: Mesh) => void) | null): Mesh | undefined {
        if (this._cache.has(name)) {
            return this._cache.get(name);
        }
        const mesh = new Mesh();
        mesh.name = name;
        if (generatorFn) generatorFn(mesh);
        this._cache.set(name, mesh);
        return mesh;
    },

    clear(): void {
        this._cache.clear();
    }
};

globalThis.Mesh = Mesh;
globalThis.MeshPool = MeshPool;
