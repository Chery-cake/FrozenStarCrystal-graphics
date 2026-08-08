// fill_buffer.slang – simple compute shader that fills a buffer with a constant value

struct PushConstants {
    uint value;   // 32-bit value to write
};

[[vk::push_constant]] PushConstants pc;

[[vk::binding(0, 0)]] RWStructuredBuffer<uint> outBuffer;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    outBuffer[id.x] = pc.value;
}
