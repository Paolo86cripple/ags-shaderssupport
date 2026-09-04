#version 120
uniform sampler2D TESTLUT;
uniform vec2 TESTLUTSize;
varying vec2 vTexCoord;
void main()
{
    vec4 c = texture2D(TESTLUT, vec2(0.5, 0.5));
    if (abs(TESTLUTSize.x - 1.0) > 0.01 || abs(TESTLUTSize.y - 1.0) > 0.01)
        c = vec4(1.0, 0.0, 1.0, 1.0);
    gl_FragColor = c;
}
