module;

#include "FrozenStarCrystal-graphics_export.h"

export module graphics.vulkan:device;

import std.compat;
import vulkan;

import :instance;

export namespace graphics::vulkan {

struct FROZENSTARCRYSTAL_GRAPHICS_API QueueFamilyIndices {
    std::optional<uint32_t> graphics;
    std::optional<uint32_t> compute;
    std::optional<uint32_t> transfer;
    std::optional<uint32_t> present;

    [[nodiscard]] bool hasGraphics() const { return graphics.has_value(); }
    [[nodiscard]] bool hasCompute() const { return compute.has_value(); }
    [[nodiscard]] bool hasTransfer() const { return transfer.has_value(); }
    [[nodiscard]] bool hasPresent() const { return present.has_value(); }

    [[nodiscard]] bool isComplete() const {
        return hasGraphics() && hasCompute() && hasTransfer() && hasPresent();
    }
};

struct FROZENSTARCRYSTAL_GRAPHICS_API GPUInfo {
    uint32_t index;
    std::string name;
    vk::PhysicalDeviceType type;
    uint32_t vendorId;
    uint32_t deviceId;
    vk::DeviceSize totalMemory;
    uint32_t apiVersion;
    uint32_t driverVersion;
    QueueFamilyIndices queueFamilies;
};

class FROZENSTARCRYSTAL_GRAPHICS_API GPUDevice {
  private:
    std::unique_ptr<vk::raii::PhysicalDevice> physicalDevice_;
    std::shared_ptr<vk::raii::Device> device_;
    GPUInfo info_;

    vk::Queue graphicsQueue_;
    vk::Queue computeQueue_;
    vk::Queue transferQueue_;
    vk::Queue presentQueue_;

    bool initialized_ = false;
    mutable std::mutex mtx;

    std::vector<std::string> getAvailableExtensions();

  public:
    GPUDevice() = default;
    ~GPUDevice() { shutdown(); }

    // Disable copy & move
    GPUDevice(const GPUDevice &) = delete;
    GPUDevice &operator=(const GPUDevice &) = delete;
    GPUDevice(GPUDevice &&other) = delete;
    GPUDevice &operator=(GPUDevice &&other) = delete;

    bool initialize(const vk::raii::Instance &instance,
                    vk::PhysicalDevice physicalDevice, const GPUInfo &info);
    void shutdown();

    [[nodiscard]] bool isInitialized() const { return initialized_; }
    [[nodiscard]] const GPUInfo &getInfo() const { return info_; }

    [[nodiscard]] std::shared_ptr<vk::raii::Device> getDevicePtr() const {
        return device_;
    }
    [[nodiscard]] vk::PhysicalDevice getPhysicalDevice() const {
        return physicalDevice_ ? **physicalDevice_ : vk::PhysicalDevice{};
    }
    [[nodiscard]] vk::Device getDevice() const {
        return device_ ? **device_ : vk::Device{};
    }

    [[nodiscard]] const vk::raii::PhysicalDevice &
    getRaiiPhysicalDevice() const {
        return *physicalDevice_;
    }
    [[nodiscard]] const vk::raii::Device &getRaiiDevice() const {
        return *device_;
    }

    [[nodiscard]] vk::Queue getGraphicsQueue() const { return graphicsQueue_; }
    [[nodiscard]] vk::Queue getComputeQueue() const { return computeQueue_; }
    [[nodiscard]] vk::Queue getTransferQueue() const { return transferQueue_; }
    [[nodiscard]] vk::Queue getPresentQueue() const { return presentQueue_; }
    [[nodiscard]] const QueueFamilyIndices &getQueueFamilies() const {
        return info_.queueFamilies;
    }

    void waitIdle() const;
};

struct FROZENSTARCRYSTAL_GRAPHICS_API DeviceConfig {
    vk::SurfaceKHR surface;
    bool enableMultiGPU = false;
    uint32_t preferredGPUIndex = 0;
};

class FROZENSTARCRYSTAL_GRAPHICS_API DeviceManager {
  private:
    std::vector<GPUInfo> availableGPUs_;
    std::vector<std::unique_ptr<GPUDevice>> devices_;
    std::shared_ptr<Instance> instance_;
    uint32_t primaryDeviceIndex_ = 0;
    bool initialized_ = false;
    mutable std::mutex deviceManagerMutex_;

    void enumeratePhysicalDevices(const vk::raii::Instance &instance,
                                  vk::SurfaceKHR surface);
    static GPUInfo queryDeviceInfo(vk::PhysicalDevice device, uint32_t index,
                                   vk::SurfaceKHR surface);
    static QueueFamilyIndices findQueueFamilies(vk::PhysicalDevice device,
                                                vk::SurfaceKHR surface);
    static int scoreDevice(const GPUInfo &info);

  public:
    DeviceManager() = default;
    ~DeviceManager() { shutdown(); }

    // Disable move & copy
    DeviceManager(const DeviceManager &) = delete;
    DeviceManager &operator=(const DeviceManager &) = delete;
    DeviceManager(DeviceManager &&) = delete;
    DeviceManager &operator=(DeviceManager &&) = delete;

    bool initialize(std::shared_ptr<Instance> instance,
                    const DeviceConfig &config);

    void shutdown();

    [[nodiscard]] bool isInitialized() const { return initialized_; }

    [[nodiscard]] const std::vector<GPUInfo> &getAvailableGPUs() const {
        return availableGPUs_;
    }

    [[nodiscard]] GPUDevice &getPrimaryDevice() {
        return *devices_[primaryDeviceIndex_];
    }
    [[nodiscard]] const GPUDevice &getPrimaryDevice() const {
        return *devices_[primaryDeviceIndex_];
    }

    [[nodiscard]] GPUDevice *getDevice(uint32_t index);
    [[nodiscard]] const GPUDevice *getDevice(uint32_t index) const;

    [[nodiscard]] const std::vector<std::unique_ptr<GPUDevice>> &
    getDevices() const {
        return devices_;
    }

    [[nodiscard]] size_t getDeviceCount() const { return devices_.size(); }

    [[nodiscard]] bool isMultiGPUEnabled() const { return devices_.size() > 1; }

    void forEachDevice(const std::function<void(GPUDevice &, size_t)>
                           &func); // TODO implement with thread pool manager
};

} // namespace graphics::vulkan
