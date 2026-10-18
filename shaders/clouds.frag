#version 440
// Soft UI big view: slow, soft clouds in the palette's own tones. Low contrast
// on purpose; no flashes. Rendered small and stretched (they are soft anyway).
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float time;
    float aspect;
    float swell;      // 0..1 smoothed music energy: clouds billow a little
    vec4 base;
    vec4 light;
    vec4 dark;
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
    for (int i = 0; i < 5; ++i) { v += a * noise(p); p = p * 2.02 + vec2(3.1, 1.7); a *= 0.5; }
    return v;
}

float layer(vec2 p, float t, float speed, float cover)
{
    vec2 q = p + vec2(t * speed, 0.0);
    float n = fbm(q + 0.35 * vec2(fbm(q * 0.6 + t * 0.03), fbm(q * 0.6 + 4.0)));
    return smoothstep(cover, cover + 0.2, n);
}

void main()
{
    vec2 uv = qt_TexCoord0;
    vec2 p = vec2(uv.x * aspect, uv.y) * 2.4;
    float cover = 0.5 - swell * 0.06;
    vec3 lit = mix(light.rgb, vec3(1.0), 0.22);

    // far layer: slow and faint; near layer: a bit faster and brighter
    float far = layer(p * 0.8 + vec2(0.0, 3.0), time, 0.025, cover + 0.04);
    float near = layer(p * 1.1, time, 0.045, cover);

    vec3 col = base.rgb;
    // gentle vertical light: brighter towards the top
    col = mix(col, light.rgb, (1.0 - uv.y) * 0.18);
    // clouds: lit tops, slightly shaded bellies
    float shadeFar = smoothstep(0.0, 1.0, uv.y);
    col = mix(col, mix(lit, base.rgb, 0.4), far * 0.42);
    vec3 cloud = mix(lit, mix(base.rgb, dark.rgb, 0.3), 0.25 + 0.35 * shadeFar);
    col = mix(col, cloud, near * (0.7 + swell * 0.08));
    fragColor = vec4(col, 1.0) * qt_Opacity;
}
