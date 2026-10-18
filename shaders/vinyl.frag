#version 440
// Vinyl record: grooves with a few track gaps and a fixed light sheen (the light
// does not turn with the record, so this layer never needs to rotate).
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float labelRadius; // 0..1 of the record radius
    vec4 sheenColor;
};

void main()
{
    vec2 p = qt_TexCoord0 * 2.0 - 1.0;
    float r = length(p);
    float aa = 0.006;
    float disc = 1.0 - smoothstep(1.0 - aa, 1.0, r);
    if (disc <= 0.0) { fragColor = vec4(0.0); return; }
    vec3 col = vec3(0.035);
    // grooves
    float g = sin(r * 900.0) * 0.5 + 0.5;
    col += vec3(0.025) * g;
    // track gaps (smoother, darker rings)
    for (int i = 0; i < 5; ++i) {
        float tr = mix(labelRadius + 0.06, 0.95, float(i) / 4.0);
        col -= vec3(0.02) * (1.0 - smoothstep(0.0, 0.006, abs(r - tr)));
    }
    // run-out groove and outer lip
    col += vec3(0.05) * (1.0 - smoothstep(0.0, 0.01, abs(r - 0.985)));
    // fixed sheen: two opposite fans of light
    float a = atan(p.y, p.x);
    float fan = pow(abs(cos(a - 0.7)), 28.0);
    float ring = smoothstep(labelRadius + 0.02, labelRadius + 0.12, r) * (1.0 - smoothstep(0.9, 1.0, r));
    col += sheenColor.rgb * fan * ring * 0.38;
    col += sheenColor.rgb * pow(abs(cos(a - 0.7)), 4.0) * ring * 0.04;
    // label area left transparent (the cover shows through, turning)
    float hole = smoothstep(labelRadius - aa, labelRadius, r);
    fragColor = vec4(col, 1.0) * disc * hole * qt_Opacity;
}
