module;

export module graphics.vulkan.media:per_device_image;

import std.compat;
import vulkan;
import graphics.vulkan.devices;

export namespace graphics::vulkan::media {

// One GPU-resident copy of a media resource on a specific device.
// Always heap-allocated via std::shared_ptr — multiple MediaResources can
// reference the same GPU allocation.
struct PerDeviceImage {
    std::shared_ptr<devices::Device> device;
    devices::AllocatedImage          image;
    std::atomic<bool>                uploadPending{false};

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

} // namespace graphics::vulkan::media
