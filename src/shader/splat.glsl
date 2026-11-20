// Portions are ported from ink-studio by siliconjungle (MIT, Copyright (c) 2026 siliconjungle;
// full notice in src/bedrock/splat/LICENSE.ink-studio): the palette resolve is src/pixel-look.js
// PixelLook.glsl, the projection and falloff follow cli/render.js.
// splat.glsl — Gaussian splat rendering and palette pixel resolve.
//
// Programs:
//   splat    — one instanced quad per splat; the vertex stage projects the splat's 3D
//              covariance to a screen-space ellipse (orthographic view), the fragment stage
//              evaluates the Gaussian. Two colour targets: 0 = colour (blended by alpha),
//              1 = normalised depth in .r (blended by alpha for solid splats only); .g and .b
//              carry the own palette of the splats that drew the pixel (see fs_splat).
//   resolve  — palette resolve of a 2x samples target into the art grid: 2x2 palette
//              vote, ordered dither on the native picture, body-coloured outer contour. A pixel
//              drawn by splats with an own palette is resolved against that palette.
//   blit     — nearest-neighbour copy of a target into a viewport.
//
// Pixel coordinates in every pass are "y-up": row 0 is the lowest screen row. Storage rows
// differ per backend (origin_top_left), so passes convert with u_flip where they address
// texels or compute ordered-dither phases.

@ctype mat4 Splat_Mat4
@ctype vec4 Splat_Vec4

@block splat_project
// Projects one splat (world position, density, covariance, packed colour) into the target and
// fills the flat outputs. Uniforms: view, cam, dep, zr (declared by the including stage).
void splat_project(int corner, vec3 pos, float al, float c00, float c01, float c02, float c11, float c12, float c22, float pk) {
    vec2 sgn = vec2((corner & 1) != 0 ? 1.0 : -1.0, (corner & 2) != 0 ? 1.0 : -1.0);
    vec3 xa = vec3(view[0][0], view[1][0], view[2][0]);
    vec3 ya = vec3(view[0][1], view[1][1], view[2][1]);
    vec3 za = vec3(view[0][2], view[1][2], view[2][2]);
    float pz = dot(za, pos) + view[3][2];
    float focal = dep.z;
    vec3 a = focal * xa;
    vec3 b = focal * ya;
    vec3 u = vec3(c00 * a.x + c01 * a.y + c02 * a.z, c01 * a.x + c11 * a.y + c12 * a.z, c02 * a.x + c12 * a.y + c22 * a.z);
    vec3 w = vec3(c00 * b.x + c01 * b.y + c02 * b.z, c01 * b.x + c11 * b.y + c12 * b.z, c02 * b.x + c12 * b.y + c22 * b.z);
    float A = dot(a, u) + 0.15;
    float B = dot(b, u);
    float C = dot(b, w) + 0.15;
    float det = A * C - B * B;
    if (al <= 0.0 || pz > -dep.w || det <= 0.0) {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        v_rel = vec2(0.0); v_conic = vec3(0.0); v_col = vec4(0.0); v_dn = 1.0;
        return;
    }
    // how many sigmas out the fragment stage can still draw: it drops everything past 3 sigma
    // (pw < -4.5), and a solid splat (al >= 1) wherever al * exp(pw) <= 0.38, where its
    // smoothstep is 0. Solid splats are most of a world and end well inside 3 sigma (2.17 at
    // density 4), so their quads are that much smaller and the fill with them.
    float k = al >= 1.0 ? min(3.0, sqrt(2.0 * log(al / 0.38))) : 3.0;
    vec2 ext = vec2(min(k * sqrt(A), 2048.0), min(k * sqrt(C), 2048.0));
    vec2 rel = focal * vec2(dot(xa, pos), dot(ya, pos));
    // the quad is a little larger than that so its edge never decides a fragment: the cuts in
    // the fragment stage do
    vec2 corner_px = rel + cam.xy + sgn * (ext * 1.02 + 0.5);
    vec2 ndc = corner_px / cam.zw * 2.0 - 1.0;
    // hardware depth: farther = larger, over the whole scene depth range
    float zd = clamp((-pz - zr.x) * zr.y - zr.w, 0.0, 1.0);
    gl_Position = vec4(ndc, zd, 1.0);
    v_rel = rel;
    v_conic = vec3(C / det, -B / det, A / det);
    v_col = vec4(mod(pk, 256.0) / 255.0, mod(floor(pk / 256.0), 256.0) / 255.0, floor(pk / 65536.0) / 255.0, al);
    v_dn = clamp((-pz - (dep.x - dep.y)) / (2.0 * dep.y), 0.0, 1.0);
}
@end

@vs vs_splat
layout(binding=0) uniform vs_splat_params {
    mat4 view;      // world -> view (column-major); only its rotation rows are used for x/y
    vec4 cam;       // x = screen offset Cx, y = screen offset Cy (target px), z = target width px, w = target height px
    vec4 dep;       // x = eye distance, y = depth radius, z = focal px, w = near
    vec4 zr;        // x = hardware depth near (view distance), y = 1 / depth range, w = how much nearer these
                    // splats are tested and written than they are (share of the depth range)
};

in vec4 a_pos;   // x y z density
in vec4 a_cov0;  // c00 c01 c02 packed rgb
in vec4 a_cov1;  // c11 c12 c22 kind

// Everything the fragment stage needs is constant per splat (flat): the fragment's offset from
// the centre is formed from gl_FragCoord there, so a view moved by whole pixels reproduces
// every fragment bit for bit.
flat out vec2 v_rel;    // splat centre minus the screen offset, target px (y-up)
flat out vec3 v_conic;
flat out vec4 v_col;
flat out float v_dn;

@include_block splat_project

void main() {
    splat_project(gl_VertexIndex, a_pos.xyz, a_pos.w, a_cov0.x, a_cov0.y, a_cov0.z, a_cov1.x, a_cov1.y, a_cov1.z, a_cov0.w);
}
@end

@vs vs_splat_skin
layout(binding=0) uniform vs_splat_params {
    mat4 view;
    vec4 cam;
    vec4 dep;
    vec4 zr;        // z = bone texture row of this instance
};
@image_sample_type bone_tex unfilterable_float
layout(binding=0) uniform texture2D bone_tex;
@sampler_type bone_smp nonfiltering
layout(binding=0) uniform sampler bone_smp;

in vec4 a_pos;   // x y z density (rest pose)
in vec4 a_cov0;  // c00 c01 c02 packed rgb
in vec4 a_cov1;  // c11 c12 c22 kind
in vec4 a_bone;  // bone, second bone, second bone share, unused

flat out vec2 v_rel;
flat out vec3 v_conic;
flat out vec4 v_col;
flat out float v_dn;

@include_block splat_project

// bone b of this instance: rows of [R | t]
void bone_rows(int b, out vec4 r0, out vec4 r1, out vec4 r2) {
    int row = int(zr.z);
    r0 = texelFetch(sampler2D(bone_tex, bone_smp), ivec2(b * 3, row), 0);
    r1 = texelFetch(sampler2D(bone_tex, bone_smp), ivec2(b * 3 + 1, row), 0);
    r2 = texelFetch(sampler2D(bone_tex, bone_smp), ivec2(b * 3 + 2, row), 0);
}

void main() {
    vec4 r0, r1, r2;
    bone_rows(int(a_bone.x), r0, r1, r2);
    vec4 p = vec4(a_pos.xyz, 1.0);
    vec3 x = vec3(dot(r0, p), dot(r1, p), dot(r2, p));
    if (a_bone.z > 0.0) {
        vec4 s0, s1, s2;
        bone_rows(int(a_bone.y), s0, s1, s2);
        x += (vec3(dot(s0, p), dot(s1, p), dot(s2, p)) - x) * a_bone.z;
    }
    // shape follows the first bone: R S R^T (as K.bones.posed)
    mat3 S = mat3(a_cov0.x, a_cov0.y, a_cov0.z, a_cov0.y, a_cov1.x, a_cov1.y, a_cov0.z, a_cov1.y, a_cov1.z);
    mat3 R = transpose(mat3(r0.xyz, r1.xyz, r2.xyz));
    mat3 Sp = R * S * transpose(R);
    splat_project(gl_VertexIndex, x, a_pos.w, Sp[0][0], Sp[0][1], Sp[0][2], Sp[1][1], Sp[1][2], Sp[2][2], a_cov0.w);
}
@end

@fs fs_splat
layout(binding=1) uniform fs_splat_params {
    vec4 frag;      // x = screen offset Cx, y = Cy, z = target height px, w = 1 if storage origin is top-left
    vec4 mode;      // x = 1: opaque cut (alpha >= 0.5 drawn whole, the rest dropped), for splats drawn out of order;
                    // y = own palette of these splats (0 = the scene's)
    vec4 tint;      // rgb = colour the splats are pulled towards, a = how far (0 = their own colour)
};
flat in vec2 v_rel;
flat in vec3 v_conic;
flat in vec4 v_col;
flat in float v_dn;
layout(location=0) out vec4 frag_color;
layout(location=1) out vec4 frag_depth;

void main() {
    vec2 fc = vec2(gl_FragCoord.x, frag.w > 0.5 ? frag.z - gl_FragCoord.y : gl_FragCoord.y);
    vec2 d = (fc - frag.xy) - v_rel;
    float dx = d.x, dy = d.y;
    float pw = -0.5 * (v_conic.x * dx * dx + v_conic.z * dy * dy) - v_conic.y * dx * dy;
    if (pw > 0.0 || pw < -4.5) discard;
    float al = v_col.a;
    float gg = al * exp(pw);
    float a = min(0.99, gg);
    bool solid = al >= 1.0;
    if (solid) a = 0.99 * smoothstep(0.38, 0.62, gg);
    if (a < 0.004) discard;
    if (mode.x > 0.5) {
        if (a < 0.5) discard;
        a = 1.0;
    }
    frag_color = vec4(mix(min(v_col.rgb, vec3(1.0)), tint.rgb, tint.a), a);
    // .g = own palette number in steps of 8/255, .b = 1 for an own palette: blended like the depth, so
    // .b is the share of the pixel that splats with an own palette drew and .g / .b their palette
    frag_depth = vec4(v_dn, mode.y * (8.0 / 255.0), mode.y > 0.0 ? 1.0 : 0.0, solid ? a : 0.0);
}
@end

@program splat vs_splat fs_splat
@program splat_skin vs_splat_skin fs_splat

@vs vs_quad
layout(binding=0) uniform vs_quad_params {
    vec4 quad;   // x = 1 if texture storage origin is top-left
};
out vec2 v_uv;
void main() {
    int i = gl_VertexIndex;
    vec2 ndc = vec2((i & 1) != 0 ? 1.0 : -1.0, (i & 2) != 0 ? 1.0 : -1.0);
    gl_Position = vec4(ndc, 0.5, 1.0);
    vec2 uv = ndc * 0.5 + 0.5;
    v_uv = vec2(uv.x, quad.x > 0.5 ? 1.0 - uv.y : uv.y);
}
@end

@fs fs_blit
layout(binding=0) uniform texture2D blit_tex;
layout(binding=0) uniform sampler blit_smp;
@image_sample_type blit_depth unfilterable_float
layout(binding=1) uniform texture2D blit_depth;
@sampler_type blit_dsmp nonfiltering
layout(binding=1) uniform sampler blit_dsmp;
// occluders: things the host drew before the composite (e.g. 2D characters). Inside an
// occluder's rectangle (art pixels, y-up), splat pixels farther than the occluder's depth are
// dropped so the host's drawing shows through; nearer ones cover it.
layout(binding=1) uniform fs_blit_params {
    vec4 occ_rect[32];    // x0, y0, x1, y1 in art pixels (y-up)
    vec4 occ_depth[32];   // x = normalised depth of the occluder's foot
    vec4 occ_info;        // x = count, y = art height, z = 1 if storage origin is top-left, w = samples height
    vec4 blit_opts;       // x = 1: the copy covers what is behind it whatever the target's alpha
};
in vec2 v_uv;
out vec4 frag_color;
void main() {
    vec4 c = texture(sampler2D(blit_tex, blit_smp), v_uv);
    int n = int(occ_info.x);
    if (n > 0 && c.a > 0.0) {
        vec2 art = vec2(textureSize(sampler2D(blit_tex, blit_smp), 0));
        // art pixel under this fragment, y-up
        vec2 a = vec2(v_uv.x, occ_info.z > 0.5 ? 1.0 - v_uv.y : v_uv.y) * art;
        ivec2 ap = ivec2(floor(a));
        float d = 1.0;
        for (int j = 0; j < 4; j++) {
            ivec2 q = ap * 2 + ivec2(j & 1, j >> 1);
            q.y = occ_info.z > 0.5 ? int(occ_info.w) - 1 - q.y : q.y;
            d = min(d, texelFetch(sampler2D(blit_depth, blit_dsmp), q, 0).r);
        }
        for (int i = 0; i < 32; i++) {
            if (i >= n) break;
            vec4 r = occ_rect[i];
            if (a.x >= r.x && a.x < r.z && a.y >= r.y && a.y < r.w) {
                float od = occ_depth[i].x;
                if (d > od) discard;
            }
        }
    }
    frag_color = blit_opts.x > 0.5 ? vec4(c.rgb, 1.0) : c;
}
@end

@program blit vs_quad fs_blit

@fs fs_resolve
layout(binding=1) uniform fs_resolve_params {
    vec4 pal[64];
    vec4 opts;   // x = palette size, y = dither, z = flip (storage origin top-left), w = outline darkening (0 = body-coloured contour)
    vec4 size;   // x, y = art grid size, z, w = samples size
    vec4 phase;  // x, y = ordered-dither phase of art pixel (0,0); z = contour depth step (normalised); w = 1: transparent background
    vec4 own[16]; // own palette k (1..15): x = size, y = dither
};
@image_sample_type u_scene unfilterable_float
layout(binding=0) uniform texture2D u_scene;
@image_sample_type u_depth unfilterable_float
layout(binding=1) uniform texture2D u_depth;
@image_sample_type u_native unfilterable_float
layout(binding=2) uniform texture2D u_native;
// own palettes: row k holds palette k (row 0 unused), one colour a texel
@image_sample_type u_pal unfilterable_float
layout(binding=3) uniform texture2D u_pal;
@sampler_type u_smp nonfiltering
layout(binding=0) uniform sampler u_smp;
in vec2 v_uv;
out vec4 frag_color;

// colour i of palette k (0 = the scene's)
vec3 pixelMapped(int k, int i) {
    if (k == 0) return pal[i].rgb;
    return texelFetch(sampler2D(u_pal, u_smp), ivec2(i, k), 0).rgb;
}
int pixelNearest(vec3 c, bool solid, int k) {
    int n = int(k == 0 ? opts.x : own[k].x);
    int best = solid && n > 1 ? 1 : 0;
    float bd = 1e9;
    for (int i = 0; i < 64; i++) {
        if (i >= n) break;
        if (solid && i == 0 && n > 1) continue;
        vec3 df = c - pixelMapped(k, i);
        float d = dot(df * df, vec3(0.30, 0.55, 0.15));
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}
// own palette of a samples texel of the depth target (0 = the scene's): see fs_splat
int ownOf(vec4 d) { return d.b > 0.5 ? int(d.g / d.b * (255.0 / 8.0) + 0.5) : 0; }
// y-up pixel coordinate -> storage texel coordinate
ivec2 stor(ivec2 p, float h) { return ivec2(p.x, opts.z > 0.5 ? int(h) - 1 - p.y : p.y); }
ivec2 clampS(ivec2 p) { return clamp(p, ivec2(0), ivec2(size.zw) - 1); }
float pixelDepth(ivec2 p) {
    ivec2 q = p * 2; float d = 1.0;
    for (int y = 0; y < 2; y++) for (int x = 0; x < 2; x++)
        d = min(d, texelFetch(sampler2D(u_depth, u_smp), stor(clampS(q + ivec2(x, y)), size.w), 0).r);
    return d;
}
float bayer(int i) {
    // 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5
    int t[16] = int[16](0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5);
    return float(t[i]);
}
void main() {
    ivec2 fp = ivec2(gl_FragCoord.xy);
    ivec2 p = ivec2(fp.x, opts.z > 0.5 ? int(size.y) - 1 - fp.y : fp.y);
    // labels number the colours of every palette apart: palette * 64 + colour
    vec3 samples[4]; int labels[4]; int owns[4]; bool solid[4]; float coverage = 0.0;
    for (int i = 0; i < 4; i++) {
        ivec2 q = stor(clampS(p * 2 + ivec2(i & 1, i >> 1)), size.w);
        samples[i] = texelFetch(sampler2D(u_scene, u_smp), q, 0).rgb;
        vec4 dq = texelFetch(sampler2D(u_depth, u_smp), q, 0);
        solid[i] = dq.r < 0.995;
        owns[i] = ownOf(dq);
        labels[i] = owns[i] * 64 + pixelNearest(samples[i], solid[i], owns[i]);
        if (solid[i]) coverage += 0.25;
    }
    // the pixel takes an own palette, and its dither, when at least two of its four samples do
    int k = 0;
    for (int i = 0; i < 4; i++) {
        if (owns[i] == 0) continue;
        int n = 0;
        for (int j = 0; j < 4; j++) if (owns[j] == owns[i]) n++;
        if (n >= 2) k = owns[i];
    }
    vec3 native = texelFetch(sampler2D(u_native, u_smp), stor(p, size.y), 0).rgb;
    ivec2 dp = p + ivec2(phase.xy);
    float noise = ((bayer((dp.x & 3) + ((dp.y & 3) << 2)) + 0.5) / 16.0 - 0.5) * (k == 0 ? opts.y : own[k].y);
    vec3 c = pixelMapped(k, pixelNearest(clamp(native + noise, 0.0, 1.0), false, k));
    int best = -1, score = 0;
    for (int i = 0; i < 4; i++) {
        if (!solid[i]) continue;
        int n = 0;
        for (int j = 0; j < 4; j++) if (solid[j] && labels[j] == labels[i]) n++;
        if (n > score) { best = labels[i]; score = n; }
    }
    bool clear = phase.w > 0.5 && best < 0;
    if (best >= 0) {
        bool outer = coverage < 1.0 || max(max(pixelDepth(p - ivec2(1, 0)), pixelDepth(p + ivec2(1, 0))),
            max(pixelDepth(p - ivec2(0, 1)), pixelDepth(p + ivec2(0, 1)))) >= 0.995;
        if (outer) {
            vec3 body = pixelMapped(best / 64, best % 64);
            vec3 tone = body * 0.64;
            tone = min(vec3(1.0), tone * max(1.0, 0.12 / max(dot(tone, vec3(0.30, 0.55, 0.15)), 0.001)));
            vec3 edge = mix(body, tone, 0.65);
            edge = mix(edge, pixelMapped(0, 0), opts.w);
            // over a transparent background the contour keeps its own colour instead of fading to palette 0
            c = phase.w > 0.5 ? edge : mix(pixelMapped(0, 0), edge, coverage);
        } else if (opts.w > 0.0) {
            // dark contour where this pixel stands in front of a sharp step back
            float d0 = pixelDepth(p);
            float dn = max(max(pixelDepth(p - ivec2(1, 0)), pixelDepth(p + ivec2(1, 0))),
                           max(pixelDepth(p - ivec2(0, 1)), pixelDepth(p + ivec2(0, 1))));
            if (dn - d0 > phase.z) c = mix(c, pixelMapped(0, 0), opts.w);
        }
    }
    frag_color = vec4(c, clear ? 0.0 : 1.0);
}
@end

@program resolve vs_quad fs_resolve

// The crop carries the visible contribution of just one own-palette model in
// depth.b. All other geometry participates in depth and colour blending.
@fs fs_detail
layout(binding=0) uniform texture2D detail_color;
layout(binding=1) uniform texture2D detail_depth;
layout(binding=2) uniform texture2D detail_palette;
@sampler_type detail_smp nonfiltering
layout(binding=0) uniform sampler detail_smp;
layout(binding=1) uniform fs_detail_params { vec4 palette; };
in vec2 v_uv;
out vec4 frag_color;
void main() {
    float coverage = texture(sampler2D(detail_depth, detail_smp), v_uv).b;
    if (coverage == 0.0) discard;
    vec3 source = texture(sampler2D(detail_color, detail_smp), v_uv).rgb;
    const int ranks[16] = int[16](0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5);
    ivec2 q = ivec2(v_uv * vec2(textureSize(sampler2D(detail_color, detail_smp), 0)));
    source += ((float(ranks[(q.x & 3) + 4*(q.y & 3)]) + 0.5)/16.0 - 0.5) * palette.z;
    vec3 color = vec3(0.0);
    float best = 1e9;
    for (int i=0; i<int(palette.y); i++) {
        vec3 entry = texelFetch(sampler2D(detail_palette, detail_smp), ivec2(i,int(palette.x)),0).rgb;
        vec3 delta = source-entry;
        float error = dot(delta*delta,vec3(0.30,0.55,0.15));
        if (error < best) { best=error; color=entry; }
    }
    vec3 background = texelFetch(sampler2D(detail_palette, detail_smp), ivec2(0, int(palette.x)), 0).rgb;
    if (coverage < 1.0 && all(equal(color, background))) discard;
    frag_color = vec4(color, 1.0);
}
@end

@program detail vs_quad fs_detail
