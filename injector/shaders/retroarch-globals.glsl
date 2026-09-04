#version 120

uniform sampler2D Texture;
uniform vec2 FinalViewportSize;
uniform int FrameTimeDelta;
uniform float OriginalFPS;
uniform int Rotation;
uniform float OriginalAspect;
uniform float OriginalAspectRotated;
uniform vec3 Gyroscope;
uniform vec3 Accelerometer;
uniform vec3 AccelerometerRest;
varying vec2 vTexCoord;

#ifdef _HAS_ORIGINALASPECT_UNIFORMS
#define HAS_ASPECT 1.0
#else
#define HAS_ASPECT 0.0
#endif

#ifdef _HAS_FRAMETIME_UNIFORMS
#define HAS_FRAMETIME 1.0
#else
#define HAS_FRAMETIME 0.0
#endif

#ifdef _HAS_SENSOR_UNIFORMS
#define HAS_SENSORS 1.0
#else
#define HAS_SENSORS 0.0
#endif

void main()
{
    float valid = HAS_ASPECT * HAS_FRAMETIME * HAS_SENSORS;
    if (FinalViewportSize.x < 64.0 || FinalViewportSize.y < 64.0) valid = 0.0;
    if (FrameTimeDelta <= 0 || OriginalFPS <= 0.0) valid = 0.0;
    if (Rotation != 0) valid = 0.0;
    if (OriginalAspect < 0.99 || OriginalAspect > 1.01) valid = 0.0;
    if (OriginalAspectRotated < 0.99 || OriginalAspectRotated > 1.01) valid = 0.0;
    if (length(Gyroscope) > 0.0001) valid = 0.0;
    if (length(Accelerometer) > 0.0001) valid = 0.0;
    if (length(AccelerometerRest) > 0.0001) valid = 0.0;
    gl_FragColor = texture2D(Texture, vTexCoord) * valid;
}
