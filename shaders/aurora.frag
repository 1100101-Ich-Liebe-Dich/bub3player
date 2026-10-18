#version 440
// Aurora: domain-warped noise in up to three colours plus drifting bokeh.
// Rendered into a tiny texture (~256 px wide) and stretched, so it is cheap.
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float time;
    float aspect;
    float intensity;
    float energy;
    float beat;
    float bokeh;
    float boost;
    vec4 c1;
    vec4 c2;
    vec4 c3;
    vec4 base;
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
    for (int i = 0; i < 5; ++i) {
        v += a * noise(p);
        p = p * 2.03 + vec2(1.7, 9.2);
        a *= 0.5;
    }
    return v;
}

void main()
{
    vec2 uv = qt_TexCoord0;
    vec2 p = vec2(uv.x * aspect, uv.y) * 1.5;
    float t = time * 0.08;

    vec2 q = vec2(fbm(p + vec2(0.0, t)), fbm(p + vec2(5.2, 1.3) - t * 0.7));
    vec2 r = vec2(fbm(p + 3.5 * q + vec2(1.7, 9.2) + t * 1.3), fbm(p + 3.5 * q + vec2(8.3, 2.8) - t));
    float f = fbm(p + 3.0 * r + energy * 0.15);

    vec3 col = base.rgb;
    col = mix(col, c1.rgb, smoothstep(0.18, 0.85, f));
    col = mix(col, c2.rgb, smoothstep(0.25, 0.95, length(q)) * 0.8);
    col = mix(col, c3.rgb, smoothstep(0.35, 1.0, r.x) * 0.65);

    // Curtains of light drifting through.
    float band = sin(p.x * 1.4 + r.y * 2.6 + t * 3.0) * 0.5 + 0.5;
    col += c2.rgb * pow(band, 6.0) * 0.22 * (0.7 + energy * 0.5);

    float vig = smoothstep(1.35, 0.1, length((uv - 0.5) * vec2(aspect * 0.7, 1.0)));
    float k = clamp(intensity * (0.95 + 0.35 * boost) + beat * 0.05, 0.0, 1.0);
    col = mix(base.rgb, col, k) * (0.68 + 0.32 * vig) * (1.0 + 0.12 * boost);

    if (bokeh > 0.5) {
        for (int i = 0; i < 14; ++i) {
            float fi = float(i);
            vec2 c = vec2(hash(vec2(fi, 1.3)), hash(vec2(fi, 7.1)));
            c.y = fract(c.y - time * 0.012 * (0.4 + hash(vec2(fi, 3.3))));
            c.x += sin(time * 0.05 + fi) * 0.03;
            float rad = mix(0.02, 0.07, hash(vec2(fi, 9.9)));
            vec2 d = (uv - c) * vec2(aspect, 1.0);
            float b = smoothstep(rad, rad * 0.55, length(d));
            vec3 bc = mix(c1.rgb, c3.rgb, hash(vec2(fi, 5.5)));
            col += bc * b * 0.16 * (0.8 + beat * 0.2);
        }
    }
    fragColor = vec4(col, 1.0) * qt_Opacity;
}
