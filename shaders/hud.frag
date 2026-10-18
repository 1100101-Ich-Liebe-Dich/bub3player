#version 440
// HUD panel: chamfered corners, translucent fill, neon border with outer glow,
// brighter corner brackets and faint inner scanlines.
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec2 itemSize;   // px including margin
    float margin;
    float chamfer;
    float bw;        // border width px
    float glow;      // glow size px
    float scan;
    vec4 fill;
    vec4 border;
    vec4 bracket;
};

float sdChamfer(vec2 p, vec2 b, float c)
{
    vec2 q = abs(p) - b;
    float box = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0);
    float cut = (abs(p.x) + abs(p.y) - (b.x + b.y - c)) * 0.70710678;
    return max(box, cut);
}

void main()
{
    vec2 p = qt_TexCoord0 * itemSize;
    vec2 b = itemSize * 0.5 - vec2(margin);
    vec2 c = p - itemSize * 0.5;
    float d = sdChamfer(c, b, chamfer);
    float inside = 1.0 - smoothstep(-0.6, 0.6, d);

    vec4 col = vec4(fill.rgb, 1.0) * fill.a * inside;
    // inner scanlines
    col.rgb *= 1.0 - scan * 0.25 * (0.5 + 0.5 * sin(gl_FragCoord.y * 3.14159));
    // border line
    float line = (1.0 - smoothstep(bw - 0.6, bw + 0.6, abs(d + bw * 0.5)));
    // corner brackets: brighter near the chamfered corners
    vec2 a = abs(c) - (b - vec2(chamfer * 2.5));
    float nearCorner = step(0.0, a.x) * step(0.0, a.y);
    vec4 lc = mix(border, bracket, nearCorner);
    col += vec4(lc.rgb, 1.0) * lc.a * line;
    // Outer + inner glow. Added as light (no alpha), and faded out before the
    // edge of the margin so there is never a visible box around the panel.
    float g = exp(-abs(d) / max(glow, 0.5)) * 0.55 * (1.0 - smoothstep(margin * 0.55, margin * 0.95, d));
    col.rgb += border.rgb * border.a * g * (d > 0.0 ? 1.0 : 0.35);
    fragColor = col * qt_Opacity;
}
