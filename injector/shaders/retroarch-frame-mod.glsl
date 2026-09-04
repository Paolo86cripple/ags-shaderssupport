#version 120

uniform sampler2D Texture;
uniform int FrameCount;
varying vec2 vTexCoord;

void main()
{
    vec4 c = texture2D(Texture, vTexCoord);
    if (FrameCount == 0)
        gl_FragColor = c;
    else
        gl_FragColor = vec4(1.0 - c.rgb, c.a);
}
