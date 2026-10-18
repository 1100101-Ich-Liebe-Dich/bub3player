#version 440
// CRT overlay: scanlines, a dark rim and a soft phosphor glow in the middle.
// Drawn over the whole window with alpha; static unless `flicker` > 0.
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec2 itemSize;
    float scan;       // 0..1 scanline strength
    float vignette;   // 0..1
    float flicker;    // 0..1
    float time;
    vec4 glow;        // phosphor colour
};

void main()
{
    vec2 uv = qt_TexCoord0;
    float y = uv.y * itemSize.y;
    float line = 0.5 + 0.5 * cos(y * 3.14159 * 2.0 / 3.0);
    float a = line * scan * 0.35;
    vec2 q = (uv - 0.5) * vec2(1.0, itemSize.y / itemSize.x);
    float r = length(q * vec2(1.0, 1.25));
    float v = smoothstep(0.42, 0.8, r) * vignette;
    // rolling bright band, very faint
    float band = flicker * 0.04 * smoothstep(0.0, 0.08, 0.08 - abs(fract(uv.y * 0.6 - time * 0.12) - 0.5) + 0.04);
    float fl = flicker * 0.02 * sin(time * 50.0);
    vec4 dark = vec4(0.0, 0.0, 0.0, clamp(a + v + fl, 0.0, 0.95));
    vec4 light = vec4(glow.rgb * band, band);
    fragColor = (dark + light) * qt_Opacity;
}
