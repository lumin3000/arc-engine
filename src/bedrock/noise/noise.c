/*
 * noise.c - C 层噪声引擎实现
 *
 * 对齐: JS scripts/mapgen/noise/00_perlin.js
 * 同 seed 同坐标，C 输出必须与 JS 输出一致（差值 < 1e-4）
 *
 * 关键对齐点:
 * - intValueNoise3D: JS 用 BigInt 避免溢出，C 用 uint32_t 位运算
 * - gradientNoise3D: 查 _randoms 表 + 点积 * 2.12
 * - gradientCoherentNoise3D: 8 角插值 + mapCubicSCurve(Medium)
 * - Perlin.getValue: fBm 多 octave 累加
 */

#include "noise.h"
#include <math.h>
#include <string.h>

// ============================================================================
// 常量 — 对齐 Utils.cs / 00_perlin.js
// ============================================================================

#define GENERATOR_NOISE_X  1619
#define GENERATOR_NOISE_Y  31337
#define GENERATOR_NOISE_Z  6971
#define GENERATOR_SEED     1013

#define QUALITY_LOW    0
#define QUALITY_MEDIUM 1
#define QUALITY_HIGH   2

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define DEG_TO_RAD (M_PI / 180.0)

// 对齐: Utils.cs:23-128 — 256 个 3D 随机向量（每 4 float，第 4 个是 0）
static const float g_randoms[256 * 4] = {
    -0.763874f, -0.596439f, -0.246489f, 0.0f, 0.396055f, 0.904518f, -0.158073f, 0.0f,
    -0.499004f, -0.8665f, -0.0131631f, 0.0f, 0.468724f, -0.824756f, 0.316346f, 0.0f,
    0.829598f, 0.43195f, 0.353816f, 0.0f, -0.454473f, 0.629497f, -0.630228f, 0.0f,
    -0.162349f, -0.869962f, -0.465628f, 0.0f, 0.932805f, 0.253451f, 0.256198f, 0.0f,
    -0.345419f, 0.927299f, -0.144227f, 0.0f, -0.715026f, -0.293698f, -0.634413f, 0.0f,
    -0.245997f, 0.717467f, -0.651711f, 0.0f, -0.967409f, -0.250435f, -0.037451f, 0.0f,
    0.901729f, 0.397108f, -0.170852f, 0.0f, 0.892657f, -0.0720622f, -0.444938f, 0.0f,
    0.0260084f, -0.0361701f, 0.999007f, 0.0f, 0.949107f, -0.19486f, 0.247439f, 0.0f,
    0.471803f, -0.807064f, -0.355036f, 0.0f, 0.879737f, 0.141845f, 0.453809f, 0.0f,
    0.570747f, 0.696415f, 0.435033f, 0.0f, -0.141751f, -0.988233f, -0.0574584f, 0.0f,
    -0.58219f, -0.0303005f, 0.812488f, 0.0f, -0.60922f, 0.239482f, -0.755975f, 0.0f,
    0.299394f, -0.197066f, -0.933557f, 0.0f, -0.851615f, -0.220702f, -0.47544f, 0.0f,
    0.848886f, 0.341829f, -0.403169f, 0.0f, -0.156129f, -0.687241f, 0.709453f, 0.0f,
    -0.665651f, 0.626724f, 0.405124f, 0.0f, 0.595914f, -0.674582f, 0.43569f, 0.0f,
    0.171025f, -0.509292f, 0.843428f, 0.0f, 0.78605f, 0.536414f, -0.307222f, 0.0f,
    0.18905f, -0.791613f, 0.581042f, 0.0f, -0.294916f, 0.844994f, 0.446105f, 0.0f,
    0.342031f, -0.58736f, -0.7335f, 0.0f, 0.57155f, 0.7869f, 0.232635f, 0.0f,
    0.885026f, -0.408223f, 0.223791f, 0.0f, -0.789518f, 0.571645f, 0.223347f, 0.0f,
    0.774571f, 0.31566f, 0.548087f, 0.0f, -0.79695f, -0.0433603f, -0.602487f, 0.0f,
    -0.142425f, -0.473249f, -0.869339f, 0.0f, -0.0698838f, 0.170442f, 0.982886f, 0.0f,
    0.687815f, -0.484748f, 0.540306f, 0.0f, 0.543703f, -0.534446f, -0.647112f, 0.0f,
    0.97186f, 0.184391f, -0.146588f, 0.0f, 0.707084f, 0.485713f, -0.513921f, 0.0f,
    0.942302f, 0.331945f, 0.043348f, 0.0f, 0.499084f, 0.599922f, 0.625307f, 0.0f,
    -0.289203f, 0.211107f, 0.9337f, 0.0f, 0.412433f, -0.71667f, -0.56239f, 0.0f,
    0.87721f, -0.082816f, 0.47291f, 0.0f, -0.420685f, -0.214278f, 0.881538f, 0.0f,
    0.752558f, -0.0391579f, 0.657361f, 0.0f, 0.0765725f, -0.996789f, 0.0234082f, 0.0f,
    -0.544312f, -0.309435f, -0.779727f, 0.0f, -0.455358f, -0.415572f, 0.787368f, 0.0f,
    -0.874586f, 0.483746f, 0.0330131f, 0.0f, 0.245172f, -0.0838623f, 0.965846f, 0.0f,
    0.382293f, -0.432813f, 0.81641f, 0.0f, -0.287735f, -0.905514f, 0.311853f, 0.0f,
    -0.667704f, 0.704955f, -0.239186f, 0.0f, 0.717885f, -0.464002f, -0.518983f, 0.0f,
    0.976342f, -0.214895f, 0.0240053f, 0.0f, -0.0733096f, -0.921136f, 0.382276f, 0.0f,
    -0.986284f, 0.151224f, -0.0661379f, 0.0f, -0.899319f, -0.429671f, 0.0812908f, 0.0f,
    0.652102f, -0.724625f, 0.222893f, 0.0f, 0.203761f, 0.458023f, -0.865272f, 0.0f,
    -0.030396f, 0.698724f, -0.714745f, 0.0f, -0.460232f, 0.839138f, 0.289887f, 0.0f,
    -0.0898602f, 0.837894f, 0.538386f, 0.0f, -0.731595f, 0.0793784f, 0.677102f, 0.0f,
    -0.447236f, -0.788397f, 0.422386f, 0.0f, 0.186481f, 0.645855f, -0.740335f, 0.0f,
    -0.259006f, 0.935463f, 0.240467f, 0.0f, 0.445839f, 0.819655f, -0.359712f, 0.0f,
    0.349962f, 0.755022f, -0.554499f, 0.0f, -0.997078f, -0.0359577f, 0.0673977f, 0.0f,
    -0.431163f, -0.147516f, -0.890133f, 0.0f, 0.299648f, -0.63914f, 0.708316f, 0.0f,
    0.397043f, 0.566526f, -0.722084f, 0.0f, -0.502489f, 0.438308f, -0.745246f, 0.0f,
    0.0687235f, 0.354097f, 0.93268f, 0.0f, -0.0476651f, -0.462597f, 0.885286f, 0.0f,
    -0.221934f, 0.900739f, -0.373383f, 0.0f, -0.956107f, -0.225676f, 0.186893f, 0.0f,
    -0.187627f, 0.391487f, -0.900852f, 0.0f, -0.224209f, -0.315405f, 0.92209f, 0.0f,
    -0.730807f, -0.537068f, 0.421283f, 0.0f, -0.0353135f, -0.816748f, 0.575913f, 0.0f,
    -0.941391f, 0.176991f, -0.287153f, 0.0f, -0.154174f, 0.390458f, 0.90762f, 0.0f,
    -0.283847f, 0.533842f, 0.796519f, 0.0f, -0.482737f, -0.850448f, 0.209052f, 0.0f,
    -0.649175f, 0.477748f, 0.591886f, 0.0f, 0.885373f, -0.405387f, -0.227543f, 0.0f,
    -0.147261f, 0.181623f, -0.972279f, 0.0f, 0.0959236f, -0.115847f, -0.988624f, 0.0f,
    -0.89724f, -0.191348f, 0.397928f, 0.0f, 0.903553f, -0.428461f, -0.00350461f, 0.0f,
    0.849072f, -0.295807f, -0.437693f, 0.0f, 0.65551f, 0.741754f, -0.141804f, 0.0f,
    0.61598f, -0.178669f, 0.767232f, 0.0f, 0.0112967f, 0.932256f, -0.361623f, 0.0f,
    -0.793031f, 0.258012f, 0.551845f, 0.0f, 0.421933f, 0.454311f, 0.784585f, 0.0f,
    -0.319993f, 0.0401618f, -0.946568f, 0.0f, -0.81571f, 0.551307f, -0.175151f, 0.0f,
    -0.377644f, 0.00322313f, 0.925945f, 0.0f, 0.129759f, -0.666581f, -0.734052f, 0.0f,
    0.601901f, -0.654237f, -0.457919f, 0.0f, -0.927463f, -0.0343576f, -0.372334f, 0.0f,
    -0.438663f, -0.868301f, -0.231578f, 0.0f, -0.648845f, -0.749138f, -0.133387f, 0.0f,
    0.507393f, -0.588294f, 0.629653f, 0.0f, 0.726958f, 0.623665f, 0.287358f, 0.0f,
    0.411159f, 0.367614f, -0.834151f, 0.0f, 0.806333f, 0.585117f, -0.0864016f, 0.0f,
    0.263935f, -0.880876f, 0.392932f, 0.0f, 0.421546f, -0.201336f, 0.884174f, 0.0f,
    -0.683198f, -0.569557f, -0.456996f, 0.0f, -0.117116f, -0.0406654f, -0.992285f, 0.0f,
    -0.643679f, -0.109196f, -0.757465f, 0.0f, -0.561559f, -0.62989f, 0.536554f, 0.0f,
    0.0628422f, 0.104677f, -0.992519f, 0.0f, 0.480759f, -0.2867f, -0.828658f, 0.0f,
    -0.228559f, -0.228965f, -0.946222f, 0.0f, -0.10194f, -0.65706f, -0.746914f, 0.0f,
    0.0689193f, -0.678236f, 0.731605f, 0.0f, 0.401019f, -0.754026f, 0.52022f, 0.0f,
    -0.742141f, 0.547083f, -0.387203f, 0.0f, -0.00210603f, -0.796417f, -0.604745f, 0.0f,
    0.296725f, -0.409909f, -0.862513f, 0.0f, -0.260932f, -0.798201f, 0.542945f, 0.0f,
    -0.641628f, 0.742379f, 0.192838f, 0.0f, -0.186009f, -0.101514f, 0.97729f, 0.0f,
    0.106711f, -0.962067f, 0.251079f, 0.0f, -0.743499f, 0.30988f, -0.592607f, 0.0f,
    -0.795853f, -0.605066f, -0.0226607f, 0.0f, -0.828661f, -0.419471f, -0.370628f, 0.0f,
    0.0847218f, -0.489815f, -0.8677f, 0.0f, -0.381405f, 0.788019f, -0.483276f, 0.0f,
    0.282042f, -0.953394f, 0.107205f, 0.0f, 0.530774f, 0.847413f, 0.0130696f, 0.0f,
    0.0515397f, 0.922524f, 0.382484f, 0.0f, -0.631467f, -0.709046f, 0.313852f, 0.0f,
    0.688248f, 0.517273f, 0.508668f, 0.0f, 0.646689f, -0.333782f, -0.685845f, 0.0f,
    -0.932528f, -0.247532f, -0.262906f, 0.0f, 0.630609f, 0.68757f, -0.359973f, 0.0f,
    0.577805f, -0.394189f, 0.714673f, 0.0f, -0.887833f, -0.437301f, -0.14325f, 0.0f,
    0.690982f, 0.174003f, 0.701617f, 0.0f, -0.866701f, 0.0118182f, 0.498689f, 0.0f,
    -0.482876f, 0.727143f, 0.487949f, 0.0f, -0.577567f, 0.682593f, -0.447752f, 0.0f,
    0.373768f, 0.0982991f, 0.922299f, 0.0f, 0.170744f, 0.964243f, -0.202687f, 0.0f,
    0.993654f, -0.035791f, -0.106632f, 0.0f, 0.587065f, 0.4143f, -0.695493f, 0.0f,
    -0.396509f, 0.26509f, -0.878924f, 0.0f, -0.0866853f, 0.83553f, -0.542563f, 0.0f,
    0.923193f, 0.133398f, -0.360443f, 0.0f, 0.00379108f, -0.258618f, 0.965972f, 0.0f,
    0.239144f, 0.245154f, -0.939526f, 0.0f, 0.758731f, -0.555871f, 0.33961f, 0.0f,
    0.295355f, 0.309513f, 0.903862f, 0.0f, 0.0531222f, -0.91003f, -0.411124f, 0.0f,
    0.270452f, 0.0229439f, -0.96246f, 0.0f, 0.563634f, 0.0324352f, 0.825387f, 0.0f,
    0.156326f, 0.147392f, 0.976646f, 0.0f, -0.0410141f, 0.981824f, 0.185309f, 0.0f,
    -0.385562f, -0.576343f, -0.720535f, 0.0f, 0.388281f, 0.904441f, 0.176702f, 0.0f,
    0.945561f, -0.192859f, -0.262146f, 0.0f, 0.844504f, 0.520193f, 0.127325f, 0.0f,
    0.0330893f, 0.999121f, -0.0257505f, 0.0f, -0.592616f, -0.482475f, -0.644999f, 0.0f,
    0.539471f, 0.631024f, -0.557476f, 0.0f, 0.655851f, -0.027319f, -0.754396f, 0.0f,
    0.274465f, 0.887659f, 0.369772f, 0.0f, -0.123419f, 0.975177f, -0.183842f, 0.0f,
    -0.223429f, 0.708045f, 0.66989f, 0.0f, -0.908654f, 0.196302f, 0.368528f, 0.0f,
    -0.95759f, -0.00863708f, 0.288005f, 0.0f, 0.960535f, 0.030592f, 0.276472f, 0.0f,
    -0.413146f, 0.907537f, 0.0754161f, 0.0f, -0.847992f, 0.350849f, -0.397259f, 0.0f,
    0.614736f, 0.395841f, 0.68221f, 0.0f, -0.503504f, -0.666128f, -0.550234f, 0.0f,
    -0.268833f, -0.738524f, -0.618314f, 0.0f, 0.792737f, -0.60001f, -0.107502f, 0.0f,
    -0.637582f, 0.508144f, -0.579032f, 0.0f, 0.750105f, 0.282165f, -0.598101f, 0.0f,
    -0.351199f, -0.392294f, -0.850155f, 0.0f, 0.250126f, -0.960993f, -0.118025f, 0.0f,
    -0.732341f, 0.680909f, -0.0063274f, 0.0f, -0.760674f, -0.141009f, 0.633634f, 0.0f,
    0.222823f, -0.304012f, 0.926243f, 0.0f, 0.209178f, 0.505671f, 0.836984f, 0.0f,
    0.757914f, -0.56629f, -0.323857f, 0.0f, -0.782926f, -0.339196f, 0.52151f, 0.0f,
    -0.462952f, 0.585565f, 0.665424f, 0.0f, 0.61879f, 0.194119f, -0.761194f, 0.0f,
    0.741388f, -0.276743f, 0.611357f, 0.0f, 0.707571f, 0.702621f, 0.0752872f, 0.0f,
    0.156562f, 0.819977f, 0.550569f, 0.0f, -0.793606f, 0.440216f, 0.42f, 0.0f,
    0.234547f, 0.885309f, -0.401517f, 0.0f, 0.132598f, 0.80115f, -0.58359f, 0.0f,
    -0.377899f, -0.639179f, 0.669808f, 0.0f, -0.865993f, -0.396465f, 0.304748f, 0.0f,
    -0.624815f, -0.44283f, 0.643046f, 0.0f, -0.485705f, 0.825614f, -0.287146f, 0.0f,
    -0.971788f, 0.175535f, 0.157529f, 0.0f, -0.456027f, 0.392629f, 0.798675f, 0.0f,
    -0.0104443f, 0.521623f, -0.853112f, 0.0f, -0.660575f, -0.74519f, 0.091282f, 0.0f,
    -0.0157698f, -0.307475f, -0.951425f, 0.0f, -0.603467f, -0.250192f, 0.757121f, 0.0f,
    0.506876f, 0.25006f, 0.824952f, 0.0f, 0.255404f, 0.966794f, 0.00884498f, 0.0f,
    0.466764f, -0.874228f, -0.133625f, 0.0f, 0.475077f, -0.0682351f, -0.877295f, 0.0f,
    -0.224967f, -0.938972f, -0.260233f, 0.0f, -0.377929f, -0.814757f, -0.439705f, 0.0f,
    -0.305847f, 0.542333f, -0.782517f, 0.0f, 0.26658f, -0.902905f, -0.337191f, 0.0f,
    0.0275773f, 0.322158f, -0.946284f, 0.0f, 0.0185422f, 0.716349f, 0.697496f, 0.0f,
    -0.20483f, 0.978416f, 0.0273371f, 0.0f, -0.898276f, 0.373969f, 0.230752f, 0.0f,
    -0.00909378f, 0.546594f, 0.837349f, 0.0f, 0.6602f, -0.751089f, 0.000959236f, 0.0f,
    0.855301f, -0.303056f, 0.420259f, 0.0f, 0.797138f, 0.0623013f, -0.600574f, 0.0f,
    0.48947f, -0.866813f, 0.0951509f, 0.0f, 0.251142f, 0.674531f, 0.694216f, 0.0f,
    -0.578422f, -0.737373f, -0.348867f, 0.0f, -0.254689f, -0.514807f, 0.818601f, 0.0f,
    0.374972f, 0.761612f, 0.528529f, 0.0f, 0.640303f, -0.734271f, -0.225517f, 0.0f,
    -0.638076f, 0.285527f, 0.715075f, 0.0f, 0.772956f, -0.15984f, -0.613995f, 0.0f,
    0.798217f, -0.590628f, 0.118356f, 0.0f, -0.986276f, -0.0578337f, -0.154644f, 0.0f,
    -0.312988f, -0.94549f, 0.0899272f, 0.0f, -0.497338f, 0.178325f, 0.849032f, 0.0f,
    -0.101136f, -0.981014f, 0.165477f, 0.0f, -0.521688f, 0.0553434f, -0.851339f, 0.0f,
    -0.786182f, -0.583814f, 0.202678f, 0.0f, -0.565191f, 0.821858f, -0.0714658f, 0.0f,
    0.437895f, 0.152598f, -0.885981f, 0.0f, -0.92394f, 0.353436f, -0.14635f, 0.0f,
    0.212189f, -0.815162f, -0.538969f, 0.0f, -0.859262f, 0.143405f, -0.491024f, 0.0f,
    0.991353f, 0.112814f, 0.0670273f, 0.0f, 0.0337884f, -0.979891f, -0.196654f, 0.0f
};

// ============================================================================
// NoiseUtils — 对齐 00_perlin.js
// ============================================================================

// 对齐: Utils.cs:216-219 — mapCubicSCurve
static inline double map_cubic_s_curve(double value) {
    return value * value * (3.0 - 2.0 * value);
}

// 对齐: Utils.cs:221-227 — mapQuinticSCurve
static inline double map_quintic_s_curve(double value) {
    double a3 = value * value * value;
    double a4 = a3 * value;
    double a5 = a4 * value;
    return 6.0 * a5 - 15.0 * a4 + 10.0 * a3;
}

// 对齐: Utils.cs:198-201 — interpolateLinear
static inline double interpolate_linear(double a, double b, double pos) {
    return (1.0 - pos) * a + pos * b;
}

// 对齐: Utils.cs:203-214 — makeInt32Range
static inline double make_int32_range(double value) {
    if (value >= 1073741824.0) {
        // IEEE remainder: x - y * round(x / y)
        double rem = value - 1073741824.0 * round(value / 1073741824.0);
        return 2.0 * rem - 1073741824.0;
    }
    if (value <= -1073741824.0) {
        double rem = value - 1073741824.0 * round(value / 1073741824.0);
        return 2.0 * rem + 1073741824.0;
    }
    return value;
}

// 对齐: Utils.cs:176-188 — gradientNoise3D
static double gradient_noise_3d(double fx, double fy, double fz,
                                int ix, int iy, int iz, int seed) {
    // 对齐: Utils.cs:178-180
    // JS: let n = ((NOISE_X * ix + NOISE_Y * iy + NOISE_Z * iz + SEED * seed) & 0xFFFFFFFF) >>> 0;
    // C: 用 uint32_t 自然截断到 32 位
    uint32_t n = (uint32_t)(GENERATOR_NOISE_X * ix + GENERATOR_NOISE_Y * iy +
                            GENERATOR_NOISE_Z * iz + GENERATOR_SEED * seed);
    n ^= (n >> 8);
    n &= 0xFF;

    // 对齐: Utils.cs:181-183
    int idx = (int)n << 2;
    double gx = (double)g_randoms[idx];
    double gy = (double)g_randoms[idx + 1];
    double gz = (double)g_randoms[idx + 2];

    // 对齐: Utils.cs:184-187
    double dx = fx - (double)ix;
    double dy = fy - (double)iy;
    double dz = fz - (double)iz;

    return (gx * dx + gy * dy + gz * dz) * 2.12;
}

// 对齐: Utils.cs:130-174 — gradientCoherentNoise3D
static double gradient_coherent_noise_3d(double x, double y, double z,
                                         int seed, int quality) {
    // 对齐: Utils.cs:132-137
    // C# 的 (int)x 是截断(向零)
    // 对于正数: x=1.5 → (int)1.5=1, x0=1
    // 对于负数: x=-0.5 → (int)-0.5=0, x0=0-1=-1
    int x0 = (x > 0.0) ? (int)x : (int)x - 1;
    int x1 = x0 + 1;
    int y0 = (y > 0.0) ? (int)y : (int)y - 1;
    int y1 = y0 + 1;
    int z0 = (z > 0.0) ? (int)z : (int)z - 1;
    int z1 = z0 + 1;

    double xs, ys, zs;

    // 对齐: Utils.cs:141-158
    switch (quality) {
        case QUALITY_LOW:
            xs = x - x0;
            ys = y - y0;
            zs = z - z0;
            break;
        case QUALITY_MEDIUM:
            xs = map_cubic_s_curve(x - x0);
            ys = map_cubic_s_curve(y - y0);
            zs = map_cubic_s_curve(z - z0);
            break;
        case QUALITY_HIGH:
            xs = map_quintic_s_curve(x - x0);
            ys = map_quintic_s_curve(y - y0);
            zs = map_quintic_s_curve(z - z0);
            break;
        default:
            xs = map_cubic_s_curve(x - x0);
            ys = map_cubic_s_curve(y - y0);
            zs = map_cubic_s_curve(z - z0);
            break;
    }

    // 对齐: Utils.cs:159-173 — 8 角插值
    double n000 = gradient_noise_3d(x, y, z, x0, y0, z0, seed);
    double n100 = gradient_noise_3d(x, y, z, x1, y0, z0, seed);
    double n010 = gradient_noise_3d(x, y, z, x0, y1, z0, seed);
    double n110 = gradient_noise_3d(x, y, z, x1, y1, z0, seed);
    double n001 = gradient_noise_3d(x, y, z, x0, y0, z1, seed);
    double n101 = gradient_noise_3d(x, y, z, x1, y0, z1, seed);
    double n011 = gradient_noise_3d(x, y, z, x0, y1, z1, seed);
    double n111 = gradient_noise_3d(x, y, z, x1, y1, z1, seed);

    // 三线性插值
    double nx00 = interpolate_linear(n000, n100, xs);
    double nx10 = interpolate_linear(n010, n110, xs);
    double nx01 = interpolate_linear(n001, n101, xs);
    double nx11 = interpolate_linear(n011, n111, xs);

    double nxy0 = interpolate_linear(nx00, nx10, ys);
    double nxy1 = interpolate_linear(nx01, nx11, ys);

    return interpolate_linear(nxy0, nxy1, zs);
}

// ============================================================================
// Perlin fBm — 对齐 Perlin.cs:148-186 / 00_perlin.js Perlin.GetValue
// ============================================================================

static double perlin_get_value(double x, double y, double z,
                               double frequency, int seed,
                               double lacunarity, double persistence,
                               int octave_count, int normalized, int invert,
                               int quality) {
    double value = 0.0;
    double cur_persistence = 1.0;

    // 对齐: Perlin.cs:153-155
    x *= frequency;
    y *= frequency;
    z *= frequency;

    // 对齐: Perlin.cs:156-168
    for (int i = 0; i < octave_count; i++) {
        double nx = make_int32_range(x);
        double ny = make_int32_range(y);
        double nz = make_int32_range(z);

        int cur_seed = (seed + i) & 0xFFFFFFFF;

        double signal = gradient_coherent_noise_3d(nx, ny, nz, cur_seed, quality);
        value += signal * cur_persistence;

        x *= lacunarity;
        y *= lacunarity;
        z *= lacunarity;
        cur_persistence *= persistence;
    }

    // 对齐: Perlin.cs:169-180
    if (normalized) {
        value = value * 0.5 + 0.5;
        if (value < 0.0) value = 0.0;
        if (value > 1.0) value = 1.0;
    }

    // 对齐: Perlin.cs:181-184
    if (invert) {
        value = 1.0 - value;
    }

    return value;
}

// ============================================================================
// eval_node — 递归求值 noise 树
// ============================================================================

static double eval_node(const NoiseNode *nodes, int node_count, int idx,
                        double x, double y, double z);

static double eval_node(const NoiseNode *nodes, int node_count, int idx,
                        double x, double y, double z) {
    if (idx < 0 || idx >= node_count) return 0.0;

    const NoiseNode *n = &nodes[idx];

    switch (n->type) {
    case NOISE_PERLIN: {
        double freq       = (double)n->params[0];
        double lacunarity = (double)n->params[1];
        double persistence= (double)n->params[2];
        int    octaves    = (int)n->params[3];
        int    seed       = (int)n->params[4];
        int    quality    = (int)n->params[5];
        int    normalized = (int)n->params[6];
        int    invert     = (int)n->params[7];
        return perlin_get_value(x, y, z, freq, seed, lacunarity, persistence,
                                octaves, normalized, invert, quality);
    }

    case NOISE_SCALE_BIAS: {
        double scale = (double)n->params[0];
        double bias  = (double)n->params[1];
        double v = eval_node(nodes, node_count, n->children[0], x, y, z);
        return v * scale + bias;
    }

    case NOISE_CONST: {
        return (double)n->params[0];
    }

    case NOISE_MULTIPLY: {
        double a = eval_node(nodes, node_count, n->children[0], x, y, z);
        double b = eval_node(nodes, node_count, n->children[1], x, y, z);
        return a * b;
    }

    case NOISE_DISPLACE: {
        // children[0]=source, children[1]=dispX, children[2]=dispZ
        // 对齐 JS Displace.getValue:
        //   x2 = x + modules[1].getValue(x, y, z)
        //   y2 = y  (no Y displacement in practice)
        //   z2 = z + modules[3].getValue(x, y, z)
        double dx = eval_node(nodes, node_count, n->children[1], x, y, z);
        double dz = eval_node(nodes, node_count, n->children[2], x, y, z);
        return eval_node(nodes, node_count, n->children[0], x + dx, y, z + dz);
    }

    case NOISE_CLAMP: {
        double mn = (double)n->params[0];
        double mx = (double)n->params[1];
        double v = eval_node(nodes, node_count, n->children[0], x, y, z);
        if (v < mn) return mn;
        if (v > mx) return mx;
        return v;
    }

    case NOISE_TRANSLATE: {
        double tx = (double)n->params[0];
        double ty = (double)n->params[1];
        double tz = (double)n->params[2];
        return eval_node(nodes, node_count, n->children[0],
                         x + tx, y + ty, z + tz);
    }

    case NOISE_ROTATE: {
        // 对齐 Rotate.cs:86-114 — 预计算旋转矩阵，应用到坐标
        double rx = (double)n->params[0] * DEG_TO_RAD;
        double ry = (double)n->params[1] * DEG_TO_RAD;
        double rz = (double)n->params[2] * DEG_TO_RAD;

        double cos_x = cos(rx), sin_x = sin(rx);
        double cos_y = cos(ry), sin_y = sin(ry);
        double cos_z = cos(rz), sin_z = sin(rz);

        // 对齐: Rotate.cs:94-102
        double x1m = sin_y * sin_x * sin_z + cos_y * cos_z;
        double y1m = cos_x * sin_z;
        double z1m = sin_y * cos_z - cos_y * sin_x * sin_z;

        double x2m = sin_y * sin_x * cos_z - cos_y * sin_z;
        double y2m = cos_x * cos_z;
        double z2m = -cos_y * sin_x * cos_z - sin_y * sin_z;

        double x3m = -sin_y * cos_x;
        double y3m = sin_x;
        double z3m = cos_y * cos_x;

        double nx = x1m * x + y1m * y + z1m * z;
        double ny = x2m * x + y2m * y + z2m * z;
        double nz = x3m * x + y3m * y + z3m * z;

        return eval_node(nodes, node_count, n->children[0], nx, ny, nz);
    }

    case NOISE_DIST_FROM_AXIS_DIRECTIONAL: {
        // 对齐: DistFromAxis_Directional.cs — return x / span
        double span = (double)n->params[0];
        return x / span;
    }

    case NOISE_DIST_FROM_POINT: {
        // 对齐: DistFromPoint.cs — return sqrt(x*x + z*z) / span
        double span = (double)n->params[0];
        return sqrt(x * x + z * z) / span;
    }

    case NOISE_POWER: {
        double base = eval_node(nodes, node_count, n->children[0], x, y, z);
        double exp  = eval_node(nodes, node_count, n->children[1], x, y, z);
        return pow(base, exp);
    }

    case NOISE_SCALE: {
        // 对齐: Scale.cs — getValue(x * scaleX, y * scaleY, z * scaleZ)
        double sx = (double)n->params[0];
        double sy = (double)n->params[1];
        double sz = (double)n->params[2];
        return eval_node(nodes, node_count, n->children[0],
                         x * sx, y * sy, z * sz);
    }

    case NOISE_DIST_FROM_AXIS: {
        // 对齐: DistFromAxis.cs — return abs(x) / span
        double span = (double)n->params[0];
        return fabs(x) / span;
    }

    default:
        return 0.0;
    }
}

// ============================================================================
// noise_fill_grid — 填满整个 grid
// ============================================================================

void noise_fill_grid(const NoiseNode *nodes, int node_count, int root_idx,
                     int width, int height, float *out) {
    for (int z = 0; z < height; z++) {
        for (int x = 0; x < width; x++) {
            out[z * width + x] = (float)eval_node(nodes, node_count, root_idx,
                                                   (double)x, 0.0, (double)z);
        }
    }
}
