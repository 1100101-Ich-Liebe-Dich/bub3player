#version 440
// Round soft light: glow behind orbs and covers, bokeh, highlights.
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec4 color;
    float softness; // 0 = hard disc, 1 = all falloff
};

void main()
{
    float r = length(qt_TexCoord0 * 2.0 - 1.0);
    float a = 1.0 - smoothstep(1.0 - max(softness, 0.01), 1.0, r);
    a *= a;
    fragColor = vec4(color.rgb, 1.0) * color.a * a * qt_Opacity;
}
