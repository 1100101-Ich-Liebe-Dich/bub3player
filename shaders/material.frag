#version 440
// Procedural cabinet materials. Static: rendered once per size change.
//  0 walnut   1 brushed aluminium   2 carbon fibre   3 piano lacquer
//  4 teak     5 rosewood            6 champagne alu  7 oak
//  8 black brushed metal            9 tolex          10 speaker grille cloth
// 11 black textured plastic        12 cream enamel (painted metal)
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec2 itemSize;
    float kind;
    float radius;
    float darken;    // 0..1 extra darkening (for recessed parts)
    float vertical;  // 1: grain / brushing runs vertically
    float seed;      // varies the pattern between boards
    float bevel;     // 0..1 strength of the edge bevel
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

// Flat-sawn wood: grain along x, warped growth rings (cathedrals), pores and
// a few darker streaks. `figure` scales how wild the rings are.
vec3 wood(vec2 p, vec3 dark, vec3 light, float ringFreq, float figure, float streaks)
{
    vec2 q = vec2(p.x * 0.0032, p.y * 0.028);
    float warp = fbm(q * vec2(1.0, 0.55) + 3.0 + seed) * 4.0 * figure;
    float rings = sin((p.y * ringFreq + warp * 2.4) * 3.14159);
    rings = rings * 0.5 + 0.5;
    rings = pow(rings, 1.6);
    float grain = fbm(vec2(p.x * 0.004, p.y * 0.38) + seed * 7.0);
    vec3 col = mix(dark, light, 0.25 + 0.5 * rings * (0.6 + 0.4 * grain) + 0.2 * grain);
    float pores = smoothstep(0.72, 0.96, noise(vec2(p.x * 0.09, p.y * 0.95) + seed));
    col *= 1.0 - pores * 0.22;
    float st = smoothstep(0.62, 0.8, fbm(vec2(p.x * 0.0015, p.y * 0.05) + seed * 3.0 + 11.0));
    col = mix(col, dark * 0.55, st * streaks);
    return col;
}

vec3 brushed(vec2 p, vec2 uv, vec3 base, float amount)
{
    float streak = noise(vec2(p.x * 0.02, p.y * 2.0) + seed) * 0.5 + noise(vec2(p.x * 0.006, p.y * 6.0) + seed) * 0.5;
    vec3 col = base + (streak - 0.5) * amount;
    col += exp(-pow((uv.x - uv.y * 0.6 - 0.25) * 3.0, 2.0)) * 0.10;
    col *= 0.93 + 0.07 * (1.0 - uv.y);
    return col;
}

void main()
{
    vec2 uv = qt_TexCoord0;
    vec2 p = qt_TexCoord0 * itemSize;
    vec2 g = vertical > 0.5 ? vec2(p.y, p.x) : p;     // grain coordinates
    vec2 guv = vertical > 0.5 ? vec2(uv.y, uv.x) : uv;
    vec3 col;
    int k = int(kind + 0.5);
    if (k == 0) {
        col = wood(g, vec3(0.15, 0.08, 0.04), vec3(0.38, 0.21, 0.11), 0.045, 1.0, 0.25);
        col += vec3(1.0, 0.85, 0.7) * pow(1.0 - uv.y, 6.0) * 0.10;
    } else if (k == 1) {
        col = brushed(g, guv, vec3(0.64, 0.655, 0.67), 0.12);
    } else if (k == 2) {
        vec2 gg = p / 6.0;
        vec2 c = floor(gg);
        vec2 f = fract(gg);
        float dir = mod(c.x + c.y, 2.0);
        float strand = dir > 0.5 ? sin(f.x * 3.14159) : sin(f.y * 3.14159);
        col = vec3(0.07, 0.075, 0.085) + vec3(0.09) * strand * (dir > 0.5 ? 1.0 : 0.7);
        col += vec3(0.1) * exp(-pow((uv.x + uv.y * 0.4 - 0.55) * 3.5, 2.0));
    } else if (k == 3) {
        col = vec3(0.025, 0.026, 0.03);
        col += vec3(0.12) * exp(-pow((uv.y - 0.12) * 9.0, 2.0)) * (0.6 + 0.4 * sin(uv.x * 3.0));
        col += vec3(0.05) * (1.0 - uv.y);
    } else if (k == 4) {
        // teak: warm honey brown, straighter grain, oily sheen
        col = wood(g, vec3(0.30, 0.16, 0.07), vec3(0.62, 0.38, 0.19), 0.06, 0.6, 0.35);
        col += vec3(1.0, 0.8, 0.55) * pow(1.0 - uv.y, 5.0) * 0.08;
    } else if (k == 5) {
        // rosewood (palisander): deep red-brown with near-black streaks
        col = wood(g, vec3(0.10, 0.035, 0.025), vec3(0.36, 0.13, 0.07), 0.035, 1.4, 0.7);
        col += vec3(1.0, 0.7, 0.6) * pow(1.0 - uv.y, 6.0) * 0.10;
    } else if (k == 6) {
        col = brushed(g, guv, vec3(0.80, 0.72, 0.56), 0.11);
    } else if (k == 7) {
        col = wood(g, vec3(0.45, 0.31, 0.17), vec3(0.78, 0.60, 0.38), 0.05, 0.8, 0.15);
    } else if (k == 8) {
        col = brushed(g, guv, vec3(0.085, 0.087, 0.095), 0.06);
    } else if (k == 9) {
        // tolex: pebbled vinyl
        float n = noise(p * 0.55 + seed) * 0.6 + noise(p * 1.3) * 0.4;
        col = vec3(0.075, 0.065, 0.06) + (n - 0.5) * 0.05;
        col += vec3(0.06) * (1.0 - uv.y);
    } else if (k == 10) {
        // grille cloth: coarse weave
        vec2 w = p / 3.0;
        float a = sin(w.x * 3.14159) * sin(w.y * 3.14159 * 0.5 + floor(w.x) * 1.57);
        float n = noise(p * 0.12 + seed);
        col = vec3(0.10, 0.085, 0.07) + vec3(0.05, 0.04, 0.03) * a + (n - 0.5) * 0.03;
    } else if (k == 11) {
        // black plastic with a fine texture
        float n = hash(floor(p * 0.9)) * 0.5 + noise(p * 0.3) * 0.5;
        col = vec3(0.07, 0.072, 0.078) + (n - 0.5) * 0.025;
        col += vec3(0.05) * exp(-pow((uv.y - 0.08) * 6.0, 2.0));
    } else {
        // cream enamel
        float n = noise(p * 0.08 + seed);
        col = vec3(0.90, 0.86, 0.76) + (n - 0.5) * 0.02;
        col *= 0.94 + 0.06 * (1.0 - uv.y);
    }
    col *= 1.0 - darken;

    // Rounded corners and a soft bevel.
    float d = sdRound(p - itemSize * 0.5, itemSize * 0.5, radius);
    float inside = 1.0 - smoothstep(-0.8, 0.8, d);
    float bw = 1.0 - smoothstep(0.0, 2.5, -d);
    col += vec3(1.0) * bw * 0.14 * bevel * (1.0 - uv.y);
    col -= vec3(1.0) * bw * 0.14 * bevel * uv.y;
    fragColor = vec4(col, 1.0) * inside * qt_Opacity;
}
