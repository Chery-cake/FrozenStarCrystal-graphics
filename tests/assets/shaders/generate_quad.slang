// generate_quad.slang – compute shader that writes a full-screen quad

struct Vertex {
    float2 position;
    float3 color;
};

[[vk::binding(0, 0)]] RWStructuredBuffer<Vertex> vertices;

[shader("compute")]
[numthreads(1, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x == 0) {
        // Quad covering the entire screen (two triangles)
        // Color: bright yellow
        vertices[0] = { float2(-1.0, -1.0), float3(1, 1, 0) };
        vertices[1] = { float2( 1.0, -1.0), float3(1, 1, 0) };
        vertices[2] = { float2(-1.0,  1.0), float3(1, 1, 0) };
        vertices[3] = { float2( 1.0,  1.0), float3(1, 1, 0) };
    }
}
