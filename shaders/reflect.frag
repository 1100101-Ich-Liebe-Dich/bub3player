#version 440
// Mirror image fading out downwards (cover reflection on a floor).
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float strength;
};
layout(binding = 1) uniform sampler2D source;

void main()
{
    vec2 uv = vec2(qt_TexCoord0.x, 1.0 - qt_TexCoord0.y);
    float fade = pow(1.0 - qt_TexCoord0.y, 2.2) * strength;
    fragColor = texture(source, uv) * fade * qt_Opacity;
}
