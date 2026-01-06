// Comment this out to disable dithering (to combat banding artifacts)
#define DITHER

#ifdef SKY_SHADER
// Allow overriding of the final sky fragment color
void overrideSkyFragColor(inout vec4 finalColor)
{
}
#endif

#ifdef STRATUS_SHADER
// Allows overriding of the fog color, fog blend factor, underlying cloud color, and alpha blending of the cloud.
void overrideStratusLighting(in vec3 fogColor, in float fogFactor, in vec3 cloudColor, in float cloudFade, inout vec4 finalColor)
{

}
#endif

#ifdef STRATOCUMULUS_SHADER
// Overrides fragment colors in stratocumulus clouds. The pre-lit color (which incorporates light scattering within the cloud) is
// given as well as the vertex color that contains blending information. These are multiplied together to provide the default finalColor.
// finalColor = color * vertexColor;
void overrideStratocumulus(in vec4 color, in vec4 vertexColor, inout vec4 finalColor)
{

}
#endif


#ifdef BILLBOARD_SHADER
// Overrides fragment colors of billboards (cloud puffs, sun, moon.)
void overrideBillboardFragment(in vec4 texColor, in vec4 lightColor, inout vec4 finalColor)
{

}
#endif

#ifdef CIRRUS_SHADER
//Overrides the final color of the Cirrus Clouds
// original finalColor is texel * gl_Color
void overrideCirrusColor(in vec4 texel, in vec4 litColor, inout vec4 finalColor)
{

}
#endif

#ifdef PARTICLE_SHADER
// Overrides the particle color used to light rain, snow, and sleet.
void overrideParticleColor(in vec4 textureColor, in vec4 lightColor, inout vec4 particleColor)
{

}
#endif

// Write the final fragment color. Implement this if you need to write to multiple render targets, for example.
void writeFragmentData(in vec4 finalColor)
{
#ifdef DITHER
    // We add a pseudo-random value (tied to screen space to avoid motion aliasing) mapped to +/- 0.5/255 to the final color.
    // This reduces banding artifacts in low light conditions, where 8 bit color isn't enough to smoothly represent
    // the resulting gradients in the sky and clouds.

    float noise = mix(-0.5/255.0, 0.5/255.0, fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453));
    gl_FragColor.xyz = finalColor.xyz + noise;
    gl_FragColor.w = finalColor.w;
#else
    gl_FragColor = finalColor;
#endif
}

