#version 440
// Rounded-corner mask for cover art (anti-aliased, one texture read per pixel).
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float radius;   // item pixels
    vec2 itemSize;  // item pixels
    float duo;      // 1: two-tone (phosphor) instead of the real colours
    vec4 duoDark;
    vec4 duoLight;
};
layout(binding = 1) uniform sampler2D source;

void main()
{
    vec2 p = qt_TexCoord0 * itemSize;
    vec2 halfSize = itemSize * 0.5;
    float r = min(radius, min(halfSize.x, halfSize.y));
    vec2 q = abs(p - halfSize) - (halfSize - vec2(r));
    float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
    float a = 1.0 - smoothstep(-0.75, 0.75, d);
    vec4 c = texture(source, qt_TexCoord0);
    if (duo > 0.5) {
        float l = dot(c.rgb, vec3(0.299, 0.587, 0.114));
        l = clamp((l - 0.5) * 1.3 + 0.5, 0.0, 1.0);
        c = vec4(mix(duoDark.rgb, duoLight.rgb, l), 1.0) * c.a;
    }
    fragColor = c * (a * qt_Opacity);
}
