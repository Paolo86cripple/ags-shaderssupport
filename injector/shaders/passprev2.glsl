#version 120
uniform sampler2D PassPrev2Texture;
uniform vec2 PassPrev2TextureSize;
varying vec2 vTexCoord;
void main()
{
    vec4 c = texture2D(PassPrev2Texture, vTexCoord);
    float valid = step(0.5, PassPrev2TextureSize.x) * step(0.5, PassPrev2TextureSize.y);
    gl_FragColor = c * valid;
}
