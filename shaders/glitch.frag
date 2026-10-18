#version 440
// Chromatic split and horizontal slice jitter for a text/cover layer.
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float amount;    // 0..1 glitch strength (slices)
    float split;     // chromatic offset in uv
    float seed;
    vec4 tintA;      // colour of the left ghost
    vec4 tintB;      // colour of the right ghost
};
layout(binding = 1) uniform sampler2D source;

float hash(float n) { return fract(sin(n) * 43758.5453); }

void main()
{
    vec2 uv = qt_TexCoord0;
    float slice = floor(uv.y * 18.0);
    float h = hash(slice * 7.13 + seed);
    float shift = (h > 1.0 - amount * 0.6) ? (hash(slice + seed * 3.1) - 0.5) * 0.12 * amount : 0.0;
    vec2 u = uv + vec2(shift, 0.0);
    float s = split * (1.0 + amount * 3.0);
    vec4 base = texture(source, u);
    vec4 l = texture(source, u - vec2(s, 0.0));
    vec4 r = texture(source, u + vec2(s, 0.0));
    vec4 col = base;
    col += vec4(tintA.rgb, 1.0) * l.a * (1.0 - base.a) * 0.85;
    col += vec4(tintB.rgb, 1.0) * r.a * (1.0 - base.a) * 0.85;
    fragColor = col * qt_Opacity;
}
