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
uniform vec2 TextureSize;
uniform vec2 InputSize;
uniform vec2 OutputSize;
uniform vec2 OriginalSize;
uniform int FrameCount;
uniform int FrameDirection;
varying vec4 COL0;
varying vec4 TEX0;

void main()
{
    gl_FragColor = texture2D(Texture, TEX0.xy) * COL0;
}
#endif
