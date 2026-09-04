#version 120

#if defined(VERTEX)
uniform mat4 MVPMatrix;
attribute vec4 VertexCoord;
attribute vec4 COLOR;
attribute vec4 TexCoord;
varying vec4 COL0;
varying vec4 TEX0;

void main()
{
    gl_Position = MVPMatrix * VertexCoord;
    COL0 = COLOR;
    TEX0 = TexCoord;
}
#elif defined(FRAGMENT)
uniform sampler2D Texture;
uniform vec2 InputSize;
uniform vec2 TextureSize;
uniform vec2 OutputSize;
varying vec4 COL0;
varying vec4 TEX0;

void main()
{
    vec4 source = texture2D(Texture, TEX0.xy) * COL0;

    bool input_ok = abs(InputSize.x - 3.0) < 0.01 &&
                    abs(InputSize.y - 5.0) < 0.01;
    bool texture_ok = abs(TextureSize.x - 4.0) < 0.01 &&
                      abs(TextureSize.y - 8.0) < 0.01;

    /* RetroArch draws only the valid image rectangle inside the POT backing
     * texture. Undoing that ratio must recover normal viewport coordinates. */
    vec2 logical_uv = TEX0.xy * TextureSize / InputSize;
    vec2 viewport_uv = gl_FragCoord.xy / OutputSize;
    bool coords_ok = all(lessThan(abs(logical_uv - viewport_uv), vec2(0.04)));

    gl_FragColor = input_ok && texture_ok && coords_ok
        ? source
        : vec4(1.0, 0.0, 1.0, 1.0);
}
#endif
