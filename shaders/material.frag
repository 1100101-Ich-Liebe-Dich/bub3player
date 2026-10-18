#version 440
// Procedural cabinet materials: 0 walnut, 1 brushed aluminium, 2 carbon fibre, 3 piano lacquer.
// Static: rendered once per size change.
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec2 itemSize;
    float kind;
    float radius;
    float darken;    // 0..1 extra darkening (for recessed parts)
};

float hash(vec2 p)
{
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}
float noise(vec2 p)
{
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1, 0)), u.x), mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), u.x), u.y);
}
float fbm(vec2 p)
{
    float v = 0.0, a = 0.5;
    for (int i = 0; i < 5; ++i) { v += a * noise(p); p = p * 2.02 + 17.0; a *= 0.5; }
    return v;
}
float sdRound(vec2 p, vec2 b, float r)
{
    vec2 q = abs(p) - b + vec2(r);
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

void main()
{
    vec2 p = qt_TexCoord0 * itemSize;
    vec2 uv = qt_TexCoord0;
    vec3 col;
    int k = int(kind + 0.5);
    if (k == 0) {
        // Walnut: long grain along x, warped rings, pores, varnish sheen.
        vec2 q = vec2(p.x * 0.0035, p.y * 0.03);
        float warp = fbm(q * vec2(1.0, 0.6) + 3.0) * 4.0;
        float rings = sin((p.y * 0.045 + warp * 2.2) * 3.14159);
        float grain = fbm(vec2(p.x * 0.004, p.y * 0.35));
        vec3 dark = vec3(0.16, 0.085, 0.045);
        vec3 light = vec3(0.36, 0.2, 0.11);
        col = mix(dark, light, 0.45 + 0.35 * rings * grain + 0.25 * grain);
        float pores = smoothstep(0.75, 0.95, noise(vec2(p.x * 0.08, p.y * 0.9)));
        col *= 1.0 - pores * 0.25;
        col += vec3(1.0, 0.85, 0.7) * pow(1.0 - uv.y, 6.0) * 0.12;          // varnish highlight
    } else if (k == 1) {
        // Brushed aluminium: fine anisotropic streaks plus a soft diagonal sheen.
        float streak = noise(vec2(p.x * 0.02, p.y * 2.0)) * 0.5 + noise(vec2(p.x * 0.006, p.y * 6.0)) * 0.5;
        col = vec3(0.62, 0.64, 0.66) + (streak - 0.5) * 0.12;
        float sheen = exp(-pow((uv.x - uv.y * 0.6 - 0.25) * 3.0, 2.0)) * 0.12;
        col += sheen;
        col *= 0.92 + 0.08 * (1.0 - uv.y);
    } else if (k == 2) {
        // Carbon fibre twill.
        vec2 g = p / 6.0;
        vec2 c = floor(g);
        vec2 f = fract(g);
        float dir = mod(c.x + c.y, 2.0);
        float strand = dir > 0.5 ? sin(f.x * 3.14159) : sin(f.y * 3.14159);
        col = vec3(0.07, 0.075, 0.085) + vec3(0.09) * strand * (dir > 0.5 ? 1.0 : 0.7);
        col += vec3(0.1) * exp(-pow((uv.x + uv.y * 0.4 - 0.55) * 3.5, 2.0));
    } else {
        // Piano lacquer: deep black with soft reflections.
        col = vec3(0.025, 0.026, 0.03);
        col += vec3(0.12) * exp(-pow((uv.y - 0.12) * 9.0, 2.0)) * (0.6 + 0.4 * sin(uv.x * 3.0));
        col += vec3(0.05) * (1.0 - uv.y);
    }
    col *= 1.0 - darken;

    // Rounded corners and a thin bevel.
    float d = sdRound(p - itemSize * 0.5, itemSize * 0.5, radius);
    float inside = 1.0 - smoothstep(-0.8, 0.8, d);
    float bevel = 1.0 - smoothstep(0.0, 2.0, -d);
    col += vec3(1.0) * bevel * 0.12 * (1.0 - uv.y);
    col -= vec3(1.0) * bevel * 0.12 * uv.y;
    fragColor = vec4(col, 1.0) * inside * qt_Opacity;
}
