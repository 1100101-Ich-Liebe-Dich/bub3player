#version 440
// Compact disc seen through the changer window: silver data side with the
// rainbow of the diffraction grating opposite the light, a clear hub ring
// and a hole in the middle where the (turning) cover label shows through.
// The light is fixed, so this layer never needs to rotate.
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float labelRadius;   // 0..1, area left transparent for the label
    float lightAngle;    // radians
    float sparkle;       // 0..1 extra brightness (beat)
};

vec3 hue2rgb(float h)
{
    vec3 k = clamp(abs(fract(h + vec3(0.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0) - 1.0, 0.0, 1.0);
    return k * k * (3.0 - 2.0 * k);
}

void main()
{
    vec2 p = qt_TexCoord0 * 2.0 - 1.0;
    float r = length(p);
    float aa = 0.008;
    float disc = 1.0 - smoothstep(1.0 - aa, 1.0, r);
    if (disc <= 0.0) { fragColor = vec4(0.0); return; }
    float a = atan(p.y, p.x);

    vec3 col;
    float alpha = 1.0;
    if (r > 0.34) {
        // silver data area
        float base = 0.62 + 0.12 * cos(a * 2.0 - lightAngle * 2.0);
        col = vec3(base) * vec3(0.95, 0.97, 1.0);
        // diffraction rainbow: two opposite fans, hue changes with radius and angle
        float fan = pow(abs(cos(a - lightAngle)), 3.0);
        float h = fract(r * 1.35 + (a - lightAngle) * 0.32 + 0.1);
        col = mix(col, hue2rgb(h) * 1.15, fan * (0.55 + 0.25 * sparkle));
        // specular streak along the light direction
        col += vec3(1.0) * pow(abs(cos(a - lightAngle)), 60.0) * 0.6;
        // faint end-of-data ring and the clear outer rim
        col *= 1.0 - 0.15 * (1.0 - smoothstep(0.0, 0.01, abs(r - 0.9)));
        if (r > 0.965) { col = mix(col, vec3(0.75, 0.78, 0.82), 0.6); alpha = 0.7; }
    } else if (r > labelRadius) {
        // clear polycarbonate hub with the stacking ring
        col = vec3(0.55, 0.58, 0.62);
        alpha = 0.55;
        col += vec3(0.3) * (1.0 - smoothstep(0.0, 0.012, abs(r - 0.3)));
        col += vec3(0.25) * pow(abs(cos(a - lightAngle)), 20.0);
    } else {
        fragColor = vec4(0.0);
        return;
    }
    float hole = smoothstep(labelRadius - aa, labelRadius, r);
    fragColor = vec4(col * alpha, alpha) * disc * hole * qt_Opacity;
}
