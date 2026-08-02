// media_common.slang — Bindless media layer for the graphics engine.
//
// This file provides the GPU-side counterpart to the CPU-side media system
// (ImageArrayRegistry / MediaRegistry).  Import it in any shader that needs
// to sample from the bindless media arrays.
//
// Shader set layout (set = 1, matching ImageArrayRegistry channel bindings):
//
//   binding 0  — persistent sampler
//   binding 1  — MediaRecord structured buffer
//   binding 2  — MediaLayer structured buffer
//   binding 3  — bindless_images     (eImage2D   channel)
//   binding 4  — bindless_atlases    (eImageAtlas channel)
//   binding 5  — bindless_video_y    (eVideoFrame luma)
//   binding 6  — bindless_video_uv   (eVideoFrame chroma)
//   binding 7  — bindless_arrays     (eImageArray / eVideoStream)
//   binding 8  — bindless_cubes      (eImageCube  channel)
//   binding 9  — bindless_hdr        (eImageHDR   channel)

// -------------------------------------------------------------------------
// Enumerations — must stay in sync with MediaKind and blendMode on the CPU
// -------------------------------------------------------------------------

// Binding-slot constants that mirror ImageArrayRegistry::ChannelDesc::binding.
static const uint kSlotImages   = 3;
static const uint kSlotAtlases  = 4;
static const uint kSlotVideoY   = 5;
static const uint kSlotVideoUV  = 6;
static const uint kSlotArrays   = 7;
static const uint kSlotCubes    = 8;
static const uint kSlotHDR      = 9;

// Blend mode identifiers stored in MediaLayer::blendMode.
static const uint kBlendNormal   = 0;
static const uint kBlendAdditive = 1;
static const uint kBlendMultiply = 2;
static const uint kBlendScreen   = 3;

// -------------------------------------------------------------------------
// GPU-side structs (match CPU layout: packed, std430)
// -------------------------------------------------------------------------

// One logical media item — references a contiguous run of MediaLayer entries.
struct MediaRecord {
    uint firstLayer;   // index into mediaLayers[]
    uint layerCount;   // number of layers in this record
    uint reserved0;
    uint reserved1;
};

// One compositing layer within a MediaRecord.
struct MediaLayer {
    uint  imageIndex;    // slot in the relevant bindless array
    uint  bindingSlot;   // which bindless array (kSlot* constants above)
    uint  arrayLayer;    // for eImageArray / eVideoStream
    uint  flags;

    float uvOffsetX;
    float uvOffsetY;
    float uvScaleX;
    float uvScaleY;

    float tintR;
    float tintG;
    float tintB;
    float tintA;

    float rotation;
    float scaleX;
    float scaleY;
    uint  blendMode;
};

// -------------------------------------------------------------------------
// Bindless resource declarations  (set = 1)
// -------------------------------------------------------------------------

[[vk::binding(0, 1)]] SamplerState                   bindless_sampler;
[[vk::binding(1, 1)]] StructuredBuffer<MediaRecord>  mediaRecords;
[[vk::binding(2, 1)]] StructuredBuffer<MediaLayer>   mediaLayers;
[[vk::binding(3, 1)]] Texture2D                      bindless_images[];
[[vk::binding(4, 1)]] Texture2D                      bindless_atlases[];
[[vk::binding(5, 1)]] Texture2D                      bindless_video_y[];
[[vk::binding(6, 1)]] Texture2D                      bindless_video_uv[];
[[vk::binding(7, 1)]] Texture2DArray                 bindless_arrays[];
[[vk::binding(8, 1)]] TextureCube                    bindless_cubes[];
[[vk::binding(9, 1)]] Texture2D                      bindless_hdr[];

// -------------------------------------------------------------------------
// Helpers
// -------------------------------------------------------------------------

// Apply a MediaLayer's UV transform (scale → rotate → offset).
float2 applyTransform(float2 uv, MediaLayer L) {
    // Scale around centre
    float2 c   = float2(0.5, 0.5);
    float2 suv = (uv - c) * float2(L.uvScaleX * L.scaleX,
                                    L.uvScaleY * L.scaleY) + c;

    // Rotate around centre
    float sinR = sin(L.rotation);
    float cosR = cos(L.rotation);
    float2 ruv = float2(
        cosR * (suv.x - c.x) - sinR * (suv.y - c.y) + c.x,
        sinR * (suv.x - c.x) + cosR * (suv.y - c.y) + c.y);

    // Translate
    return ruv + float2(L.uvOffsetX, L.uvOffsetY);
}

// YCbCr (BT.709 limited range) → linear RGB.
// Expects Y  from a single-channel texture (G8_B8R8_2Plane420 luma plane)
// and UV from a two-channel texture (chroma plane).
float4 sampleYCbCr(uint imageIndex, float2 uv) {
    float  Y  = bindless_video_y [NonUniformResourceIndex(imageIndex)].Sample(bindless_sampler, uv).r;
    float2 UV = bindless_video_uv[NonUniformResourceIndex(imageIndex)].Sample(bindless_sampler, uv).rg;

    // BT.709 limited-range coefficients
    Y  = Y  - 16.0 / 255.0;
    UV = UV - 128.0 / 255.0;

    float r = 1.164 * Y +              1.793 * UV.y;
    float g = 1.164 * Y - 0.213 * UV.x - 0.533 * UV.y;
    float b = 1.164 * Y + 2.112 * UV.x;

    return float4(clamp(r, 0.0, 1.0),
                  clamp(g, 0.0, 1.0),
                  clamp(b, 0.0, 1.0),
                  1.0);
}

// Convert a 2-D UV + cube-face index to a 3-D cube sample direction.
float3 uvToCubeDir(float2 uv, uint face) {
    float2 s = uv * 2.0 - 1.0; // [-1, 1]
    switch (face) {
        case 0: return normalize(float3( 1,  s.y, -s.x)); // +X
        case 1: return normalize(float3(-1,  s.y,  s.x)); // -X
        case 2: return normalize(float3( s.x,  1, -s.y)); // +Y
        case 3: return normalize(float3( s.x, -1,  s.y)); // -Y
        case 4: return normalize(float3( s.x,  s.y,  1)); // +Z
        default: return normalize(float3(-s.x,  s.y, -1)); // -Z
    }
}

// Composite two premultiplied-alpha colour values using the given blend mode.
float4 blendLayer(float4 base, float4 layer, uint mode) {
    switch (mode) {
        case kBlendAdditive: return base + layer;
        case kBlendMultiply: return base * layer;
        case kBlendScreen:   return 1.0 - (1.0 - base) * (1.0 - layer);
        default:             // kBlendNormal — over operator
            return layer + base * (1.0 - layer.a);
    }
}

// -------------------------------------------------------------------------
// Public API
// -------------------------------------------------------------------------

// Sample a media record by ID, compositing all its layers onto a transparent
// background.  `time` is unused for static images; pass the current time in
// seconds for animated or procedural layers.
float4 sampleMedia(uint mediaId, float2 uv, float time) {
    (void)time; // reserved for future animated-layer support

    MediaRecord rec = mediaRecords[mediaId];
    float4 result   = float4(0.0, 0.0, 0.0, 0.0);

    for (uint i = 0; i < rec.layerCount; i++) {
        MediaLayer L        = mediaLayers[rec.firstLayer + i];
        float2     sampleUv = applyTransform(uv, L);
        float4     color;

        switch (L.bindingSlot) {
            case kSlotImages:
                color = bindless_images[NonUniformResourceIndex(L.imageIndex)]
                            .Sample(bindless_sampler, sampleUv);
                break;
            case kSlotAtlases:
                color = bindless_atlases[NonUniformResourceIndex(L.imageIndex)]
                            .Sample(bindless_sampler, sampleUv);
                break;
            case kSlotVideoY:  // YCbCr — luma binding drives the full sample
                color = sampleYCbCr(L.imageIndex, sampleUv);
                break;
            case kSlotArrays:
                color = bindless_arrays[NonUniformResourceIndex(L.imageIndex)]
                            .Sample(bindless_sampler,
                                    float3(sampleUv, float(L.arrayLayer)));
                break;
            case kSlotCubes:
                color = bindless_cubes[NonUniformResourceIndex(L.imageIndex)]
                            .Sample(bindless_sampler,
                                    uvToCubeDir(sampleUv, L.arrayLayer));
                break;
            case kSlotHDR:
                color = bindless_hdr[NonUniformResourceIndex(L.imageIndex)]
                            .Sample(bindless_sampler, sampleUv);
                break;
            default:
                color = float4(1.0, 0.0, 1.0, 1.0); // magenta = unknown type
                break;
        }

        result = blendLayer(
            result,
            color * float4(L.tintR, L.tintG, L.tintB, L.tintA),
            L.blendMode);
    }

    return result;
}
