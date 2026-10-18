#version 440
// Frosted glass: blurs the (already soft) background texture behind the panel,
// tints it, adds a bright rim, a top sheen, a light reflex at the mouse position
// and a soft drop shadow around the panel.
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec4 rect;        // this shader item in background-texture uv (x, y, w, h)
    vec2 itemSize;    // px, including the shadow margin
    float radius;
    float margin;     // shadow margin around the panel, px
    float blur;
    float tintAmount;
    float reflex;
    float shadowAlpha;
    float rim;
    vec4 tint;
    vec2 light;       // mouse position in item px
    vec4 rim1;        // rim colour on the left (iridescent edge)
    vec4 rim2;        // rim colour on the right
};
layout(binding = 1) uniform sampler2D source;

float sdRound(vec2 p, vec2 b, float r)
{
    vec2 q = abs(p) - b + vec2(r);
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

void main()
{
    vec2 p = qt_TexCoord0 * itemSize;
    vec2 halfSize = itemSize * 0.5 - vec2(margin);
    float d = sdRound(p - itemSize * 0.5, halfSize, radius);
    float inside = 1.0 - smoothstep(-0.8, 0.8, d);

    float ds = sdRound(p - itemSize * 0.5 - vec2(0.0, margin * 0.3), halfSize, radius);
    float sh = pow(1.0 - smoothstep(-margin * 0.3, margin, ds), 2.0) * shadowAlpha;

    vec2 uv = rect.xy + qt_TexCoord0 * rect.zw;
    vec2 px = rect.zw / itemSize;
    float r = blur * 46.0;
    vec3 acc = texture(source, uv).rgb * 0.5;
    float w = 0.5;
    for (int i = 0; i < 12; ++i) {
        float a = float(i) * 0.5236;
        float rr = (i % 2 == 0) ? r : r * 0.5;
        acc += texture(source, uv + vec2(cos(a), sin(a)) * rr * px).rgb;
        w += 1.0;
    }
    vec3 col = mix(acc / w * 1.06 + 0.015, tint.rgb, tintAmount);

    float topness = 1.0 - clamp((p.y - margin) / max(1.0, itemSize.y - 2.0 * margin), 0.0, 1.0);
    float rimMask = (1.0 - smoothstep(0.0, 1.5, -d)) * inside;
    vec3 rimCol = mix(rim1.rgb, rim2.rgb, clamp((p.x - margin) / max(1.0, itemSize.x - 2.0 * margin), 0.0, 1.0));
    col += rimCol * rimMask * rim * (0.22 + 0.55 * topness);
    col += vec3(1.0) * 0.03 * topness * topness * inside;
    if (reflex > 0.0) {
        float dist = length(p - light);
        col += vec3(1.0) * reflex * 0.09 * exp(-dist * dist / (2.0 * 170.0 * 170.0)) * inside;
    }

    vec4 glass = vec4(col, 1.0) * inside;
    vec4 shadow = vec4(0.0, 0.0, 0.0, 1.0) * sh * (1.0 - inside);
    fragColor = (glass + shadow) * qt_Opacity;
}
