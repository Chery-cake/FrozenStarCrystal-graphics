module;

module graphics.vulkan.devices;

import vulkan;

namespace graphics::vulkan::devices {

namespace {

uint32_t scoreDevice(const GPUInfo &info) {
  uint32_t score = 0;

  // Prefer discrete GPUs
  if (info.type == vk::PhysicalDeviceType::eDiscreteGpu) {
    score += 1000;
  } else if (info.type == vk::PhysicalDeviceType::eIntegratedGpu) {
    score += 500;
  }

  // Score based on memory
  score += info.totalMemory / (1024 * 1024 * 1024); // GB of memory

  // Bonus for compute and transfer queues
  if (info.queueFamilies.hasCompute()) {
    score += 100;
  }
  if (info.queueFamilies.hasTransfer()) {
    score += 100;
  }
  if (info.queueFamilies.hasGraphics()) {
    score += 100;
  }
  if (info.queueFamilies.hasSparseBinding()) {
    score += 100;
  }
  if (info.queueFamilies.hasProtected()) {
    score += 100;
  }

  return score;
}

QueueFamilyIndices findQueueFamilies(vk::PhysicalDevice device) {
  QueueFamilyIndices indices;

  auto queueFamilies = device.getQueueFamilyProperties();

  // 1. Exclusive helper – family has ONLY the target flag (no graphics, no
  // other bits)
  static auto assignIfExclusive = [](auto i, auto &family,
                                     vk::QueueFlagBits target,
                                     std::optional<uint32_t> &targetIndex) {
    if (family.queueFlags == target) {
      targetIndex = static_cast<uint32_t>(i);
      return true;
    }
    return false;
  };

  // 2. Dedicated helper – family has the target flag but NO graphics
  static auto assignIfDedicated = [](auto i, auto &family,
                                     vk::QueueFlagBits target,
                                     std::optional<uint32_t> &targetIndex) {
    // Note: exclusive already covers the case where only target is set,
    // so this will only trigger when the family has additional non‑graphics
    // flags.
    if ((family.queueFlags & target) &&
        !(family.queueFlags & vk::QueueFlagBits::eGraphics)) {
      targetIndex = static_cast<uint32_t>(i);
      return true;
    }
    return false;
  };

  // 3. Fallback helper – any family with the target flag, if not already set
  static auto assignIfAny = [](auto i, auto &family, vk::QueueFlagBits target,
                               std::optional<uint32_t> &targetIndex) {
    if ((family.queueFlags & target) && !targetIndex.has_value()) {
      targetIndex = static_cast<uint32_t>(i);
    }
  };

  // TODO improve family selection
  std::ranges::for_each(
      queueFamilies | std::views::enumerate, [&indices](const auto &pair) {
        auto [i, family] = pair;

        if (!indices.graphicsQueue &&
            (family.queueFlags & vk::QueueFlagBits::eGraphics)) {
          indices.graphicsQueue = static_cast<uint32_t>(i);
        }

        // Compute: exclusive > dedicated > any
        if (!indices.computeQueue) {
          if (!assignIfExclusive(i, family, vk::QueueFlagBits::eCompute,
                                 indices.computeQueue) &&
              !assignIfDedicated(i, family, vk::QueueFlagBits::eCompute,
                                 indices.computeQueue)) {
            assignIfAny(i, family, vk::QueueFlagBits::eCompute,
                        indices.computeQueue);
          }
        }

        // Transfer: exclusive > dedicated > any
        if (!indices.transferQueue) {
          if (!assignIfExclusive(i, family, vk::QueueFlagBits::eTransfer,
                                 indices.transferQueue) &&
              !assignIfDedicated(i, family, vk::QueueFlagBits::eTransfer,
                                 indices.transferQueue)) {
            assignIfAny(i, family, vk::QueueFlagBits::eTransfer,
                        indices.transferQueue);
          }
        }

        // Sparse binding – just pick the first available
        if (!indices.sparseBindingQueue &&
            (family.queueFlags & vk::QueueFlagBits::eSparseBinding)) {
          indices.sparseBindingQueue = static_cast<uint32_t>(i);
        }

        // Protected – just pick the first available
        if (!indices.protectedQueue &&
            (family.queueFlags & vk::QueueFlagBits::eProtected)) {
          indices.protectedQueue = static_cast<uint32_t>(i);
        }
      });

  return indices;
}

GPUInfo queryDeviceInfo(vk::PhysicalDevice device, uint32_t index) {
  GPUInfo info{};
  info.index = index;

  auto properties = device.getProperties();
  info.name = properties.deviceName.data();
  info.type = properties.deviceType;
  info.vendorId = properties.vendorID;
  info.deviceId = properties.deviceID;
  info.apiVersion = properties.apiVersion;
  info.driverVersion = properties.driverVersion;

  // Get memory info
  auto memoryProperties = device.getMemoryProperties();
  for (uint32_t i = 0; i < memoryProperties.memoryHeapCount; ++i) {
    if (memoryProperties.memoryHeaps[i].flags &
        vk::MemoryHeapFlagBits::eDeviceLocal) {
      info.totalMemory += memoryProperties.memoryHeaps[i].size;
    }
  }

  // Find queue families
  info.queueFamilies = findQueueFamilies(device);

  return info;
}

std::vector<std::pair<GPUInfo, vk::PhysicalDevice>>
enumeratePhysicalDevices(vk::raii::Instance *instance) {
  return instance->enumeratePhysicalDevices() | std::views::enumerate |
         std::views::transform([](const auto &pair) {
           auto [i, physicalDevice] = pair;
           GPUInfo info =
               queryDeviceInfo(physicalDevice, static_cast<uint32_t>(i));
           std::println("[Manager] Found GPU {}: {}", i, info.name);
           return std::pair<GPUInfo, vk::PhysicalDevice>{info, physicalDevice};
         }) |
         std::ranges::to<std::vector>();
}

} // namespace

Manager::Manager(const std::shared_ptr<vk::raii::Instance> &instance)
    : instance_(instance) {

  auto infoDev = enumeratePhysicalDevices(instance.get());

  if (infoDev.empty()) {
    throw std::runtime_error("[Manager] No GPU found");
  }

  std::ranges::for_each(infoDev, [&entries = deviceEntries_,
                                  &instance](const auto &pair) {
    auto [info, physicalDevice] = pair;

    auto device = std::make_shared<Device>(*instance, physicalDevice, info);
    auto phyDev =
        std::make_shared<vk::raii::PhysicalDevice>(*instance, physicalDevice);

    entries.emplace_back(info, device, phyDev, scoreDevice(info));
  });

  std::ranges::sort(deviceEntries_,
                    [](const DeviceEntry &a, const DeviceEntry &b) {
                      return a.score > b.score;
                    });

  std::println("[Manager] Initialized with {} device(s)",
               deviceEntries_.size());
}

Manager::~Manager() {

  deviceEntries_.clear();

  instance_.reset();

  std::cout << "[Manager] Cleared\n";
}

} // namespace graphics::vulkan::devices
