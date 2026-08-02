module;

#include "FrozenStarCrystal-graphics_export.h"

export module graphics.vulkan.media:image_array_registry;

import std.compat;
import vulkan;

export namespace graphics::vulkan::media {

// Index into a single bindless descriptor array entry.
struct ImageHandle {
    uint32_t index   = ~0u;
    uint32_t binding = 0;

    [[nodiscard]] bool valid() const noexcept { return index != ~0u; }
};

// Index into a single bindless storage-buffer descriptor array entry.
struct BufferHandle {
    uint32_t index   = ~0u;
    uint32_t binding = 0;

    [[nodiscard]] bool valid() const noexcept { return index != ~0u; }
};

// Describes one "channel" — a single Texture2D[] (or Buffer[]) binding in
// the bindless set.  Define as many as needed: color, atlas, video_y, …
struct ChannelDesc {
    uint32_t    binding;
    uint32_t    maxSlots = 4096;
    std::string debugName;
};

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
public:
    ImageArrayRegistry() = default;
    ~ImageArrayRegistry() = default;

    ImageArrayRegistry(const ImageArrayRegistry &) = delete;
    ImageArrayRegistry &operator=(const ImageArrayRegistry &) = delete;
    ImageArrayRegistry(ImageArrayRegistry &&) noexcept = default;
    ImageArrayRegistry &operator=(ImageArrayRegistry &&) noexcept = default;

    // --- Setup (call before init()) ---

    // Register a channel description.  Must be called before init().
    void defineChannel(ChannelDesc desc);

    // Create VkDescriptorSetLayout, VkDescriptorPool, and allocate one
    // VkDescriptorSet from the defined channels.  Must be called once,
    // after all defineChannel() calls and before any registerImage().
    void init(const vk::raii::Device &device);

    // --- Runtime ---

    // Register a VkImageView into the given channel (by binding slot).
    // Deduplicates: the same VkImageView always returns the same handle.
    // Slots released via removeImage() are recycled through a free list.
    // Returns an invalid handle if the channel is full or unknown.
    [[nodiscard]] ImageHandle registerImage(uint32_t binding,
                                            vk::ImageView view);

    // Release a slot.  The view is removed from the dedup map and the slot
    // is returned to the free list.  The GPU descriptor is left stale until
    // the next commitDescriptors() call.
    void removeImage(ImageHandle handle);

    // Write all dirty descriptors to the set in a single batch call.
    // Must be called with the sampler that will be written for each entry.
    void commitDescriptors(const vk::raii::Device &device, vk::Sampler sampler);

    [[nodiscard]] vk::DescriptorSet       bindlessSet()    const;
    [[nodiscard]] vk::DescriptorSetLayout bindlessLayout() const;

    [[nodiscard]] bool isInitialized() const noexcept { return initialized_; }

private:
    struct ChannelState {
        ChannelDesc                              desc;
        std::vector<vk::ImageView>               slots;    // indexed by slot id
        std::vector<uint32_t>                    freeList;
        uint32_t                                 nextSlot = 0;
        std::unordered_map<vk::ImageView, uint32_t> viewToSlot;
        std::unordered_set<uint32_t>             dirtySlots;
    };

    std::unordered_map<uint32_t, ChannelState>     channels_;
    std::unique_ptr<vk::raii::DescriptorSetLayout> layout_;
    std::unique_ptr<vk::raii::DescriptorPool>      pool_;
    std::unique_ptr<vk::raii::DescriptorSet>       set_;
    bool                                           initialized_ = false;
    mutable std::mutex                             mutex_;
};

// --------------------------------------------------------------------------
// BufferArrayRegistry — parallel registry for storage-buffer descriptors.
// Useful for audio waveforms, video bitstream data, and compute outputs read
// by shaders as raw byte arrays.
//
// Same slot / free-list / dirty-commit design as ImageArrayRegistry.
// --------------------------------------------------------------------------
class FROZENSTARCRYSTAL_GRAPHICS_API BufferArrayRegistry {
public:
    BufferArrayRegistry() = default;
    ~BufferArrayRegistry() = default;

    BufferArrayRegistry(const BufferArrayRegistry &) = delete;
    BufferArrayRegistry &operator=(const BufferArrayRegistry &) = delete;
    BufferArrayRegistry(BufferArrayRegistry &&) noexcept = default;
    BufferArrayRegistry &operator=(BufferArrayRegistry &&) noexcept = default;

    void defineChannel(ChannelDesc desc);
    void init(const vk::raii::Device &device);

    [[nodiscard]] BufferHandle registerBuffer(uint32_t binding,
                                              vk::Buffer buffer,
                                              vk::DeviceSize offset,
                                              vk::DeviceSize range);
    void removeBuffer(BufferHandle handle);
    void commitDescriptors(const vk::raii::Device &device);

    [[nodiscard]] vk::DescriptorSet       bindlessSet()    const;
    [[nodiscard]] vk::DescriptorSetLayout bindlessLayout() const;

    [[nodiscard]] bool isInitialized() const noexcept { return initialized_; }

private:
    struct BufferEntry {
        vk::Buffer     buffer{};
        vk::DeviceSize offset = 0;
        vk::DeviceSize range  = 0;
    };

    struct ChannelState {
        ChannelDesc                              desc;
        std::vector<BufferEntry>                 slots;
        std::vector<uint32_t>                    freeList;
        uint32_t                                 nextSlot = 0;
        std::unordered_map<vk::Buffer, uint32_t> bufferToSlot;
        std::unordered_set<uint32_t>             dirtySlots;
    };

    std::unordered_map<uint32_t, ChannelState>     channels_;
    std::unique_ptr<vk::raii::DescriptorSetLayout> layout_;
    std::unique_ptr<vk::raii::DescriptorPool>      pool_;
    std::unique_ptr<vk::raii::DescriptorSet>       set_;
    bool                                           initialized_ = false;
    mutable std::mutex                             mutex_;
};

} // namespace graphics::vulkan::media
