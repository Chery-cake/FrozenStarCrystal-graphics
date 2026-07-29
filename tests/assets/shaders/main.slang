// Main shader module – imports the common definitions
import common;

[shader("vertex")]
[export("vertexMain")]
VSOutput vertexMain(uint vertexID : SV_VertexID)
{
    const float2 positions[3] =
    {
        float2( 0.0, -0.5),
        float2( 0.5,  0.5),
        float2(-0.5,  0.5)
    };
    const float3 colors[3] =
    {
        float3(1, 0, 0),
        float3(0, 1, 0),
        float3(0, 0, 1)
    };

    VSOutput output;
    output.position = float4(positions[vertexID], 0.0, 1.0);
    output.color    = colors[vertexID];
    return output;
}

[shader("fragment")]
[export("fragmentMain")]
float4 fragmentMain(VSOutput input) : SV_Target0
{
    return float4(input.color, 1.0);
}
