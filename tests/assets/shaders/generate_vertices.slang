// generate_vertices.slang – compute shader that writes triangle vertex data

struct Vertex {
    float2 position;
    float3 color;
};

[[vk::binding(0, 0)]] RWStructuredBuffer<Vertex> vertices;

[shader("compute")]
[numthreads(1, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    // Single thread writes 3 vertices (a triangle)
    if (id.x == 0) {
        vertices[0] = { float2( 0.0, -0.5), float3(1,0,0) };
        vertices[1] = { float2( 0.5,  0.5), float3(0,1,0) };
        vertices[2] = { float2(-0.5,  0.5), float3(0,0,1) };
    }
}
