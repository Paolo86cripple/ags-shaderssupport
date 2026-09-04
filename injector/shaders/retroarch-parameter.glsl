#version 120

#pragma parameter Gain "Gain" 0.5 0.0 1.0 0.1

uniform sampler2D Texture;
varying vec2 vTexCoord;

#ifdef PARAMETER_UNIFORM
uniform float Gain;
#else
#define Gain 0.5
#endif

void main()
{
    gl_FragColor = texture2D(Texture, vTexCoord) * Gain;
}
