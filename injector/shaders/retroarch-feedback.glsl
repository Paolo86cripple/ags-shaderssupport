#version 120

uniform sampler2D FeedbackTexture;
uniform vec2 FeedbackTextureSize;
varying vec2 vTexCoord;

void main()
{
    vec4 c = texture2D(FeedbackTexture, vTexCoord);
    float valid = step(0.5, FeedbackTextureSize.x) * step(0.5, FeedbackTextureSize.y);
    gl_FragColor = c * valid;
}
