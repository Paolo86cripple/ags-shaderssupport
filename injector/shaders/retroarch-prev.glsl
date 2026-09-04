#version 120

uniform sampler2D PrevTexture;
uniform vec2 PrevTextureSize;
varying vec2 vTexCoord;

void main()
{
    vec4 c = texture2D(PrevTexture, vTexCoord);
    float valid = step(0.5, PrevTextureSize.x) * step(0.5, PrevTextureSize.y);
    gl_FragColor = c * valid;
}
