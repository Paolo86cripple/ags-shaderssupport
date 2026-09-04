#version 120

#if defined(VERTEX)
uniform mat4 MVPMatrix;
attribute vec4 VertexCoord;
attribute vec4 TexCoord;
attribute vec4 LUTTexCoord;
attribute vec4 OrigTexCoord;
attribute vec4 Pass1TexCoord;
varying vec4 TEX0;
varying vec2 ORIG_COORD;
varying vec2 PASS_COORD;
varying vec2 LUT_COORD;

void main()
{
    gl_Position = MVPMatrix * VertexCoord;
    TEX0 = TexCoord;
    ORIG_COORD = OrigTexCoord.xy;
    PASS_COORD = Pass1TexCoord.xy;
    LUT_COORD = LUTTexCoord.xy;
}
#elif defined(FRAGMENT)
uniform sampler2D Texture;
varying vec4 TEX0;
varying vec2 ORIG_COORD;
varying vec2 PASS_COORD;
varying vec2 LUT_COORD;

float centered(vec2 p)
{
    return step(0.25, p.x) * step(p.x, 0.75) *
           step(0.25, p.y) * step(p.y, 0.75);
}

void main()
{
    float valid = centered(ORIG_COORD) * centered(PASS_COORD) * centered(LUT_COORD);
    gl_FragColor = texture2D(Texture, TEX0.xy) * valid;
}
#endif
