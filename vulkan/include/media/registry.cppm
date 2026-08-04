module;

#include "FrozenStarCrystal-graphics_export.h"

export module graphics.vulkan.media:registry;

import std.compat;
import vulkan;
import resource;
import graphics.vulkan.devices;

import :structs;
import :imageArrayRegistry;

export namespace graphics::vulkan::media {

// Everything the engine stores per registered media item.
struct FROZENSTARCRYSTAL_GRAPHICS_API Resource {
  Kind kind;

  // GPU residency — one entry per device the resource lives on.
  std::vector<std::shared_ptr<PerDeviceImage>> deviceImages;

  // Bindless handles for each device's ImageArrayRegistry entry.
  // Indices correspond 1-to-1 with deviceImages.
  std::vector<ImageHandle> bindlessHandles;

  // CPU-side readback buffer (only populated when CPU access is requested).
  std::optional<devices::AllocatedBuffer> cpuBuffer;

  // Video / animation metadata
  uint32_t currentFrame = 0;
  uint32_t totalFrames = 1;

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
using RegistryBase =
    resource::Registry<Tag, Resource, resource::WeakPtrPolicy<Tag, Resource>>;

class FROZENSTARCRYSTAL_GRAPHICS_API Registry : public RegistryBase {
public:
  Registry() = default;
  ~Registry() override = default;

  // Load a media file, upload it to each target device, and register the
  // resulting views in the given ImageArrayRegistry.
  // Returns a shared_ptr — the resource stays alive as long as the caller
  // holds the pointer.
  std::future<std::shared_ptr<Resource>>
  load(const Tag *tag,
       std::vector<std::shared_ptr<devices::Device>> targetDevices,
       ImageArrayRegistry &bindless);

  // Load on computeDevice and automatically mirror to displayDevice using the
  // cross-device transfer pipeline.
  std::future<std::shared_ptr<Resource>>
  loadCrossDevice(const Tag *tag,
                  std::shared_ptr<devices::Device> computeDevice,
                  std::shared_ptr<devices::Device> displayDevice,
                  ImageArrayRegistry &bindless);

  // For video streams: upload the next decoded frame and advance
  // currentFrame by one (wrapping at totalFrames).
  void advanceFrame(const Tag *tag, devices::Device &device);
};

} // namespace graphics::vulkan::media
