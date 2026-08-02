module;

#include <cstdint>

export module graphics.vulkan.devices:semaphores;

import std.compat;
import vulkan;

import :device;

export namespace graphics::vulkan::devices {

inline std::unique_ptr<vk::raii::Semaphore>
createTimelineSemaphore(const vk::raii::Device &device,
                        uint64_t initialValue = 0) {
  vk::SemaphoreTypeCreateInfo typeInfo{vk::SemaphoreType::eTimeline,
                                       initialValue};
  vk::SemaphoreCreateInfo createInfo{};
  createInfo.pNext = &typeInfo;
  return std::make_unique<vk::raii::Semaphore>(device, createInfo);
}

inline void signalTimelineSemaphore(const vk::raii::Device &device,
                                    vk::Semaphore semaphore, uint64_t value) {
  vk::SemaphoreSignalInfo signalInfo{semaphore, value};
  device.signalSemaphore(signalInfo);
}

inline void waitTimelineSemaphore(const vk::raii::Device &device,
                                  vk::Semaphore semaphore, uint64_t value,
                                  uint64_t timeout = UINT64_MAX) {
  vk::SemaphoreWaitInfo waitInfo{vk::SemaphoreWaitFlagBits::eAny, semaphore,
                                 value};
  auto result = device.waitSemaphores(waitInfo, timeout);
  if (result != vk::Result::eSuccess) {
    throw std::runtime_error("Timeline semaphore wait failed");
  }
}

} // namespace graphics::vulkan::devices
