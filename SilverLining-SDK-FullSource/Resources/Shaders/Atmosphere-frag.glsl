uniform vec4 sl_outputScale;

void writeFragmentData(in vec4 finalColor);

void main()
{
    vec4 finalColor = gl_Color;
    finalColor.xyz *= sl_outputScale.xyz;
    writeFragmentData(finalColor);
}
