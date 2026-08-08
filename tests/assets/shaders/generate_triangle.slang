// generate_triangle.slang – compute shader that writes a triangle

struct Vertex {
    float2 position;
    float3 color;
};

[[vk::binding(0, 0)]] RWStructuredBuffer<Vertex> vertices;

[shader("compute")]
[numthreads(1, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x == 0) {
        vertices[0] = { float2( 0.0, -0.8), float3(1, 0, 0) };  // top, red
        vertices[1] = { float2( 0.8,  0.8), float3(0, 1, 0) };  // right, green
        vertices[2] = { float2(-0.8,  0.8), float3(0, 0, 1) };  // left, blue
    }
}
