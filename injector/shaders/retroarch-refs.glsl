#version 120

uniform sampler2D Texture;
uniform sampler2D OrigTexture;
uniform sampler2D Pass1Texture;
uniform sampler2D PassPrev2Texture;
uniform vec2 OrigTextureSize;
uniform vec2 Pass1TextureSize;
uniform vec2 PassPrev2TextureSize;
varying vec2 vTexCoord;

void main()
{
    vec4 a = texture2D(OrigTexture, vTexCoord);
    vec4 b = texture2D(Pass1Texture, vTexCoord);
    vec4 c = texture2D(PassPrev2Texture, vTexCoord);
    float valid =
        step(0.5, OrigTextureSize.x) *
        step(0.5, Pass1TextureSize.x) *
        step(0.5, PassPrev2TextureSize.x);
    gl_FragColor = ((a + b + c) / 3.0) * valid;
}
