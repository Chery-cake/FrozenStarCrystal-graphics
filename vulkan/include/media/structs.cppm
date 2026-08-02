module;

#include "FrozenStarCrystal-graphics_export.h"

export module graphics.vulkan.media:structs;

import std.compat;
import vulkan;

import graphics.vulkan.devices;

export namespace graphics::vulkan::media {

// What kind of media a resource is — drives format selection and shader UV
// addressing.
enum class Kind : uint8_t {
  image2D,       // PNG/JPG/BMP/TGA — single frame, RGBA
  imageHDR,      // EXR/HDR — float16/32
  imageAtlas,    // sprite sheet — UV rect addressing
  imageCube,     // 6-face cubemap
  imageArray,    // array of same-size frames (animation)
  videoFrame,    // one decoded frame of MP4/GIF/WEBM — YCbCr or RGBA
  videoStream,   // multi-frame stream with frame-index addressing
  computeBuffer, // raw buffer output from a compute shader
  depthMap,      // depth/shadow map
};

// A sub-region or time-slice inside a resource.  Replaces the hardcoded atlas
// UV offset/scale with a fully generic UV transform.
struct FROZENSTARCRYSTAL_GRAPHICS_API Region {
  // Atlas / image-array addressing
  float uvOffsetX = 0.F;
  float uvOffsetY = 0.F;
  float uvScaleX = 1.F;
  float uvScaleY = 1.F;

  // Video / animation frame arrays
  uint32_t frameIndex = 0;
  uint32_t arrayLayer = 0;
  uint32_t mipLevel = 0;

  // Cubemap face selection
  uint32_t cubeFace = 0;
};

// Static descriptor — intended to live as a constexpr (or static inline)
// object; its pointer is used as the registry key.
struct FROZENSTARCRYSTAL_GRAPHICS_API Tag {
  std::string_view name;
  Kind kind;
  std::string_view sourcePath; // file path or URI

  // Non-null for atlas / video slices; null means the whole resource.
  const Region *region = nullptr;

  // Hint for resources that should reside on a specific GPU (e.g. compute
  // output).  null = load to primary display device.
  const void *preferredDeviceHint = nullptr;
};

// One GPU-resident copy of a media resource on a specific device.
// Always heap-allocated via std::shared_ptr — multiple MediaResources can
// reference the same GPU allocation.
struct PerDeviceImage {
  std::shared_ptr<devices::Device> device;
  devices::AllocatedImage image;
  std::atomic<bool> uploadPending{false};

  PerDeviceImage() = default;
  ~PerDeviceImage() = default;

  // Non-copyable, non-moveable: always accessed through shared_ptr.
  PerDeviceImage(const PerDeviceImage &) = delete;
  PerDeviceImage &operator=(const PerDeviceImage &) = delete;
  PerDeviceImage(PerDeviceImage &&) = delete;
  PerDeviceImage &operator=(PerDeviceImage &&) = delete;

  [[nodiscard]] bool isValid() const {
    return device != nullptr && image.isValid();
  }
  [[nodiscard]] vk::ImageLayout currentLayout() const {
    return image.currentLayout;
  }
};

// Index into a single bindless descriptor array entry.
struct FROZENSTARCRYSTAL_GRAPHICS_API ImageHandle {
  uint32_t index = ~0U;
  uint32_t binding = 0;

  [[nodiscard]] bool valid() const noexcept { return index != ~0U; }
};

// Index into a single bindless storage-buffer descriptor array entry.
struct FROZENSTARCRYSTAL_GRAPHICS_API BufferHandle {
  uint32_t index = ~0U;
  uint32_t binding = 0;

  [[nodiscard]] bool valid() const noexcept { return index != ~0U; }
};

// Describes one "channel" — a single Texture2D[] (or Buffer[]) binding in
// the bindless set.  Define as many as needed: color, atlas, video_y, …
struct FROZENSTARCRYSTAL_GRAPHICS_API ChannelDesc {
  uint32_t binding;
  uint32_t maxSlots = 4096;
  std::string debugName;
};

} // namespace graphics::vulkan::media
