#version 440
// Glass skin backdrop: a deep two-tone gradient with large, very soft colour
// glows (optionally the cover), and soap bubbles rising slowly through it.
// Each bubble is almost clear: a thin film that shimmers in rainbow colours at
// the rim, a soft window reflection, nothing that flashes.
// Rendered twice: crisp for the screen, and with `soft` = 1 into a tiny
// texture that the frosted glass panels blur.
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float time;       // integrated: rises faster when the music is fuller
    float aspect;
    float soft;       // 0 crisp, 1 very soft
    float px;         // one output pixel in height units
    float coverMix;
    float scale;      // bubble size
    float count;      // number of bubbles (4..14)
    float swell;      // 0..1 smoothed music energy
    vec4 base1;
    vec4 base2;
    vec4 c1;
    vec4 c2;
    vec4 c3;
};
layout(binding = 1) uniform sampler2D cover;

float hash1(float n) { return fract(sin(n * 127.1) * 43758.5453); }

vec3 film(float d)
{
    // thin-film interference colours (magenta, gold, green, blue bands)
    return 0.55 + 0.45 * cos(6.2832 * (d + vec3(0.0, 0.33, 0.67)));
}

void main()
{
    vec2 uv = qt_TexCoord0;
    vec2 p = vec2(uv.x * aspect, uv.y);

    // background: gradient, the cover (optional), three slow, very soft glows
    float g = clamp(dot(uv, vec2(0.55, 0.45)), 0.0, 1.0);
    vec3 bg = mix(base1.rgb, base2.rgb, smoothstep(0.0, 1.0, g));
    if (coverMix > 0.0) {
        vec2 cuv = vec2(0.5) + (uv - vec2(0.5)) * vec2(min(1.0, aspect), min(1.0, 1.0 / aspect)) * 0.75;
        bg = mix(bg, texture(cover, cuv).rgb * 0.75, coverMix * 0.8);
    }
    float t = time;
    vec2 g1 = vec2(aspect * (0.22 + 0.06 * sin(t * 0.05)), 0.30 + 0.05 * cos(t * 0.04));
    vec2 g2 = vec2(aspect * (0.80 + 0.05 * cos(t * 0.045)), 0.72 + 0.06 * sin(t * 0.05));
    vec2 g3 = vec2(aspect * (0.55 + 0.07 * sin(t * 0.035 + 2.0)), 0.10 + 0.05 * sin(t * 0.03));
    bg += c1.rgb * 0.42 * exp(-dot(p - g1, p - g1) / (0.16 * (1.0 + swell * 0.15)));
    bg += c2.rgb * 0.40 * exp(-dot(p - g2, p - g2) / 0.2);
    bg += c3.rgb * 0.26 * exp(-dot(p - g3, p - g3) / 0.1);

    // soap bubbles
    int n = int(count + 0.5);
    for (int i = 0; i < 14; ++i) {
        if (i >= n) break;
        float fi = float(i);
        float r = (0.035 + 0.10 * hash1(fi + 3.7)) * scale;
        float speed = 0.018 + 0.022 * hash1(fi + 9.1);
        float cycle = fract(t * speed + hash1(fi + 1.3));
        float y = 1.0 + r - cycle * (1.0 + 2.0 * r + 0.1);
        float x = aspect * (0.06 + 0.88 * hash1(fi + 5.9)) + 0.035 * sin(t * (0.25 + 0.2 * hash1(fi)) + fi * 2.1);
        // a gentle breath with the music
        r *= 1.0 + 0.04 * swell * sin(t * 1.3 + fi);
        vec2 d = p - vec2(x, y);
        float l = length(d) / r;
        if (l > 1.25) continue;
        float aa = (px * 1.2) / r + soft * 0.45;
        float inside = 1.0 - smoothstep(1.0 - aa, 1.0 + aa, l);
        if (inside <= 0.0) continue;
        vec2 q = d / r;
        float ang = atan(q.y, q.x);
        // film thickness swirls slowly; thinner (bluer) at the top
        float thick = 0.9 * l + 0.25 * sin(ang * 2.0 + t * 0.35 + fi) + 0.15 * sin(ang * 3.0 - t * 0.27) + q.y * 0.25;
        vec3 irid = film(thick);
        float fres = pow(clamp(l, 0.0, 1.0), 5.0);
        float a = (0.05 + 0.6 * fres) * inside;
        vec3 col = bg * (1.0 - 0.18 * fres * inside) + irid * a * 0.75;
        // clear interior: a whisper brighter
        col += vec3(0.03) * inside;
        // window reflection top-left and a faint one bottom-right
        float w1 = 1.0 - smoothstep(0.10, 0.26 + soft * 0.2, length((q - vec2(-0.42, -0.44)) * vec2(1.0, 1.35)));
        float w2 = 1.0 - smoothstep(0.03, 0.14 + soft * 0.2, length(q - vec2(0.45, 0.5)));
        col += vec3(1.0) * (w1 * 0.38 + w2 * 0.16) * inside;
        bg = col;
    }

    // the soft copy is what the glass shows: a little darker keeps white text readable
    bg *= mix(1.0, 0.8, soft);
    fragColor = vec4(bg, 1.0) * qt_Opacity;
}
