#version 120
#pragma parameter Gain "Gain" 0.5 0.0 1.0 0.1
uniform sampler2D BASE;
uniform vec2 BASESize;
uniform float Gain;
varying vec2 vTexCoord;
void main()
{
    vec4 c = texture2D(BASE, vTexCoord);
    gl_FragColor = vec4(c.rgb * Gain, c.a);
}
