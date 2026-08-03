module;

#include "FrozenStarCrystal-graphics_export.h"

export module graphics.vulkan.media:media_registry;

import std.compat;
import vulkan;
import resource;
import graphics.vulkan.devices;

import :tag;
import :per_device_image;
import :image_array_registry;

export namespace graphics::vulkan::media {

// Everything the engine stores per registered media item.
struct FROZENSTARCRYSTAL_GRAPHICS_API MediaResource {
    MediaKind kind;

    // GPU residency — one entry per device the resource lives on.
    std::vector<std::shared_ptr<PerDeviceImage>> deviceImages;

    // Bindless handles for each device's ImageArrayRegistry entry.
    // Indices correspond 1-to-1 with deviceImages.
    std::vector<ImageHandle> bindlessHandles;

    // CPU-side readback buffer (only populated when CPU access is requested).
    std::optional<devices::AllocatedBuffer> cpuBuffer;

    // Video / animation metadata
    uint32_t currentFrame = 0;
    uint32_t totalFrames  = 1;

    // Canonical GPU format shared by all PerDeviceImage entries.
    vk::Format gpuFormat = vk::Format::eUndefined;
};

// --------------------------------------------------------------------------
// MediaRegistry — thin wrapper over resource::Registry<MediaTag, MediaResource,
// WeakPtrPolicy>.
//
// WeakPtrPolicy: resources auto-evict from VRAM when no caller holds a
// shared_ptr to them.
// --------------------------------------------------------------------------
using MediaRegistryBase =
    resource::Registry<MediaTag, MediaResource,
                       resource::WeakPtrPolicy<MediaTag, MediaResource>>;

class FROZENSTARCRYSTAL_GRAPHICS_API MediaRegistry : public MediaRegistryBase {
public:
    MediaRegistry() = default;
    ~MediaRegistry() override = default;

    // Load a media file, upload it to each target device, and register the
    // resulting views in the given ImageArrayRegistry.
    // Returns a shared_ptr — the resource stays alive as long as the caller
    // holds the pointer.
    std::future<std::shared_ptr<MediaResource>>
    load(const MediaTag              *tag,
         std::span<devices::Device *> targetDevices,
         ImageArrayRegistry          &bindless);

    // Ownership-safe version — keeps devices alive for the duration of the
    // upload.
    std::future<std::shared_ptr<MediaResource>>
    load(const MediaTag                               *tag,
         std::vector<std::shared_ptr<devices::Device>> targetDevices,
         ImageArrayRegistry                           &bindless);

    // Load on computeDevice and automatically mirror to displayDevice using the
    // cross-device transfer pipeline.
    std::future<std::shared_ptr<MediaResource>>
    loadCrossDevice(const MediaTag                        *tag,
                    std::shared_ptr<devices::Device>       computeDevice,
                    std::shared_ptr<devices::Device>       displayDevice,
                    ImageArrayRegistry                    &bindless);

    // For video streams: upload the next decoded frame and advance
    // currentFrame by one (wrapping at totalFrames).
    void advanceFrame(const MediaTag *tag, devices::Device &device);
};

} // namespace graphics::vulkan::media
