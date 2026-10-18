#version 440
// 70s wallpaper for the Vintage skin. Static: rendered once per size change.
// pattern: 0 circles, 1 waves, 2 wood panelling, 3 flower power, 4 plain wall
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec2 itemSize;
    float pattern;
    float tile;       // tile size in px
    float dim;        // 0..1 darkening (the room is dimly lit)
    vec4 c1;          // main colour (orange)
    vec4 c2;          // dark (brown)
    vec4 c3;          // light (cream)
    vec4 c4;          // second accent (mustard)
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
    for (int i = 0; i < 4; ++i) { v += a * noise(p); p = p * 2.03 + 11.0; a *= 0.5; }
    return v;
}
// returns 1 inside a band [a, b] of a distance field, antialiased
float band(float d, float a, float b, float aa)
{
    return smoothstep(a - aa, a + aa, d) * (1.0 - smoothstep(b - aa, b + aa, d));
}

vec3 circles(vec2 p)
{
    vec2 c = p / tile;
    vec2 f = fract(c) - 0.5;
    float d = length(f);
    float aa = fwidth(d) * 0.8;
    vec3 col = c3.rgb;
    // quarter circles in the corners (they form a second, offset circle)
    float dc = length(abs(f) - 0.5);
    col = mix(col, c2.rgb, 1.0 - smoothstep(0.30 - aa, 0.30 + aa, dc));
    col = mix(col, c4.rgb, 1.0 - smoothstep(0.20 - aa, 0.20 + aa, dc));
    col = mix(col, c2.rgb, 1.0 - smoothstep(0.10 - aa, 0.10 + aa, dc));
    // the big concentric circle
    col = mix(col, c1.rgb, 1.0 - smoothstep(0.44 - aa, 0.44 + aa, d));
    col = mix(col, c3.rgb, 1.0 - smoothstep(0.36 - aa, 0.36 + aa, d));
    col = mix(col, c2.rgb, 1.0 - smoothstep(0.31 - aa, 0.31 + aa, d));
    col = mix(col, c4.rgb, 1.0 - smoothstep(0.20 - aa, 0.20 + aa, d));
    col = mix(col, c1.rgb, 1.0 - smoothstep(0.10 - aa, 0.10 + aa, d));
    return col;
}

vec3 waves(vec2 p)
{
    float y = p.y + sin(p.x / tile * 3.14159 * 1.0) * tile * 0.22;
    float t = fract(y / tile);
    float aa = fwidth(t) * 0.8;
    vec3 col = c3.rgb;
    col = mix(col, c2.rgb, band(t, 0.00, 0.22, aa));
    col = mix(col, c1.rgb, band(t, 0.22, 0.46, aa));
    col = mix(col, c4.rgb, band(t, 0.46, 0.62, aa));
    col = mix(col, c3.rgb, band(t, 0.62, 0.70, aa));
    col = mix(col, c1.rgb, band(t, 0.70, 0.78, aa));
    return col;
}

vec3 panels(vec2 p)
{
    float w = tile * 1.1;
    float x = mod(p.x, w);
    float id = floor(p.x / w);
    vec2 g = vec2(p.y + id * 397.0, x);
    float warp = fbm(vec2(g.x * 0.003, g.y * 0.03) + id) * 4.0;
    float rings = sin((g.y * 0.05 + warp * 2.2) * 3.14159) * 0.5 + 0.5;
    float grain = fbm(vec2(g.x * 0.004, g.y * 0.35) + id * 3.0);
    vec3 dark = c2.rgb * 0.75;
    vec3 light = mix(c2.rgb, c1.rgb, 0.45);
    vec3 col = mix(dark, light, 0.3 + 0.45 * rings * grain + 0.25 * grain);
    // V-groove between boards
    float edge = min(x, w - x);
    col *= mix(0.35, 1.0, smoothstep(0.0, 3.0, edge));
    col += vec3(1.0, 0.85, 0.6) * 0.06 * (1.0 - smoothstep(3.0, 6.0, abs(x - 4.0)));
    return col;
}

vec3 flowers(vec2 p)
{
    vec3 col = c3.rgb;
    // two offset grids of flowers
    for (int k = 0; k < 2; ++k) {
        vec2 off = k == 0 ? vec2(0.0) : vec2(0.5, 0.5);
        vec2 c = p / tile + off;
        vec2 id = floor(c);
        vec2 f = fract(c) - 0.5;
        float rot = hash(id + float(k) * 7.0) * 6.28;
        float sz = k == 0 ? 0.42 : 0.26;
        float r = length(f);
        float a = atan(f.y, f.x) + rot;
        float petal = sz * (0.62 + 0.38 * abs(cos(a * 2.5)));
        float aa = fwidth(r) * 0.9;
        vec3 pc = k == 0 ? c1.rgb : c4.rgb;
        col = mix(col, c2.rgb, 1.0 - smoothstep(petal + 0.03 - aa, petal + 0.03 + aa, r));   // outline
        col = mix(col, pc, 1.0 - smoothstep(petal - aa, petal + aa, r));
        col = mix(col, k == 0 ? c4.rgb : c1.rgb, 1.0 - smoothstep(sz * 0.36 - aa, sz * 0.36 + aa, r));
        col = mix(col, c2.rgb, 1.0 - smoothstep(sz * 0.18 - aa, sz * 0.18 + aa, r));
    }
    return col;
}

void main()
{
    vec2 p = qt_TexCoord0 * itemSize;
    vec2 uv = qt_TexCoord0;
    int k = int(pattern + 0.5);
    vec3 col;
    if (k == 0) col = circles(p);
    else if (k == 1) col = waves(p);
    else if (k == 2) col = panels(p);
    else if (k == 3) col = flowers(p);
    else col = mix(c2.rgb * 0.6, c2.rgb, fbm(p * 0.01));

    // printed on paper: slight blotchiness and fibre
    float paper = fbm(p * 0.02) * 0.6 + noise(p * 0.6) * 0.4;
    col *= 0.92 + paper * 0.1;
    // warm lamp from the upper left, dark corners
    float lamp = exp(-pow(length((uv - vec2(0.18, 0.05)) * vec2(itemSize.x / itemSize.y, 1.0)) * 1.1, 2.0));
    col *= mix(1.0 - dim, 1.0, lamp * 0.6);
    col += vec3(1.0, 0.7, 0.35) * lamp * 0.05;
    float vig = smoothstep(1.25, 0.35, length((uv - 0.5) * vec2(1.25, 1.0)));
    col *= mix(0.55, 1.0, vig);
    fragColor = vec4(col, 1.0) * qt_Opacity;
}
