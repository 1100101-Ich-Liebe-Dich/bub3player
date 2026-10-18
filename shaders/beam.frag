#version 440
// A soft cone of stage light, from the top centre of the item downwards.
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec4 color;
    float intensity;
    float spread;     // half width at the bottom, 0..0.5 of the item width
};

void main()
{
    vec2 uv = qt_TexCoord0;
    float halfW = mix(0.012, spread, uv.y);
    float d = abs(uv.x - 0.5) / halfW;
    float cone = (1.0 - smoothstep(0.35, 1.0, d));
    float fall = (1.0 - uv.y * 0.75) * smoothstep(0.0, 0.04, uv.y);
    float core = exp(-d * d * 6.0) * 0.6;
    float a = (cone * 0.55 + core) * fall * intensity;
    fragColor = vec4(color.rgb * a, a * 0.85) * qt_Opacity;
}
