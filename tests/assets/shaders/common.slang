// Common definitions shared between shader stages

struct VSOutput
{
    float4 position : SV_Position;
    float3 color    : COLOR0;
};

// We’ll use vertex‑index pulling – no vertex buffer needed.
// The vertex shader will generate positions from gl_VertexIndex.
