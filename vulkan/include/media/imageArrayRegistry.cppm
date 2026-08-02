module;

#include "FrozenStarCrystal-graphics_export.h"

export module graphics.vulkan.media:imageArrayRegistry;

import std.compat;
import vulkan;

import :structs;

export namespace graphics::vulkan::media {

// --------------------------------------------------------------------------
// ImageArrayRegistry — manages a bindless array of combined-image-sampler
// descriptors, one channel per logical image category.
//
// Usage:
//   ImageArrayRegistry reg;
//   reg.defineChannel({.binding = 0, .maxSlots = 1024, .debugName = "color"});
//   reg.init(device);                          // create layout, pool, set
//   ImageHandle h = reg.registerImage(0, view);
//   reg.commitDescriptors(device, sampler);    // flush dirty writes
//   reg.removeImage(h);
// --------------------------------------------------------------------------
class FROZENSTARCRYSTAL_GRAPHICS_API ImageArrayRegistry {
private:
  struct ChannelState {
    ChannelDesc desc;
    std::vector<vk::ImageView> slots; // indexed by slot id
    std::vector<uint32_t> freeList;
    uint32_t nextSlot = 0;
    std::unordered_map<vk::ImageView, uint32_t> viewToSlot;
    std::unordered_set<uint32_t> dirtySlots;
  };

  std::shared_ptr<vk::raii::Device> device_;

  std::unordered_map<uint32_t, ChannelState> channels_;
  std::unique_ptr<vk::raii::DescriptorSetLayout> layout_;
  std::unique_ptr<vk::raii::DescriptorPool> pool_;
  std::unique_ptr<vk::raii::DescriptorSet> set_;
  mutable std::mutex mutex_;

public:
  ImageArrayRegistry(const std::shared_ptr<vk::raii::Device> &device,
                     ChannelDesc &desc);
  ~ImageArrayRegistry() = default;

  ImageArrayRegistry(const ImageArrayRegistry &) = delete;
  ImageArrayRegistry &operator=(const ImageArrayRegistry &) = delete;
  ImageArrayRegistry(ImageArrayRegistry &&) noexcept = default;
  ImageArrayRegistry &operator=(ImageArrayRegistry &&) noexcept = default;

  // --- Setup (call before init()) ---

  // Register a channel description.  Must be called before init().
  void defineChannel(ChannelDesc desc);

  // --- Runtime ---

  // Register a VkImageView into the given channel (by binding slot).
  // Deduplicates: the same VkImageView always returns the same handle.
  // Slots released via removeImage() are recycled through a free list.
  // Returns an invalid handle if the channel is full or unknown.
  [[nodiscard]] ImageHandle registerImage(uint32_t binding, vk::ImageView view);

  // Release a slot.  The view is removed from the dedup map and the slot
  // is returned to the free list.  The GPU descriptor is left stale until
  // the next commitDescriptors() call.
  void removeImage(ImageHandle handle);

  // Write all dirty descriptors to the set in a single batch call.
  // Must be called with the sampler that will be written for each entry.
  void commitDescriptors(vk::Sampler sampler);

  [[nodiscard]] vk::DescriptorSet bindlessSet() const;
  [[nodiscard]] vk::DescriptorSetLayout bindlessLayout() const;
};

} // namespace graphics::vulkan::media
