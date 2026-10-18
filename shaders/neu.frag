#version 440
// Neumorphism / soft UI surface.
// mode 0: raised (light shadow top-left, dark shadow bottom-right)
// mode 1: inset  (pressed into the surface: inner shadows)
// mode 2: convex (raised with a gentle dome gradient)
// mode 3: flat   (only a hairline highlight)
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec2 itemSize;    // px including margin
    float margin;
    float radius;
    float dist;       // shadow offset px
    float blur;       // shadow softness px
    float mode;
    float strength;   // 0..1 shadow intensity
    vec4 base;
    vec4 light;
    vec4 dark;
    vec4 fill;        // surface colour (usually base; accent for active parts)
};

float sdRound(vec2 p, vec2 b, float r)
{
    vec2 q = abs(p) - b + vec2(r);
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

void main()
{
    vec2 p = qt_TexCoord0 * itemSize - itemSize * 0.5;
    vec2 b = itemSize * 0.5 - vec2(margin);
    float r = min(radius, min(b.x, b.y));
    float d = sdRound(p, b, r);
    float inside = 1.0 - smoothstep(-0.7, 0.7, d);
    int m = int(mode + 0.5);
    vec2 off = vec2(dist);
    vec4 outCol = vec4(0.0);

    if (m == 0 || m == 2) {
        // outer shadows (fade to zero before the margin edge)
        float dd = sdRound(p - off, b, r);
        float dl = sdRound(p + off, b, r);
        float edgeFade = 1.0 - smoothstep(margin * 0.6, margin, max(abs(p.x) - b.x, abs(p.y) - b.y));
        float sDark = (1.0 - smoothstep(-blur * 0.3, blur, dd)) * strength * edgeFade;
        float sLight = (1.0 - smoothstep(-blur * 0.3, blur, dl)) * strength * edgeFade;
        vec4 shadow = vec4(light.rgb, 1.0) * sLight * light.a;
        shadow = vec4(dark.rgb, 1.0) * sDark * dark.a + shadow * (1.0 - sDark * dark.a);
        outCol = shadow * (1.0 - inside);
        vec3 surf = fill.rgb;
        if (m == 2) {
            float t = clamp((p.x + p.y) / (b.x + b.y) * 0.5 + 0.5, 0.0, 1.0);
            surf = mix(mix(fill.rgb, vec3(1.0), 0.13), mix(fill.rgb, vec3(0.0), 0.10), t);
        }
        // faint rim light on the top-left edge
        float rim = (1.0 - smoothstep(0.0, 1.6, -d)) * clamp((-p.x - p.y) / (b.x + b.y) + 0.3, 0.0, 1.0);
        surf = mix(surf, light.rgb, rim * 0.5 * strength);
        outCol += vec4(surf, 1.0) * inside;
    } else if (m == 1) {
        // inner shadows: dark from the top-left edge, light from the bottom-right edge
        float dd = sdRound(p + off, b, r);
        float dl = sdRound(p - off, b, r);
        float iDark = smoothstep(-blur, blur * 0.4, dd) * strength;
        float iLight = smoothstep(-blur, blur * 0.4, dl) * strength * 0.9;
        vec3 surf = fill.rgb;
        surf = mix(surf, dark.rgb, iDark * dark.a);
        surf = mix(surf, light.rgb, iLight * light.a * (1.0 - iDark));
        outCol = vec4(surf, 1.0) * inside;
    } else {
        float rim = (1.0 - smoothstep(0.0, 1.4, -d));
        vec3 surf = mix(fill.rgb, light.rgb, rim * 0.4);
        outCol = vec4(surf, 1.0) * inside;
    }
    fragColor = outCol * fill.a * qt_Opacity;
}
