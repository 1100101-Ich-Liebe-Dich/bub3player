#version 440
// Hi-Fi tower big view backdrop, 90s style, on a dark plastic wall:
// mode 0: oscilloscope - two phosphor traces and a faint graticule
// mode 1: chrome wave  - a polished metal ribbon, like a 90s screensaver
// The traces are sums of sines whose amplitudes follow the (smoothed) bass,
// mids and highs, so they move with the music without ever flashing.
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float time;
    float aspect;
    float mode;
    float bass;
    float mid;
    float high;
    float centre;     // vertical centre of the wave, 0..1
    vec4 ink;
    vec4 wall;
};

float wave(float x, float t, float ph)
{
    float a = 0.018 + bass * 0.07;
    float b = 0.006 + mid * 0.035;
    float c = 0.002 + high * 0.016;
    return a * sin(x * 6.2832 * 1.3 + t * 1.1 + ph)
         + b * sin(x * 6.2832 * 4.7 - t * 2.3 + ph * 1.7)
         + c * sin(x * 6.2832 * 13.0 + t * 4.1 + ph * 2.3);
}

void main()
{
    vec2 uv = qt_TexCoord0;
    float t = time;
    // wall: dark plastic with a soft light from above
    vec3 col = wall.rgb * (0.75 + 0.45 * (1.0 - uv.y));
    col *= 0.85 + 0.15 * smoothstep(1.2, 0.2, length((uv - vec2(0.5, 0.3)) * vec2(aspect * 0.6, 1.0)));

    int m = int(mode + 0.5);
    if (m == 0) {
        // graticule: 10 x 8 divisions over the whole wall, very faint
        vec2 g = vec2(uv.x * 12.0, uv.y * 8.0);
        float lx = 1.0 - smoothstep(0.0, 1.5 * fwidth(g.x), min(fract(g.x), 1.0 - fract(g.x)));
        float ly = 1.0 - smoothstep(0.0, 1.5 * fwidth(g.y), min(fract(g.y), 1.0 - fract(g.y)));
        col += ink.rgb * max(lx, ly) * 0.035;
        // the axis through the trace
        float axis = 1.0 - smoothstep(0.0, 1.5 * fwidth(uv.y), abs(uv.y - centre));
        col += ink.rgb * axis * 0.05;

        // two traces (left / right), distance to the curve via the slope
        for (int k = 0; k < 2; ++k) {
            float ph = float(k) * 1.9;
            float y = centre + wave(uv.x, t, ph) * (k == 0 ? 1.0 : 0.85);
            float e = 0.002;
            float dy = (wave(uv.x + e, t, ph) - wave(uv.x - e, t, ph)) / (2.0 * e) * (k == 0 ? 1.0 : 0.85);
            float d = abs(uv.y - y) / sqrt(1.0 + dy * dy / (aspect * aspect));
            float w = fwidth(uv.y);
            float core = exp(-pow(d / (w * 1.1), 2.0));
            float halo = exp(-d / (w * 9.0)) * 0.35;
            vec3 c = k == 0 ? ink.rgb : mix(ink.rgb, vec3(1.0), 0.35);
            col += c * (core * (k == 0 ? 0.9 : 0.55) + halo * (k == 0 ? 0.5 : 0.3));
        }
    } else if (m == 1) {
        // a polished chrome tube winding along the wall
        float yc = centre + (0.02 + bass * 0.05) * sin(uv.x * 6.2832 * 1.1 + t * 0.6)
                          + (0.006 + mid * 0.02) * sin(uv.x * 6.2832 * 3.3 - t * 1.1);
        float thick = 0.026 + bass * 0.006;
        float v = (uv.y - yc) / thick;                     // -1 top edge .. 1 bottom edge
        float w = fwidth(uv.y) / thick * 1.5;
        float inside = 1.0 - smoothstep(1.0 - w, 1.0 + w, abs(v));
        // soft shadow on the wall below the tube
        float sh = exp(-pow((uv.y - yc - thick * 1.6) / (thick * 1.2), 2.0));
        col *= 1.0 - 0.45 * sh * (1.0 - inside);
        if (inside > 0.0) {
            // reflection of a simple world: bright sky above, dark horizon, warm floor below
            float n = v * 0.5 + 0.5;
            vec3 sky = vec3(0.92, 0.95, 1.0);
            vec3 hor = vec3(0.16, 0.17, 0.2);
            vec3 flo = vec3(0.52, 0.5, 0.48);
            vec3 chrome = mix(sky, mix(sky * 0.6, hor, smoothstep(0.3, 0.5, n)), smoothstep(0.1, 0.5, n));
            chrome = mix(chrome, flo, smoothstep(0.55, 0.95, n));
            chrome += vec3(1.0) * exp(-pow((n - 0.18) / 0.05, 2.0)) * 0.6;   // highlight
            chrome = mix(chrome, chrome * (ink.rgb * 0.6 + 0.4), 0.25);      // tinted by the display colour
            chrome *= 1.0 - 0.35 * pow(abs(v), 6.0);                         // rolled-off edges
            col = mix(col, chrome, inside);
        }
    }
    // fine plastic texture and vignette
    col *= 0.8 + 0.2 * smoothstep(1.3, 0.3, length((uv - 0.5) * vec2(aspect * 0.8, 1.0)));
    fragColor = vec4(col, 1.0) * qt_Opacity;
}
