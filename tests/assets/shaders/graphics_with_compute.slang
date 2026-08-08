// graphics_with_compute.slang
// Compute stage generates triangle vertices into an SSBO.
// Vertex stage reads the SSBO (as vertex buffer) and passes to fragment.
// Fragment stage outputs a solid colour.

// ---- Compute Stage ----
struct Vertex {
    float2 position;
    float3 color;
};

[[vk::binding(0, 0)]] RWStructuredBuffer<Vertex> vertices;

[shader("compute")]
[numthreads(1, 1, 1)]
void compMain(uint3 id : SV_DispatchThreadID) {
    if (id.x == 0) {
        // Quad covering the entire screen (two triangles)
        vertices[0] = { float2(-1.0, -1.0), float3(1, 0, 0) };
        vertices[1] = { float2( 1.0, -1.0), float3(0, 1, 0) };
        vertices[2] = { float2(-1.0,  1.0), float3(0, 0, 1) };
        vertices[3] = { float2( 1.0,  1.0), float3(1, 0, 1) };
    }
}

// ---- Vertex Stage ----
// The vertex buffer is bound as a vertex buffer, not SSBO.
// The vertex shader uses SV_VertexID to index it.
struct VSOutput {
    float4 position : SV_Position;
    float3 color    : COLOR0;
};

// The SSBO `vertices` is accessible here because it's in the same module.
// However, for vertex pipeline we need to bind it as a vertex buffer.
// We'll instead use a raw vertex buffer. To avoid confusion, we can pass the
// buffer as both SSBO (for compute) and vertex buffer (for graphics) – that's
// allowed. So the vertex shader can just read the SSBO.
// But Vulkan requires vertex buffers to be bound as VkVertexInputBinding.
// We'll use the SSBO in the vertex shader and ignore the vertex binding.
// Vertex shader will read the SSBO using the same binding as compute.

struct VSInput {
    [[vk::location(0)]] float2 position : POSITION;
    [[vk::location(1)]] float3 color    : COLOR0;
};

[shader("vertex")]
[export("vertexMain")]
VSOutput vertexMain(VSInput input) {
    VSOutput output;
    output.position = float4(input.position, 0.0, 1.0);
    output.color    = input.color;
    return output;
}

[shader("fragment")]
[export("fragmentMain")]
float4 fragmentMain(VSOutput input) : SV_Target0 {
    return float4(input.color, 1.0);
}
