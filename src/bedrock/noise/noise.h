/*
 * noise.h - C 层噪声引擎
 *
 * 职责: 提供高性能 Perlin/NoiseUtils 噪声计算，JS 侧构建参数后一次性调 C 填满 Float32Array
 *
 * 设计: noise 树由 NoiseNode 数组描述，每个节点有类型、参数、子节点索引
 *       noise_fill_grid 递归求值 noise 树，对每个 cell (x,z) 写入 out[]
 */

#ifndef NOISE_H
#define NOISE_H

#include <stdint.h>

// 噪声模块类型（对齐 JS ModuleBase 子类）
typedef enum {
    NOISE_PERLIN = 0,
    NOISE_SCALE_BIAS,
    NOISE_DISPLACE,
    NOISE_MULTIPLY,
    NOISE_CONST,
    NOISE_CLAMP,
    NOISE_TRANSLATE,
    NOISE_ROTATE,
    NOISE_DIST_FROM_AXIS_DIRECTIONAL,
    NOISE_DIST_FROM_POINT,
    NOISE_POWER,
    NOISE_SCALE,            // 坐标缩放: getValue(x*sx, y*sy, z*sz)
    NOISE_DIST_FROM_AXIS,   // abs(x) / span (非方向性)
} NoiseType;

// 最多 3 个子模块（对齐 Displace 的 source/dispX/dispZ 三路）
#define NOISE_MAX_CHILDREN 3
#define NOISE_MAX_NODES    64   // 一棵 noise 树不超过 64 个节点

typedef struct {
    NoiseType type;
    float     params[8];                    // 各类型自用参数
    int       children[NOISE_MAX_CHILDREN]; // 子节点 index（-1 = 无）
} NoiseNode;

/*
 * params 布局（对齐 JS 构造函数参数顺序）:
 *
 * NOISE_PERLIN:
 *   [0] frequency, [1] lacunarity, [2] persistence, [3] octaves (int),
 *   [4] seed (int), [5] quality (int), [6] normalized (bool), [7] invert (bool)
 *
 * NOISE_SCALE_BIAS:
 *   [0] scale, [1] bias
 *   children[0] = source
 *
 * NOISE_CONST:
 *   [0] value
 *
 * NOISE_MULTIPLY:
 *   children[0] = lhs, children[1] = rhs
 *
 * NOISE_DISPLACE:
 *   children[0] = source, children[1] = dispX, children[2] = dispZ
 *   (Y displacement 忽略，对齐 JS: y = Const(0) 被跳过)
 *
 * NOISE_TRANSLATE:
 *   [0] tx, [1] ty, [2] tz
 *   children[0] = source
 *
 * NOISE_ROTATE:
 *   [0] rx, [1] ry, [2] rz (度)
 *   children[0] = source
 *   注: C 侧预计算旋转矩阵在 eval 时按需计算
 *
 * NOISE_DIST_FROM_AXIS_DIRECTIONAL:
 *   [0] halfSize (span)
 *
 * NOISE_DIST_FROM_POINT:
 *   [0] radius (span)
 *
 * NOISE_CLAMP:
 *   [0] min, [1] max
 *   children[0] = source
 *
 * NOISE_POWER:
 *   children[0] = base, children[1] = exponent
 *
 * NOISE_SCALE:
 *   [0] scaleX, [1] scaleY, [2] scaleZ
 *   children[0] = source
 *   对齐: Scale.cs — getValue(x*sx, y*sy, z*sz)
 *
 * NOISE_DIST_FROM_AXIS:
 *   [0] span
 *   对齐: DistFromAxis.cs — return abs(x) / span
 */

// 填满整个 grid（width × height 个 float）
// nodes[]  = noise 树节点数组，root_idx = 根节点下标
// out[]    = 调用方分配的 float 数组，长度 = width * height
void noise_fill_grid(const NoiseNode *nodes, int node_count, int root_idx,
                     int width, int height, float *out);

#endif // NOISE_H
