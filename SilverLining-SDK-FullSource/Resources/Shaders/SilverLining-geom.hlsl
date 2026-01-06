float gLineWidthNDC;

struct SL_Vertex {
    float4 pos :
    SV_Position;
    float4 color :
    COLOR0;
    float3 tex :
    TEXTURE0;
    float fogLerp :
    FOG;
};

[maxvertexcount(4)]
void main(line SL_Vertex input[2], inout TriangleStream<SL_Vertex> WideLineStream)
{
    SL_Vertex output;

    float4 ndc[2];
    // Find direction perpendicular to the line
    ndc[0] = input[0].pos / input[0].pos.w;
    ndc[1] = input[1].pos / input[1].pos.w;

    float3 z = float3(0, 0, 1);
    float4 dir = ndc[1] - ndc[0];
    dir.z = 0;
    float3 y = normalize(dir.xyz);

    float3 x3 = cross(z, y);
    float4 x = float4(x3, 1.0);
    //
    // Emit two new verts
    //
    for (int i = 0; i<2; i++) {
        float4 ndc1 = ndc[i] - (x * gLineWidthNDC * 0.5);
        float4 position1 = ndc1 * input[i].pos.w;

        float4 ndc2 = ndc1 + (x * gLineWidthNDC);
        float4 position2 = ndc2 * input[i].pos.w;

        output.pos = position2;

        output.color = input[i].color;

        output.tex = input[i].tex;

        output.fogLerp = input[i].fogLerp;

        WideLineStream.Append(output);

        output.pos = position1;

        WideLineStream.Append(output);

    }

}