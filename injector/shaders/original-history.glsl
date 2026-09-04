#version 120
uniform sampler2D Texture;
uniform sampler2D OriginalHistoryTexture;
varying vec2 vTexCoord;
void main()
{
    gl_FragColor = texture2D(OriginalHistoryTexture, vTexCoord);
}
