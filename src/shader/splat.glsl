// splat.glsl — Gaussian splat rendering and palette pixel resolve.
//
// Programs:
//   splat    — one instanced quad per splat; the vertex stage projects the splat's 3D
//              covariance to a screen-space ellipse (orthographic view), the fragment stage
//              evaluates the Gaussian. Two colour targets: 0 = colour (blended by alpha),
//              1 = normalised depth in .r (blended by alpha for solid splats only).
//   resolve  — palette resolve of a 2x samples target into the art grid: 2x2 palette
//              vote, ordered dither on the native picture, body-coloured outer contour.
//   blit     — nearest-neighbour copy of a target into a viewport.
//
// Pixel coordinates in every pass are "y-up": row 0 is the lowest screen row. Storage rows
// differ per backend (origin_top_left), so passes convert with u_flip where they address
// texels or compute ordered-dither phases.

@ctype mat4 Splat_Mat4
@ctype vec4 Splat_Vec4

@vs vs_splat
layout(binding=0) uniform vs_splat_params {
    mat4 view;      // world -> view (column-major)
    vec4 cam;       // x = ortho half-height, y = aspect, z = target width px, w = target height px
    vec4 dep;       // x = eye distance, y = world radius, z = focal px, w = near
};

in vec4 a_pos;   // x y z density
in vec4 a_cov0;  // c00 c01 c02 packed rgb
in vec4 a_cov1;  // c11 c12 c22 kind

out vec2 v_off;
out vec3 v_conic;
out vec4 v_col;
out float v_dn;

void main() {
    int corner = gl_VertexIndex;
    vec2 sgn = vec2((corner & 1) != 0 ? 1.0 : -1.0, (corner & 2) != 0 ? 1.0 : -1.0);
    vec4 p = view * vec4(a_pos.xyz, 1.0);
    float al = a_pos.w;
    float focal = dep.z;
    vec3 a = focal * vec3(view[0][0], view[1][0], view[2][0]);
    vec3 b = focal * vec3(view[0][1], view[1][1], view[2][1]);
    float c00 = a_cov0.x, c01 = a_cov0.y, c02 = a_cov0.z;
    float c11 = a_cov1.x, c12 = a_cov1.y, c22 = a_cov1.z;
    vec3 u = vec3(c00 * a.x + c01 * a.y + c02 * a.z, c01 * a.x + c11 * a.y + c12 * a.z, c02 * a.x + c12 * a.y + c22 * a.z);
    vec3 w = vec3(c00 * b.x + c01 * b.y + c02 * b.z, c01 * b.x + c11 * b.y + c12 * b.z, c02 * b.x + c12 * b.y + c22 * b.z);
    float A = dot(a, u) + 0.15;
    float B = dot(b, u);
    float C = dot(b, w) + 0.15;
    float det = A * C - B * B;
    if (al <= 0.0 || p.z > -dep.w || det <= 0.0) {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        v_off = vec2(0.0); v_conic = vec3(0.0); v_col = vec4(0.0); v_dn = 1.0;
        return;
    }
    vec2 ext = vec2(min(3.0 * sqrt(A), 2048.0), min(3.0 * sqrt(C), 2048.0));
    vec2 s = vec2((p.x / (cam.x * cam.y)) * 0.5 + 0.5, (p.y / cam.x) * 0.5 + 0.5) * cam.zw;
    vec2 off = sgn * ext;
    vec2 ndc = (s + off) / cam.zw * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.5, 1.0);
    v_off = off;
    v_conic = vec3(C / det, -B / det, A / det);
    float pk = a_cov0.w;
    v_col = vec4(mod(pk, 256.0) / 255.0, mod(floor(pk / 256.0), 256.0) / 255.0, floor(pk / 65536.0) / 255.0, al);
    v_dn = clamp((-p.z - (dep.x - dep.y)) / (2.0 * dep.y), 0.0, 1.0);
}
@end

@fs fs_splat
in vec2 v_off;
in vec3 v_conic;
in vec4 v_col;
in float v_dn;
layout(location=0) out vec4 frag_color;
layout(location=1) out vec4 frag_depth;

void main() {
    float dx = v_off.x, dy = v_off.y;
    float pw = -0.5 * (v_conic.x * dx * dx + v_conic.z * dy * dy) - v_conic.y * dx * dy;
    if (pw > 0.0 || pw < -4.5) discard;
    float al = v_col.a;
    float gg = al * exp(pw);
    float a = min(0.99, gg);
    bool solid = al >= 1.0;
    if (solid) a = 0.99 * smoothstep(0.38, 0.62, gg);
    if (a < 0.004) discard;
    frag_color = vec4(min(v_col.rgb, vec3(1.0)), a);
    frag_depth = vec4(v_dn, 0.0, 0.0, solid ? a : 0.0);
}
@end

@program splat vs_splat fs_splat

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
in vec2 v_uv;
out vec4 frag_color;
void main() {
    frag_color = vec4(texture(sampler2D(blit_tex, blit_smp), v_uv).rgb, 1.0);
}
@end

@program blit vs_quad fs_blit

@fs fs_resolve
layout(binding=1) uniform fs_resolve_params {
    vec4 pal[48];
    vec4 opts;   // x = palette size, y = dither, z = flip (storage origin top-left), w = outline darkening (0 = body-coloured contour)
    vec4 size;   // x, y = art grid size, z, w = samples size
    vec4 phase;  // x, y = ordered-dither phase of art pixel (0,0); z = contour depth step (normalised)
};
@image_sample_type u_scene unfilterable_float
layout(binding=0) uniform texture2D u_scene;
@image_sample_type u_depth unfilterable_float
layout(binding=1) uniform texture2D u_depth;
@image_sample_type u_native unfilterable_float
layout(binding=2) uniform texture2D u_native;
@sampler_type u_smp nonfiltering
layout(binding=0) uniform sampler u_smp;
in vec2 v_uv;
out vec4 frag_color;

int pixelNearest(vec3 c, bool solid) {
    int n = int(opts.x);
    int best = solid && n > 1 ? 1 : 0;
    float bd = 1e9;
    for (int i = 0; i < 48; i++) {
        if (i >= n) break;
        if (solid && i == 0 && n > 1) continue;
        vec3 df = c - pal[i].rgb;
        float d = dot(df * df, vec3(0.30, 0.55, 0.15));
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}
vec3 pixelMapped(int i) { return pal[i].rgb; }
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
    vec3 samples[4]; int labels[4]; bool solid[4]; float coverage = 0.0;
    for (int i = 0; i < 4; i++) {
        ivec2 q = stor(clampS(p * 2 + ivec2(i & 1, i >> 1)), size.w);
        samples[i] = texelFetch(sampler2D(u_scene, u_smp), q, 0).rgb;
        solid[i] = texelFetch(sampler2D(u_depth, u_smp), q, 0).r < 0.995;
        labels[i] = pixelNearest(samples[i], solid[i]);
        if (solid[i]) coverage += 0.25;
    }
    vec3 native = texelFetch(sampler2D(u_native, u_smp), stor(p, size.y), 0).rgb;
    ivec2 dp = p + ivec2(phase.xy);
    float noise = ((bayer((dp.x & 3) + ((dp.y & 3) << 2)) + 0.5) / 16.0 - 0.5) * opts.y;
    vec3 c = pixelMapped(pixelNearest(clamp(native + noise, 0.0, 1.0), false));
    int best = -1, score = 0;
    for (int i = 0; i < 4; i++) {
        if (!solid[i]) continue;
        int n = 0;
        for (int j = 0; j < 4; j++) if (solid[j] && labels[j] == labels[i]) n++;
        if (n > score) { best = labels[i]; score = n; }
    }
    if (best >= 0) {
        bool outer = coverage < 1.0 || max(max(pixelDepth(p - ivec2(1, 0)), pixelDepth(p + ivec2(1, 0))),
            max(pixelDepth(p - ivec2(0, 1)), pixelDepth(p + ivec2(0, 1)))) >= 0.995;
        if (outer) {
            vec3 body = pixelMapped(best);
            vec3 tone = body * 0.64;
            tone = min(vec3(1.0), tone * max(1.0, 0.12 / max(dot(tone, vec3(0.30, 0.55, 0.15)), 0.001)));
            vec3 edge = mix(body, tone, 0.65);
            edge = mix(edge, pixelMapped(0), opts.w);
            c = mix(pixelMapped(0), edge, coverage);
        } else if (opts.w > 0.0) {
            // dark contour where this pixel stands in front of a sharp step back
            float d0 = pixelDepth(p);
            float dn = max(max(pixelDepth(p - ivec2(1, 0)), pixelDepth(p + ivec2(1, 0))),
                           max(pixelDepth(p - ivec2(0, 1)), pixelDepth(p + ivec2(0, 1))));
            if (dn - d0 > phase.z) c = mix(c, pixelMapped(0), opts.w);
        }
    }
    frag_color = vec4(c, 1.0);
}
@end

@program resolve vs_quad fs_resolve
