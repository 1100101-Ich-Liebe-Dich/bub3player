#version 440
// Synthwave scene: gradient sky, stars, striped retro sun, mountains and a
// perspective neon grid that races towards the viewer. Scanlines and vignette on top.
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float time;      // grid travel (already scaled by speed)
    float clock;     // wall time for twinkle and sun drift
    float aspect;
    float beat;
    float energy;
    float horizon;   // 0..1 from the top
    float sunOn;
    float starsOn;
    float mountOn;
    float scan;
    float glowOn;
    vec4 sky0;
    vec4 sky1;
    vec4 sky2;
    vec4 sky3;
    vec4 sun0;
    vec4 sun1;
    vec4 grid;
    vec4 grid2;
};

float hash(vec2 p)
{
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}
float noise1(float x)
{
    float i = floor(x), f = fract(x);
    float u = f * f * (3.0 - 2.0 * f);
    return mix(hash(vec2(i, 0.0)), hash(vec2(i + 1.0, 0.0)), u);
}

void main()
{
    vec2 uv = qt_TexCoord0;
    float h = horizon;
    vec3 col;

    if (uv.y < h) {
        float t = uv.y / h;
        col = t < 0.45 ? mix(sky0.rgb, sky1.rgb, t / 0.45)
            : t < 0.8  ? mix(sky1.rgb, sky2.rgb, (t - 0.45) / 0.35)
                       : mix(sky2.rgb, sky3.rgb, (t - 0.8) / 0.2);
        if (starsOn > 0.5) {
            vec2 g = vec2(uv.x * aspect, uv.y) * 110.0;
            vec2 id = floor(g);
            float s = hash(id);
            if (s > 0.986) {
                vec2 f = fract(g) - 0.5;
                float tw = 0.55 + 0.45 * sin(clock * 2.3 + s * 60.0);
                col += vec3(1.0) * smoothstep(0.12, 0.0, length(f)) * tw * (1.0 - t) * 0.9;
            }
        }
        if (sunOn > 0.5) {
            vec2 c = vec2(0.5, h - 0.03);
            vec2 d = (uv - c) * vec2(aspect, 1.0);
            float R = 0.24 + beat * 0.012;
            float r = length(d);
            float inSun = smoothstep(R, R - 0.003, r);
            // Cut-out stripes in the lower half, thicker towards the horizon.
            float yy = (uv.y - (c.y - R * 0.2)) / (R * 1.2);
            float stripe = 1.0;
            if (yy > 0.0) {
                float k = fract(yy * 6.0 - clock * 0.12);
                stripe = step(0.06 + yy * 0.5, k);
            }
            vec3 sc = mix(sun0.rgb, sun1.rgb, clamp((uv.y - (c.y - R)) / (R * 1.25), 0.0, 1.0));
            col = mix(col, sc, inSun * stripe);
            col += sun1.rgb * exp(-max(r - R, 0.0) * 7.0) * 0.35 * glowOn * (1.0 + beat * 0.6);
        }
        if (mountOn > 0.5) {
            float x = uv.x * aspect * 2.2;
            float m = h - (0.035 + 0.075 * noise1(x * 1.3 + 3.0) * noise1(x * 0.4 + 11.0) * 1.6
                          + 0.025 * noise1(x * 4.0));
            float edge = smoothstep(m - 0.002, m + 0.002, uv.y);
            vec3 mcol = mix(sky0.rgb * 0.6, sky1.rgb * 0.35, 0.5);
            col = mix(col, mcol, edge);
            col += grid.rgb * exp(-abs(uv.y - m) * 380.0) * 0.6 * glowOn * (1.0 - edge * 0.3);
        }
    } else {
        float y = uv.y - h;
        float z = 0.16 / max(y, 0.0008);           // depth
        float x = (uv.x - 0.5) * aspect * z * 4.0;
        float zz = z + time;
        float fx = fwidth(x), fz = fwidth(zz * 0.5);
        float lx = abs(fract(x + 0.5) - 0.5);
        float lz = abs(fract(zz * 0.5 + 0.5) - 0.5);
        float gx = 1.0 - smoothstep(0.0, fx * 1.4 + 0.012, lx);
        float gz = 1.0 - smoothstep(0.0, fz * 1.4 + 0.012, lz);
        float lines = max(gx, gz);
        float halo = max(exp(-lx / (fx * 4.0 + 0.02)), exp(-lz / (fz * 4.0 + 0.02))) * 0.35 * glowOn;
        float fade = smoothstep(0.0, 0.22, y);      // dim at the horizon (lines merge there)
        vec3 floorCol = mix(sky3.rgb * 0.55, sky0.rgb * 0.4, smoothstep(0.0, 0.12, y));
        vec3 lc = mix(grid.rgb, grid2.rgb, smoothstep(0.0, 0.45, y));
        float pulse = 1.0 + beat * 1.2 + energy * 0.4;
        col = floorCol + lc * (lines + halo) * fade * pulse;
        // horizon glow
        col += sky3.rgb * exp(-y * 26.0) * 0.8;
    }

    // Scanlines and vignette.
    float sl = 0.5 + 0.5 * sin(gl_FragCoord.y * 3.14159);
    col *= 1.0 - scan * 0.38 * sl;
    float vig = smoothstep(1.25, 0.35, length((uv - 0.5) * vec2(aspect * 0.6, 1.0)));
    col *= 0.6 + 0.4 * vig;
    fragColor = vec4(col, 1.0) * qt_Opacity;
}
