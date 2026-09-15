interface ShaderHandle { name: string; id: number; valid: boolean; }

class Shader { declare name: string; declare vs: unknown; declare fs: unknown; declare id: number; declare valid: boolean;
    constructor(name: string, vs: unknown, fs: unknown) {
        this.name = name;
        this.vs = vs;
        this.fs = fs;
        this.id = -1;
        this.valid = false;

        this._compile();
    }

    _compile(): void {
        if (!globalThis.graphics?.create_shader) {
            throw new Error("[Shader] graphics.create_shader binding not found!");
        }

        this.id = globalThis.graphics.create_shader(this.vs, this.fs);
        this.valid = (this.id !== undefined && this.id >= 0);

        if (!this.valid) {
            throw new Error(`[Shader] Failed to compile '${this.name}'`);
        }
    }
}

const ShaderDatabase = {
    _shaders: new Map<string, ShaderHandle>(),
    _initialized: false,

    load(name: string, vs: unknown, fs: unknown): ShaderHandle | undefined {
        if (this._shaders.has(name)) {
            return this._shaders.get(name);
        }
        const shader = new Shader(name, vs, fs);
        this._shaders.set(name, shader);
        return shader;
    },

    get(name: string): ShaderHandle | null {

        return this._shaders.get(name) || null;
    },

    init(): void {
        if (this._initialized) return;
        this._initialized = true;

        this._shaders.set('Standard', { name: 'Standard', id: -1, valid: false });
        this._shaders.set('Landform', { name: 'Landform', id: -1, valid: false });
        this._shaders.set('Transparent', { name: 'Transparent', id: -1, valid: false });
        this._shaders.set('Cutout', { name: 'Cutout', id: -1, valid: false });
        this._shaders.set('CutoutComplex', { name: 'CutoutComplex', id: -1, valid: false });
        this._shaders.set('MetaOverlay', { name: 'MetaOverlay', id: -1, valid: false });
        this._shaders.set('EdgeDetect', { name: 'EdgeDetect', id: -1, valid: false });
        this._shaders.set('SunShadow', { name: 'SunShadow', id: -1, valid: false });
        this._shaders.set('SunShadowFade', { name: 'SunShadowFade', id: -1, valid: false });
    }
};

globalThis.Shader = Shader;
globalThis.ShaderDatabase = ShaderDatabase;
