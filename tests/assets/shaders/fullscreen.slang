// fullscreen.slang – vertex shader generates a fullscreen triangle,
// fragment shader outputs a push-constant color.

struct VSOutput {
    float4 position : SV_Position;
};

// Push constant block used by the fragment shader.
struct PushConstants {
    float4 color;
};
[[vk::push_constant]] PushConstants pc;

[shader("vertex")]
[export("vs_main")]
VSOutput vs_main(uint vertexID : SV_VertexID)
{
    // Fullscreen triangle: three vertices cover the entire screen.
    const float2 positions[3] = {
        float2(-1.0, -1.0),
        float2( 3.0, -1.0),
        float2(-1.0,  3.0)
    };

    VSOutput output;
    output.position = float4(positions[vertexID], 0.0, 1.0);
    return output;
}

[shader("fragment")]
[export("fs_main")]
float4 fs_main(VSOutput input) : SV_Target0
{
    return pc.color;
}
