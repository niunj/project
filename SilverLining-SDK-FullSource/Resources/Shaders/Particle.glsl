#define PARTICLE_SHADER
uniform mat4 sl_modelViewProj;
uniform vec4 sl_lightingColor;

vec4 overridePosition(in vec4 position);

void main()
{
    gl_FrontColor = sl_lightingColor;

    gl_Position = overridePosition(sl_modelViewProj * vec4(gl_Vertex.x, gl_Vertex.y, gl_Vertex.z, 1.0));

    gl_TexCoord[0] = gl_MultiTexCoord0;
}
