module;

#include "FrozenStarCrystal-graphics_export.h"

export module graphics.vulkan.media:bufferArrayRegistry;

import std.compat;
import vulkan;

import :structs;

export namespace graphics::vulkan::media {

// --------------------------------------------------------------------------
// BufferArrayRegistry — parallel registry for storage-buffer descriptors.
// Useful for audio waveforms, video bitstream data, and compute outputs read
// by shaders as raw byte arrays.
//
// Same slot / free-list / dirty-commit design as ImageArrayRegistry.
// --------------------------------------------------------------------------
class FROZENSTARCRYSTAL_GRAPHICS_API BufferArrayRegistry {
private:
  struct BufferEntry {
    vk::Buffer buffer{};
    vk::DeviceSize offset = 0;
    vk::DeviceSize range = 0;
  };

  struct ChannelState {
    ChannelDesc desc;
    std::vector<BufferEntry> slots;
    std::vector<uint32_t> freeList;
    uint32_t nextSlot = 0;
    std::unordered_map<vk::Buffer, uint32_t> bufferToSlot;
    std::unordered_set<uint32_t> dirtySlots;
  };

  std::shared_ptr<vk::raii::Device> device_;

  std::unordered_map<uint32_t, ChannelState> channels_;
  std::unique_ptr<vk::raii::DescriptorSetLayout> layout_;
  std::unique_ptr<vk::raii::DescriptorPool> pool_;
  std::unique_ptr<vk::raii::DescriptorSet> set_;
  mutable std::mutex mutex_;

public:
  BufferArrayRegistry(const std::shared_ptr<vk::raii::Device> &device,
                      ChannelDesc &desc);
  ~BufferArrayRegistry() = default;

  BufferArrayRegistry(const BufferArrayRegistry &) = delete;
  BufferArrayRegistry &operator=(const BufferArrayRegistry &) = delete;
  BufferArrayRegistry(BufferArrayRegistry &&) noexcept = default;
  BufferArrayRegistry &operator=(BufferArrayRegistry &&) noexcept = default;

  void defineChannel(ChannelDesc desc);

  [[nodiscard]] BufferHandle registerBuffer(uint32_t binding, vk::Buffer buffer,
                                            vk::DeviceSize offset,
                                            vk::DeviceSize range);
  void removeBuffer(BufferHandle handle);
  void commitDescriptors(const vk::raii::Device &device);

  [[nodiscard]] vk::DescriptorSet bindlessSet() const;
  [[nodiscard]] vk::DescriptorSetLayout bindlessLayout() const;
};

} // namespace graphics::vulkan::media
