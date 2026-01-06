uniform mat4 sl_modelViewProj;
uniform vec4 sl_sunPosition;
//uniform mat4 sl_world;
uniform vec4 sl_fadeDistVec;
uniform mat4 sl_billboard;

vec4 overridePosition(in vec4 position);

void main()
{
    //vec4 posWorld = (gl_Vertex * sl_world);
    //vec3 nPos = normalize(posWorld.xyz / posWorld.w);
    //float dp = dot(nPos, sl_sunPosition.xyz);

    float r = sl_fadeDistVec.z + gl_MultiTexCoord0.y;

    vec4 vtx = gl_Vertex;
    vtx.x *= r;
    vtx.y *= r;

    vec4 projPt = sl_modelViewProj * vtx;
    gl_Position = overridePosition(projPt);

    vec3 nPos = normalize(sl_billboard * vtx);
    float dp = dot(nPos, sl_sunPosition.xyz);

    float d = gl_MultiTexCoord0.z;
    vec4 density = vec4(d, d, d, 1.0);

    float fade = sl_fadeDistVec.y;
    if (dp <= 0.0) {
        fade = -dp / sl_fadeDistVec.x;
        fade = clamp(1.0 - fade, 0.0, 1.0);
    }

    vec4 fadeColor = vec4(fade, fade, fade, 1.0);
    vec4 baseColor = vec4(0.5, 0.5, 1.0, 1.0);

    gl_FrontColor = baseColor * density * fadeColor;
}
