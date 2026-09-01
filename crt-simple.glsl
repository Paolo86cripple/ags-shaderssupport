#version 120

uniform sampler2D uTexture;
uniform vec2 uInputSize;

varying vec2 vTexCoord;

void main()
{
    vec3 color = texture2D(uTexture, vTexCoord).rgb;
    float scanline = 0.94 + 0.06 *
        cos(vTexCoord.y * uInputSize.y * 3.14159265);
    float d = length(vTexCoord - vec2(0.5));
    float vignette = 1.0 - 0.12 * d * d;
    gl_FragColor = vec4(color * scanline * vignette, 1.0);
}
