module;

export module graphics.vulkan.media:tag;

import std.compat;

export namespace graphics::vulkan::media {

// What kind of media a resource is — drives format selection and shader UV
// addressing.
enum class MediaKind : uint8_t {
    eImage2D,       // PNG/JPG/BMP/TGA — single frame, RGBA
    eImageHDR,      // EXR/HDR — float16/32
    eImageAtlas,    // sprite sheet — UV rect addressing
    eImageCube,     // 6-face cubemap
    eImageArray,    // array of same-size frames (animation)
    eVideoFrame,    // one decoded frame of MP4/GIF/WEBM — YCbCr or RGBA
    eVideoStream,   // multi-frame stream with frame-index addressing
    eComputeBuffer, // raw buffer output from a compute shader
    eDepthMap,      // depth/shadow map
};

// A sub-region or time-slice inside a resource.  Replaces the hardcoded atlas
// UV offset/scale with a fully generic UV transform.
struct MediaRegion {
    // Atlas / image-array addressing
    float    uvOffsetX  = 0.f;
    float    uvOffsetY  = 0.f;
    float    uvScaleX   = 1.f;
    float    uvScaleY   = 1.f;

    // Video / animation frame arrays
    uint32_t frameIndex = 0;
    uint32_t arrayLayer = 0;
    uint32_t mipLevel   = 0;

    // Cubemap face selection
    uint32_t cubeFace   = 0;
};

// Static descriptor — intended to live as a constexpr (or static inline)
// object; its pointer is used as the registry key.
struct MediaTag {
    std::string_view   name;
    MediaKind          kind;
    std::string_view   sourcePath;           // file path or URI

    // Non-null for atlas / video slices; null means the whole resource.
    const MediaRegion *region              = nullptr;

    // Hint for resources that should reside on a specific GPU (e.g. compute
    // output).  null = load to primary display device.
    const void        *preferredDeviceHint = nullptr;
};

} // namespace graphics::vulkan::media
