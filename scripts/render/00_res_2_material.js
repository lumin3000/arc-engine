
class Material {
    constructor(shader, color) {
        this.shader = shader;
        this.color = color || { r: 1, g: 1, b: 1, a: 1 };
        this.texturePath = null;
        this.textureId = -1;
        this.blendMode = BlendMode.Alpha;

        this.renderQueue = 3000;

        this.uvOffset = { x: 0, y: 0 };
        this.uvScale = { x: 1, y: 1 };

        this.id = null;
    }

    get mainTexture() {
        return this.texturePath;
    }

    set mainTexture(path) {
        if (this.texturePath === path) return;
        this.texturePath = path;

        if (path && globalThis.graphics?.load_texture) {
            this.textureId = globalThis.graphics.load_texture(path);
        } else {
            this.textureId = -1;
        }
    }

    setShader(shader) {
        this.shader = shader;
    }

    setColor(r, g, b, a) {
        this.color = { r, g, b, a };
    }
}

const MaterialPool = {
    _cache: new Map(),

    get TEXTURE_PATHS() {
        return globalThis.GAME_CONFIG?.TEXTURE_PATHS || [];
    },

    get TEXTURE_ROOT() {
        return this.TEXTURE_PATHS[0];
    },

    _fileExistsCache: new Map(),

    _fileExists(path) {
        if (this._fileExistsCache.has(path)) {
            return this._fileExistsCache.get(path);
        }

        let exists = false;
        if (typeof std !== "undefined" && std.loadFile) {
            const content = std.loadFile(path);
            exists = (content !== null);
        }

        this._fileExistsCache.set(path, exists);
        return exists;
    },

    resolveTexturePath(texPath, suffix = "") {
        if (!texPath) return null;

        if (texPath.startsWith("/")) {
            return texPath;
        }

        const parts = texPath.split("/");
        let fileName = parts[parts.length - 1];

        if (texPath.includes("/Linked/") || texPath.includes("_Atlas")) {

            if (!fileName.includes("Atlas")) {
                fileName = fileName + "_Atlas_Smooth";
            }
        }

        const fullFileName = fileName.endsWith(".png")
            ? fileName + suffix
            : fileName + suffix + ".png";

        for (const root of this.TEXTURE_PATHS) {
            const fullPath = root + "/" + fullFileName;
            if (this._fileExists(fullPath)) {
                return fullPath;
            }
        }

        return this.TEXTURE_PATHS[0] + "/" + fullFileName;
    },

    matFrom(path, shaderDef, color, silent = false) {
        if (!path) return null;

        const shaderName = shaderDef?.name || 'Standard';
        const colorKey = color ? `${color.r}_${color.g}_${color.b}_${color.a}` : '1_1_1_1';
        const key = `${path}_${shaderName}_${colorKey}`;

        if (this._cache.has(key)) {
            return this._cache.get(key);
        }

        const absolutePath = this.resolveTexturePath(path);

        if (silent && !this._fileExists(absolutePath)) {

            const mat = new Material(null, color ? { ...color } : { r: 1, g: 1, b: 1, a: 1 });
            mat.path = path;
            mat.id = key;
            mat.textureId = -1;

            return mat;
        }

        const shader = globalThis.ShaderDatabase?.get(shaderName);

        const mat = new Material(shader, color ? { ...color } : { r: 1, g: 1, b: 1, a: 1 });
        mat.path = path;
        mat.id = key;

        mat.mainTexture = absolutePath;

        if (shaderName === 'Transparent' || shaderName === 'Standard' ||
            shaderName === 'Cutout' || shaderName === 'CutoutComplex' ||
            shaderName === 'EdgeDetect') {
            mat.blendMode = BlendMode.Alpha;
        } else if (shaderName === 'SunShadowFade') {
            mat.blendMode = BlendMode.Alpha;
        } else {
            mat.blendMode = BlendMode.Opaque;
        }

        this._cache.set(key, mat);
        return mat;
    },

    clear() {
        this._cache.clear();
    }
};

class MaterialAtlas {
    constructor(rootMat) {
        this._rootMat = rootMat;
        this._subMats = new Array(16);

        const texPadding = 1.0 / 32.0;
        const mainTextureScale = 0.1875;

        for (let linkDir = 0; linkDir < 16; linkDir++) {
            const x = (linkDir % 4) * 0.25 + texPadding;
            const y = Math.floor(linkDir / 4) * 0.25 + texPadding;

            this._subMats[linkDir] = new Material(rootMat.shader, rootMat.color);

            const sm = this._subMats[linkDir];
            sm.path = rootMat.path;
            sm.texturePath = rootMat.texturePath;
            sm.textureId = rootMat.textureId;
            sm.blendMode = rootMat.blendMode;
            sm.renderQueue = rootMat.renderQueue;

            sm.id = `${rootMat.id}_atlas_${linkDir}`;
            sm.uvOffset = { x: x, y: y };
            sm.uvScale = { x: mainTextureScale, y: mainTextureScale };
            sm.atlasIndex = linkDir;
        }
    }

    subMat(linkDir) {
        const index = linkDir & 0xF;
        return this._subMats[index] || this._subMats[0];
    }
}

const MaterialAtlasPool = {
    _atlasDict: new Map(),

    subMaterialFromAtlas(mat, linkSet) {
        if (!mat) return null;

        const key = mat.id || mat.path;
        if (!this._atlasDict.has(key)) {
            this._atlasDict.set(key, new MaterialAtlas(mat));
        }

        return this._atlasDict.get(key).subMat(linkSet);
    },

    clear() {
        this._atlasDict.clear();
    }
};

const MatBases = {
    _loaded: {},

    get SunShadow() { return this._get('SunShadow', 'SunShadow'); },
    get SunShadowFade() { return this._get('SunShadowFade', 'SunShadowFade'); },
    get LightOverlay() { return this._get('LightOverlay', 'LightOverlay'); },
    get ShadowMask() { return this._get('ShadowMask', 'ShadowMask'); },

    _get(name, shaderName) {
        if (!this._loaded[name]) {

            this._loaded[name] = MaterialPool.matFrom(`MatBases/${name}`, { name: shaderName }, { r: 1, g: 1, b: 1, a: 1 });

        }
        return this._loaded[name];
    }
};

globalThis.Material = Material;
globalThis.MaterialPool = MaterialPool;
globalThis.MaterialAtlasPool = MaterialAtlasPool;
globalThis.MatBases = MatBases;
