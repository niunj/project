layout(set=0,binding=10) uniform CloudTemporal {
    mat4 rayMatrix;
    mat4 previousClip;
    vec4 screen;       // output width / height / valid history / phase
    vec4 motion;       // world wind displacement / delta seconds
    vec4 eyeDelta;     // current eye - previous eye
    vec4 control;      // history weight / exposure / jitter XY in raw pixels
    vec4 resolution;   // raw width / height / projection depth coefficients
    vec4 fog;
    vec4 fogLight;
    vec4 eyeEarth;
} temporal;
