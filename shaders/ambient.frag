#version 440
// Cover-coloured ambient glow. Rendered into a tiny offscreen texture and then
// stretched over the window, so it costs almost nothing per frame.
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float strength;
    float aspect;    // window width / height
};
layout(binding = 1) uniform sampler2D source;

void main()
{
    vec2 uv = qt_TexCoord0;
    // "Cover" fit of the square artwork over a wide window.
    vec2 tuv = uv;
    if (aspect > 1.0)
        tuv.y = (uv.y - 0.5) / aspect + 0.5;
    else
        tuv.x = (uv.x - 0.5) * aspect + 0.5;

    vec4 acc = vec4(0.0);
    float wsum = 0.0;
    for (int x = -3; x <= 3; ++x) {
        for (int y = -3; y <= 3; ++y) {
            vec2 o = vec2(float(x), float(y));
            float w = exp(-dot(o, o) / 6.0);
            acc += texture(source, clamp(tuv + o * 0.05, 0.0, 1.0)) * w;
            wsum += w;
        }
    }
    vec4 avg = acc / wsum;                    // premultiplied
    vec3 c = avg.rgb / max(avg.a, 0.001);     // straight colour
    // Lift saturation a little so muddy covers still give a colour.
    float l = dot(c, vec3(0.299, 0.587, 0.114));
    c = clamp(mix(vec3(l), c, 1.35), 0.0, 1.0);
    // Glow pours in from the top left and fades towards the bottom right.
    float fall = 1.0 - smoothstep(0.0, 1.25, length((uv - vec2(0.18, -0.1)) * vec2(1.0, 1.35)));
    float a = strength * fall * avg.a;        // no art -> no glow
    fragColor = vec4(c * a, a) * qt_Opacity;
}
