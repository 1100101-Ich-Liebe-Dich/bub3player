#version 440
// Progress ring with a two-colour sweep and a soft glow.
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float progress;
    float thickness;  // fraction of the radius
    float glow;
    vec4 color1;
    vec4 color2;
    vec4 track;
};

void main()
{
    vec2 p = qt_TexCoord0 * 2.0 - 1.0;
    float r = length(p);
    float a = atan(p.x, -p.y);                // 0 at the top, clockwise
    float t = (a < 0.0 ? a + 6.2831853 : a) / 6.2831853;
    float outer = 1.0 - thickness * 0.5 - glow * 0.5;
    float inner = outer - thickness;
    float aa = 0.012;
    float band = smoothstep(inner - aa, inner + aa, r) * (1.0 - smoothstep(outer - aa, outer + aa, r));
    float on = step(t, progress);
    vec4 c = mix(color1, color2, t);
    vec4 col = mix(track * track.a, vec4(c.rgb, 1.0), on) * band;
    // glow around the lit part
    float mid = (inner + outer) * 0.5;
    float g = exp(-pow((r - mid) / (thickness * 0.5 + glow), 2.0)) * glow * 2.2 * on;
    col += vec4(c.rgb, 1.0) * g * (1.0 - band) * 0.6;
    fragColor = col * qt_Opacity;
}
