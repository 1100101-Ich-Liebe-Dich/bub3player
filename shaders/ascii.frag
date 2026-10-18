#version 440
// ASCII art: the cover rendered as characters on a grid. Each cell takes the
// brightness of the image there and picks a 5x5 glyph of matching density.
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec2 cells;        // columns, rows
    vec4 ink;
    vec4 bg;
    float colorMix;    // 0 phosphor ink, 1 colours of the cover
    float contrast;
};
layout(binding = 1) uniform sampler2D source;

// 5x5 glyphs, bit (x + 5 * y), ordered by ink coverage:  . : - + * = o % @ #
int glyph(float v)
{
    int i = int(clamp(v, 0.0, 0.999) * 11.0);
    if (i <= 0) return 0;
    if (i == 1) return 4194304;
    if (i == 2) return 131200;
    if (i == 3) return 14336;
    if (i == 4) return 145536;
    if (i == 5) return 332096;
    if (i == 6) return 459200;
    if (i == 7) return 469440;
    if (i == 8) return 27070835;
    if (i == 9) return 15136302;
    return 11512810;
}

void main()
{
    vec2 c = floor(qt_TexCoord0 * cells);
    vec2 cuv = (c + 0.5) / cells;
    vec2 d = 0.25 / cells;
    vec3 col = (texture(source, cuv + vec2(-d.x, -d.y)).rgb + texture(source, cuv + vec2(d.x, -d.y)).rgb
              + texture(source, cuv + vec2(-d.x, d.y)).rgb + texture(source, cuv + vec2(d.x, d.y)).rgb) * 0.25;
    float lum = dot(col, vec3(0.299, 0.587, 0.114));
    lum = clamp((lum - 0.5) * contrast + 0.5, 0.0, 1.0);

    // position inside the cell on a 5x5 grid with a one-pixel gap (cells are 6 wide, 7 high)
    vec2 f = fract(qt_TexCoord0 * cells) * vec2(6.0, 7.0) - vec2(0.5, 1.0);
    float on = 0.0;
    if (f.x >= 0.0 && f.x < 5.0 && f.y >= 0.0 && f.y < 5.0) {
        int bit = int(f.x) + 5 * int(f.y);
        on = float((glyph(lum) >> bit) & 1);
    }
    vec3 inkCol = mix(ink.rgb, normalize(col + 0.001) * 1.4, colorMix);
    vec3 outc = mix(bg.rgb, inkCol, on);
    fragColor = vec4(outc, 1.0) * qt_Opacity;
}
