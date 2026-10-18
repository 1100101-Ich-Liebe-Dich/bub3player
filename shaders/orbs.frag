#version 440
// Glass skin backdrop: a deep two-tone gradient (optionally the cover, hugely
// enlarged) with glossy coloured spheres and a ring drifting slowly.
// The same shader renders twice: crisp at screen size for what you see, and
// with `soft` = 1 into a tiny texture that the frosted glass panels sample.
// Soft edges are computed analytically, so the tiny version needs no blur pass.
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float time;
    float aspect;
    float soft;       // 0 crisp, 1 very soft
    float px;         // one output pixel in height units (anti-aliasing)
    float coverMix;   // 0..1 how much of the cover shows in the background
    float scale;      // sphere size multiplier
    vec2 parallax;    // -1..1 mouse offset
    vec4 base1;
    vec4 base2;
    vec4 c1;
    vec4 c2;
    vec4 c3;
    vec4 c4;
    vec4 c5;
};
layout(binding = 1) uniform sampler2D cover;

float hash(vec2 p)
{
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

// Shaded sphere: light from the top left, deeper colour at the bottom right,
// a soft specular spot and a faint bright rim.
vec3 sphere(vec2 local, vec3 col)
{
    float t = clamp(dot(local, vec2(0.62, 0.78)) * 0.5 + 0.5, 0.0, 1.0);
    vec3 s = mix(mix(col, vec3(1.0), 0.28), col * 0.62, t);
    float hl = 1.0 - clamp(length(local - vec2(-0.38, -0.42)) * 1.4, 0.0, 1.0);
    s += vec3(1.0) * pow(hl, 2.6) * 0.55;
    float rl = length(local);
    s += mix(col, vec3(1.0), 0.5) * smoothstep(0.78, 1.0, rl) * 0.18 * (1.0 - t);
    return s;
}

void drawOrb(inout vec3 bg, vec2 p, vec2 c, float r, vec3 col)
{
    float aa = px * 1.2 + soft * r * 0.55;
    vec2 d = p - c;
    float l = length(d);
    // soft contact shadow below and to the right
    float sh = 1.0 - smoothstep(r * 0.5, r * 1.5, length(d - vec2(r * 0.12, r * 0.22)));
    bg *= 1.0 - sh * 0.28;
    float cov = 1.0 - smoothstep(r - aa, r + aa, l);
    bg = mix(bg, sphere(d / r, col), cov);
}

void drawRing(inout vec3 bg, vec2 p, vec2 c, float r, float w, vec3 col)
{
    float aa = px * 1.2 + soft * w * 1.2;
    vec2 d = p - c;
    float l = length(d);
    float band = abs(l - r);
    float cov = 1.0 - smoothstep(w - aa, w + aa, band);
    // tube shading across the band, light from the top left
    float across = (l - r) / w;               // -1 inner .. 1 outer
    vec2 n = d / max(l, 1e-4);
    float facing = dot(n, vec2(-0.62, -0.78)); // 1 on the lit side
    vec3 s = mix(col * 0.6, mix(col, vec3(1.0), 0.35), 0.5 + 0.5 * facing * (1.0 - abs(across)));
    s += vec3(1.0) * pow(max(0.0, 1.0 - abs(across + 0.35)), 6.0) * 0.35 * max(0.0, facing);
    bg = mix(bg, s, cov);
}

void main()
{
    vec2 uv = qt_TexCoord0;
    vec2 p = vec2(uv.x * aspect, uv.y);

    // background
    float g = clamp(dot(uv, vec2(0.55, 0.45)), 0.0, 1.0);
    vec3 bg = mix(base1.rgb, base2.rgb, smoothstep(0.0, 1.0, g));
    if (coverMix > 0.0) {
        vec2 cuv = vec2(0.5) + (uv - vec2(0.5)) * vec2(min(1.0, aspect), min(1.0, 1.0 / aspect)) * 0.75;
        vec3 cv = texture(cover, cuv).rgb;
        bg = mix(bg, cv * 0.8, coverMix * 0.85);
    }
    // a faint glow in the middle so the centre never looks flat
    bg += mix(c2.rgb, c1.rgb, uv.x) * 0.10 * (1.0 - smoothstep(0.0, 0.9, length((uv - vec2(0.5, 0.45)) * vec2(aspect, 1.0))));

    float t = time;
    vec2 par = parallax * 0.025;
    float S = scale;
    drawOrb(bg, p, vec2(aspect * 0.16 + 0.05 * sin(t * 0.21), 0.30 + 0.05 * cos(t * 0.17)) + par * 1.6, 0.21 * S, c1.rgb);
    drawOrb(bg, p, vec2(aspect * 0.52 + 0.06 * sin(t * 0.13 + 2.0), 0.12 + 0.04 * sin(t * 0.19)) + par * 0.8, 0.10 * S, c3.rgb);
    drawRing(bg, p, vec2(aspect * 0.60 + 0.04 * cos(t * 0.11), 0.56 + 0.05 * sin(t * 0.15 + 1.0)) + par * 1.1, 0.17 * S, 0.034 * S, c5.rgb);
    drawOrb(bg, p, vec2(aspect * 0.84 + 0.05 * cos(t * 0.16 + 1.3), 0.80 + 0.05 * sin(t * 0.12)) + par * 2.0, 0.27 * S, c2.rgb);
    drawOrb(bg, p, vec2(aspect * 0.36 + 0.05 * sin(t * 0.18 + 4.0), 0.90 + 0.04 * cos(t * 0.22)) + par * 1.3, 0.14 * S, c4.rgb);
    drawOrb(bg, p, vec2(aspect * 0.93 + 0.03 * sin(t * 0.25 + 0.7), 0.18 + 0.04 * cos(t * 0.2 + 2.2)) + par * 0.6, 0.075 * S, c3.rgb);
    drawOrb(bg, p, vec2(aspect * 0.05 + 0.03 * cos(t * 0.23), 0.86 + 0.03 * sin(t * 0.27)) + par * 0.9, 0.06 * S, c5.rgb);

    // the soft copy is what the glass shows: a little darker keeps white text readable
    bg *= mix(1.0, 0.74, soft);
    // dither against banding
    bg += (hash(uv * 911.0 + fract(t)) - 0.5) / 255.0;
    fragColor = vec4(bg, 1.0) * qt_Opacity;
}
