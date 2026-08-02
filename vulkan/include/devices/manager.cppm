module;

#include "FrozenStarCrystal-graphics_export.h"

export module graphics.vulkan.devices:manager;

import std.compat;
import concurrency.pool.coroutine;

import :structs;
import :device;

export namespace graphics::vulkan::devices {

class FROZENSTARCRYSTAL_GRAPHICS_API Manager {
public:
  struct DeviceEntry {
    GPUInfo info;
    std::shared_ptr<Device> device;
    std::shared_ptr<vk::raii::PhysicalDevice> physicalDevice;
    uint32_t score = 0;
  };

private:
  std::shared_ptr<vk::raii::Instance> instance_;

  std::vector<DeviceEntry> deviceEntries_;

  mutable std::mutex mtx_;

public:
  Manager(const std::shared_ptr<vk::raii::Instance> &instance);
  ~Manager();

  Manager(const Manager &) = delete;
  Manager &operator=(const Manager &) = delete;
  Manager(Manager &&) = delete;
  Manager &operator=(Manager &&) = delete;

  [[nodiscard]] std::vector<DeviceEntry> getDeviceEntries() {
    std::unique_lock lock(mtx_);
    return deviceEntries_;
  }
  [[nodiscard]] std::optional<DeviceEntry> getEntry(uint32_t index) {
    std::unique_lock lock(mtx_);
    if (index >= deviceEntries_.size()) {
      return std::nullopt;
    }
    return deviceEntries_[index];
  }
  [[nodiscard]] std::optional<DeviceEntry> getEntry(GPUInfo &info) {
    std::unique_lock lock(mtx_);
    auto it =
        std::ranges::find_if(deviceEntries_, [&info](const DeviceEntry &entry) {
          return entry.info == info;
        });
    if (it != deviceEntries_.end()) {
      return *it;
    }
    return std::nullopt;
  }
  [[nodiscard]] std::optional<DeviceEntry> getEntryGPUId(uint32_t id) {
    std::unique_lock lock(mtx_);
    auto it =
        std::ranges::find_if(deviceEntries_, [&id](const DeviceEntry &entry) {
          return entry.info.deviceId == id;
        });
    if (it != deviceEntries_.end()) {
      return *it;
    }
    return std::nullopt;
  }
  [[nodiscard]] std::vector<DeviceEntry> getDevicesWithWindows() {
    std::unique_lock lock(mtx_);
    return deviceEntries_ | std::views::filter([](const DeviceEntry &entry) {
             return entry.device->getWindows().size() > 0;
           }) |
           std::ranges::to<std::vector<DeviceEntry>>();
  }
  [[nodiscard]] std::vector<DeviceEntry> getDevicesWithoutWindows() {
    std::unique_lock lock(mtx_);
    return deviceEntries_ | std::views::filter([](const DeviceEntry &entry) {
             return entry.device->getWindows().size() == 0;
           }) |
           std::ranges::to<std::vector<DeviceEntry>>();
  }

  template <concurrency::pool::coroutine::policy::Queue QP =
                concurrency::pool::coroutine::policy::Queue::Inline>
  [[nodiscard]] concurrency::pool::coroutine::Scheduler<QP>
  scheduleOnDevice(uint32_t deviceIndex) {
    std::unique_lock lock(mtx_);
    return deviceEntries_.at(deviceIndex).device->schedule<QP>();
  }

  template <typename F>
  [[nodiscard]] std::vector<std::future<void>> broadcastToAllDevices(F &&fn) {
    std::vector<std::shared_ptr<Device>> devices;
    {
      std::unique_lock lock(mtx_);
      devices.reserve(deviceEntries_.size());
      for (const auto &entry : deviceEntries_) {
        devices.push_back(entry.device);
      }
    }

    std::vector<std::future<void>> futures;
    futures.reserve(devices.size());
    for (const auto &device : devices) {
      futures.push_back(device->submit([fn]() mutable { std::invoke(fn); }));
    }
    return futures;
  }
};

} // namespace graphics::vulkan::devices
