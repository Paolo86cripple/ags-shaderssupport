#version 120

#pragma parameter Gain "Gain" 0.5 0.0 1.0 0.1

uniform sampler2D BASETexture;
uniform vec2 BASETextureSize;
uniform float Gain;
varying vec2 vTexCoord;

void main()
{
    vec4 c = texture2D(BASETexture, vTexCoord);
    float valid = step(0.5, BASETextureSize.x) * step(0.5, BASETextureSize.y);
    gl_FragColor = vec4(c.rgb * Gain * valid, c.a);
}
