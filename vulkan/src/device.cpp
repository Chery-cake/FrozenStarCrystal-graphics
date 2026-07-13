module;

#include <cstdio>

module graphics.vulkan;

import std.compat;
import vulkan;

namespace graphics::vulkan {

void GPUDevice::shutdown() {
    if (!initialized_) {
        return;
    }

    if (device_) {
        device_->waitIdle();
    }

    device_.reset();
    physicalDevice_.reset();

    initialized_ = false;
    std::println("[GPUDevice] Shutdown: {}", info_.name);
}

void GPUDevice::waitIdle() const {
    if (device_) {
        device_->waitIdle();
    }
}

bool GPUDevice::initialize(const vk::raii::Instance &instance,
                           vk::PhysicalDevice physicalDeviceHandle,
                           const GPUInfo &info) {
    if (initialized_) {
        std::println(stderr, "[GPUDevice] Already initialized");
        return false;
    }

    info_ = info;

    // Create RAII physical device wrapper
    physicalDevice_ = std::make_unique<vk::raii::PhysicalDevice>(
        instance, physicalDeviceHandle);

    // Collect unique queue families
    std::set<uint32_t> uniqueQueueFamilies;
    if (info.queueFamilies.hasGraphics()) {
        uniqueQueueFamilies.insert(info.queueFamilies.graphics.value());
    }
    if (info.queueFamilies.hasPresent()) {
        uniqueQueueFamilies.insert(info.queueFamilies.present.value());
    }
    if (info.queueFamilies.hasCompute()) {
        uniqueQueueFamilies.insert(info.queueFamilies.compute.value());
    }
    if (info.queueFamilies.hasTransfer()) {
        uniqueQueueFamilies.insert(info.queueFamilies.transfer.value());
    }

    // Create queue create infos
    std::vector<vk::DeviceQueueCreateInfo> queueCreateInfos;
    float queuePriority = 1.0F;
    for (uint32_t family : uniqueQueueFamilies) {
        vk::DeviceQueueCreateInfo queueCreateInfo{
            {}, family, 1, &queuePriority};
        queueCreateInfos.push_back(queueCreateInfo);
    }

    // Get device features
    // TODO see to add optional features
    vk::PhysicalDeviceFeatures deviceFeatures{};
    deviceFeatures.setSamplerAnisotropy(vk::True);
    deviceFeatures.setFillModeNonSolid(vk::True);
    deviceFeatures.setWideLines(vk::True);
    deviceFeatures.setGeometryShader(vk::True);
    deviceFeatures.setTessellationShader(vk::True);
    deviceFeatures.setShaderInt16(vk::True);
    deviceFeatures.setShaderInt64(vk::True);
    // deviceFeatures.setShaderFloat64(vk::True);

    vk::PhysicalDeviceVulkan11Features deviceFeatures11{};
    deviceFeatures11.sType = vk::StructureType::ePhysicalDeviceVulkan11Features;
    deviceFeatures11.setShaderDrawParameters(vk::True);

    vk::PhysicalDeviceVulkan12Features deviceFeatures12{};
    deviceFeatures12.setDescriptorIndexing(vk::True);
    deviceFeatures12.setShaderSampledImageArrayNonUniformIndexing(vk::True);
    deviceFeatures12.setShaderStorageBufferArrayNonUniformIndexing(vk::True);
    deviceFeatures12.setRuntimeDescriptorArray(vk::True);
    deviceFeatures12.setDescriptorBindingPartiallyBound(vk::True);
    deviceFeatures12.setDescriptorBindingVariableDescriptorCount(vk::True);
    deviceFeatures12.setDescriptorBindingSampledImageUpdateAfterBind(vk::True);
    deviceFeatures12.setPNext(&deviceFeatures11);

    vk::PhysicalDeviceVulkan13Features deviceFeatures13{};
    deviceFeatures13.setDynamicRendering(vk::True);
    deviceFeatures13.setPNext(&deviceFeatures12);

    vk::PhysicalDeviceVulkan14Features deviceFeatures14{};
    deviceFeatures14.setPNext(&deviceFeatures13);

    // Prepare extensions
    auto extensionNames = getAvailableExtensions();
    std::vector<const char *> enabledExtensions =
        extensionNames | std::views::transform([](const std::string &ext) {
            return ext.c_str();
        }) |
        std::ranges::to<std::vector<const char *>>();

    if (enabledExtensions.empty()) {
        return false;
    }

    // Create logical device
    vk::DeviceCreateInfo createInfo{{},
                                    queueCreateInfos,
                                    {}, // No layers for device
                                    enabledExtensions,
                                    &deviceFeatures,
                                    &deviceFeatures14};

    try {
        device_ =
            std::make_unique<vk::raii::Device>(*physicalDevice_, createInfo);
    } catch (const vk::SystemError &e) {
        std::println(stderr, "[GPUDevice] Failed to create logical device: {}",
                     e.what());
        return false;
    }

    // Get queues (raw handles)
    if (info.queueFamilies.hasGraphics()) {
        graphicsQueue_ =
            device_->getQueue(info.queueFamilies.graphics.value(), 0);
    }
    if (info.queueFamilies.hasPresent()) {
        presentQueue_ =
            device_->getQueue(info.queueFamilies.present.value(), 0);
    }
    if (info.queueFamilies.hasCompute()) {
        computeQueue_ =
            device_->getQueue(info.queueFamilies.compute.value(), 0);
    }
    if (info.queueFamilies.hasTransfer()) {
        transferQueue_ =
            device_->getQueue(info.queueFamilies.transfer.value(), 0);
    }

    initialized_ = true;
    std::println("[GPUDevice] Initialized: {}", info.name);
    return true;
}

std::vector<std::string> GPUDevice::getAvailableExtensions() {
    auto available = physicalDevice_->enumerateDeviceExtensionProperties();
    std::unordered_set<std::string> available_set;
    std::ranges::for_each(available,
                          [&available_set](const vk::ExtensionProperties &ext) {
                              available_set.emplace(ext.extensionName);
                          });

    const auto &required = Config::instance().getDeviceExtensions();
    const auto &optional = Config::instance().getOptionalDeviceExtensions();

    std::vector<std::string> suported;
    std::vector<std::string> unsuported;

    std::ranges::partition_copy(required.begin(), required.end(),
                                std::back_inserter(suported),
                                std::back_inserter(unsuported),
                                [&available_set](const std::string &ext) {
                                    return available_set.contains(ext);
                                });

    if (!unsuported.empty()) {
        std::println(stderr, "[GPUDevice] Unsupported extensions:");
        std::ranges::for_each(
            unsuported.begin(), unsuported.end(),
            [](const auto &ext) { std::println(stderr, "  - {}", ext); });
        return {};
    }

    std::ranges::partition_copy(optional.begin(), optional.end(),
                                std::back_inserter(suported),
                                std::back_inserter(unsuported),
                                [&available_set](const std::string &ext) {
                                    return available_set.contains(ext);
                                });

    if (!unsuported.empty()) {
        std::println(stderr, "[GPUDevice] Unsupported optional extensions:");
        std::ranges::for_each(
            unsuported.begin(), unsuported.end(),
            [](const auto &ext) { std::println(stderr, "  - {}", ext); });
    }

    return suported;
}

void DeviceManager::shutdown() {
    if (!initialized_) {
        return;
    }

    // Shutdown all devices
    for (auto &device : devices_) {
        device->shutdown();
    }
    devices_.clear();
    availableGPUs_.clear();
    instance_.reset();

    initialized_ = false;
    std::println("[DeviceManager] Shutdown complete");
}

bool DeviceManager::initialize(std::shared_ptr<Instance> instance,
                               const DeviceConfig &config) {
    if (initialized_) {
        std::println(stderr, "[DeviceManager] Already initialized");
        return false;
    }

    if (!instance->isInitialized()) {
        std::println(stderr, "[DeviceManager] VulkanInstance not initialized");
        return false;
    }

    instance_ = instance;

    // Enumerate physical devices
    enumeratePhysicalDevices(instance->getRaiiInstance(), config.surface);

    if (availableGPUs_.empty()) {
        std::println(stderr, "[DeviceManager] No suitable GPUs found");
        return false;
    }

    // Sort GPUs by score
    std::ranges::sort(availableGPUs_.begin(), availableGPUs_.end(),
                      [](const GPUInfo &a, const GPUInfo &b) {
                          return scoreDevice(a) > scoreDevice(b);
                      });

    // Determine which GPUs to use
    std::vector<uint32_t> selectedDevices;

    if (config.enableMultiGPU) {
        // Use all suitable GPUs

        selectedDevices =
            availableGPUs_ |
            std::views::filter(
                [](const GPUInfo &gpu) { // TODO check if only graphics is
                                         // enough, and if it don't need to
                                         // check for present too
                    return gpu.queueFamilies.hasGraphics();
                }) |
            std::views::transform(
                [](const GPUInfo &gpu) { return gpu.index; }) |
            std::ranges::to<std::vector<uint32_t>>();
    } else {
        // Use only the preferred or best GPU
        uint32_t selectedIndex = config.preferredGPUIndex;
        if (selectedIndex >= availableGPUs_.size()) {
            selectedIndex = 0; // Fall back to best GPU
        }
        selectedDevices.push_back(availableGPUs_[selectedIndex].index);
    }

    if (selectedDevices.empty()) {
        std::println(stderr, "[DeviceManager] No suitable GPU found");
        return false;
    }

    if (config.enableMultiGPU) {
        bool onePresent =
            std::ranges::any_of(selectedDevices, [&](const uint32_t &i) {
                return availableGPUs_[i].queueFamilies.hasPresent();
            });
        if (!onePresent) {
            std::println(stderr, "[DeviceManager] No GPU has Present Family");
            return false;
        }
    }

    // Get physical devices from instance
    auto physicalDevices =
        instance->getRaiiInstance().enumeratePhysicalDevices();

    auto validGpuIndices =
        selectedDevices |
        std::views::filter([&physicalDevices](const uint32_t &index) {
            if (index >= physicalDevices.size()) {
                std::println(
                    stderr,
                    "[DeviceManager] GPU index {} out of range (device "
                    "may have been removed)",
                    index);
                return false;
            }
            return true;
        });

    auto selectedGPUs =
        validGpuIndices | std::views::transform([&](const uint32_t &index) {
            auto it = std::ranges::find_if(
                availableGPUs_,
                [&index](const GPUInfo &gpu) { return gpu.index == index; });
            if (it == availableGPUs_.end()) {
                return std::optional<std::tuple<uint32_t, GPUInfo *>>{};
            }
            return std::make_optional(
                std::make_tuple(index, std::addressof(*it)));
        }) |
        std::views::filter([](const auto &opt) { return opt.has_value(); }) |
        std::views::transform([](const auto &opt) { return *opt; });

    size_t idx = 0;
    std::ranges::for_each(
        selectedGPUs.begin(), selectedGPUs.end(), [&](const auto &entry) {
            auto [index, gpuPtr] = entry;
            auto device = std::make_unique<GPUDevice>();
            if (device->initialize(instance->getRaiiInstance(),
                                   *physicalDevices[index], *gpuPtr)) {
                if (idx == 0) {
                    primaryDeviceIndex_ =
                        static_cast<uint32_t>(devices_.size());
                }
                devices_.push_back(std::move(device));
                idx++;
            }
        });

    if (devices_.empty()) {
        std::println(stderr,
                     "[DeviceManager] Failed to create any logical device");
        return false;
    }

    initialized_ = true;
    std::println("[DeviceManager] Initialized with {} device(s)",
                 devices_.size());
    return true;
}

void DeviceManager::enumeratePhysicalDevices(const vk::raii::Instance &instance,
                                             vk::SurfaceKHR surface) {
    auto physicalDevices = instance.enumeratePhysicalDevices();

    availableGPUs_.clear();
    for (uint32_t i = 0; i < physicalDevices.size(); ++i) {
        auto info = queryDeviceInfo(*physicalDevices[i], i, surface);
        availableGPUs_.push_back(info);

        std::println("[DeviceManager] Found GPU {}: {} (Score: {})", i,
                     info.name, scoreDevice(info));
    }
}

GPUInfo DeviceManager::queryDeviceInfo(vk::PhysicalDevice device,
                                       uint32_t index, vk::SurfaceKHR surface) {
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
    info.queueFamilies = findQueueFamilies(device, surface);

    return info;
}

QueueFamilyIndices DeviceManager::findQueueFamilies(vk::PhysicalDevice device,
                                                    vk::SurfaceKHR surface) {
    QueueFamilyIndices indices;

    auto queueFamilies = device.getQueueFamilyProperties();

    for (uint32_t i = 0; i < queueFamilies.size(); ++i) {
        const auto &family = queueFamilies[i];

        // Graphics queue
        if (family.queueFlags & vk::QueueFlagBits::eGraphics) {
            indices.graphics = i;
        }

        // Compute queue (prefer dedicated compute queue)
        if ((family.queueFlags & vk::QueueFlagBits::eCompute) &&
            !(family.queueFlags & vk::QueueFlagBits::eGraphics)) {
            indices.compute = i;
        } else if ((family.queueFlags & vk::QueueFlagBits::eCompute) &&
                   !indices.hasCompute()) {
            indices.compute = i;
        }

        // Transfer queue (prefer dedicated transfer queue)
        if ((family.queueFlags & vk::QueueFlagBits::eTransfer) &&
            !(family.queueFlags & vk::QueueFlagBits::eGraphics) &&
            !(family.queueFlags & vk::QueueFlagBits::eCompute)) {
            indices.transfer = i;
        } else if ((family.queueFlags & vk::QueueFlagBits::eTransfer) &&
                   !indices.hasTransfer()) {
            indices.transfer = i;
        }

        // Present queue
        if (surface) {
            vk::Bool32 presentSupport = false;
            presentSupport = device.getSurfaceSupportKHR(i, surface);
            if (presentSupport) {
                indices.present = i;
            }
        } else {
            // If no surface, assume graphics queue supports present
            if (family.queueFlags & vk::QueueFlagBits::eGraphics) {
                indices.present = i;
            }
        }

        if (indices.isComplete() && indices.hasCompute() &&
            indices.hasTransfer()) {
            break;
        }
    }

    return indices;
}

int DeviceManager::scoreDevice(const GPUInfo &info) {
    int score = 0;

    // Prefer discrete GPUs
    if (info.type == vk::PhysicalDeviceType::eDiscreteGpu) {
        score += 1000;
    } else if (info.type == vk::PhysicalDeviceType::eIntegratedGpu) {
        score += 100;
    }

    // Score based on memory
    score += static_cast<int>(info.totalMemory /
                              (1024 * 1024 * 1024)); // GB of memory

    // Bonus for compute and transfer queues
    if (info.queueFamilies.hasCompute())
        score += 50;
    if (info.queueFamilies.hasTransfer())
        score += 50;

    // Must have required capabilities
    if (!(info.queueFamilies.hasPresent() &&
          info.queueFamilies.hasGraphics())) {
        score = -1;
    }

    return score;
}

GPUDevice *DeviceManager::getDevice(uint32_t index) {
    std::lock_guard<std::mutex> lock(deviceManagerMutex_);
    if (index >= devices_.size()) {
        return nullptr;
    }
    return devices_[index].get();
}

const GPUDevice *DeviceManager::getDevice(uint32_t index) const {
    std::lock_guard<std::mutex> lock(deviceManagerMutex_);
    if (index >= devices_.size()) {
        return nullptr;
    }
    return devices_[index].get();
}

} // namespace graphics::vulkan
