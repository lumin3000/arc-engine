
const BlendMode = {
    Opaque: 0,
    Alpha: 1,
    Additive: 2,
    Premultiplied: 3,
    NoDepthTest: 4
};

const RenderQueue = {
    Background: 1000,
    Geometry: 2000,
    AlphaTest: 2450,
    Transparent: 3000,
    Overlay: 4000
};

const ShaderType = {
    None: 0,
    TerrainHard: 1,
    TerrainFade: 2,
    TerrainFadeRough: 3,
    TerrainWater: 4,
    TerrainWaterMoving: 5,
    TerrainHardPolluted: 6,
    TerrainFadePolluted: 7,
    TerrainFadeRoughPolluted: 8,
    Textured: 9,
    Cutout: 10,
    Transparent: 11,
    MetaOverlay: 12,
    EdgeDetect: 13,
};

globalThis.BlendMode = BlendMode;
globalThis.RenderQueue = RenderQueue;
globalThis.ShaderType = ShaderType;
