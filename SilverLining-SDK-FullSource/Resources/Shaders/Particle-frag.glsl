#define PARTICLE_SHADER
uniform vec4 sl_outputScale;
uniform sampler2D sl_tex2D;

void writeFragmentData(in vec4 finalColor);
void overrideParticleColor(in vec4 textureColor, in vec4 lightColor, inout vec4 particleColor);

void main()
{
    vec4 texel = texture2D(sl_tex2D, gl_TexCoord[0].st);
    vec4 particleColor = texel * gl_Color;

    overrideParticleColor(texel, gl_Color, particleColor);

    particleColor.xyz *= sl_outputScale.xyz;
    writeFragmentData(particleColor);
}