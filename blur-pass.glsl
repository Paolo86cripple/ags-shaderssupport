#version 120

uniform sampler2D uTexture;
uniform vec2 uTexelSize;
varying vec2 vTexCoord;

void main()
{
    vec4 color = texture2D(uTexture, vTexCoord) * 0.36;
    color += texture2D(uTexture, vTexCoord + vec2(uTexelSize.x, 0.0)) * 0.16;
    color += texture2D(uTexture, vTexCoord - vec2(uTexelSize.x, 0.0)) * 0.16;
    color += texture2D(uTexture, vTexCoord + vec2(0.0, uTexelSize.y)) * 0.16;
    color += texture2D(uTexture, vTexCoord - vec2(0.0, uTexelSize.y)) * 0.16;
    gl_FragColor = color;
}
