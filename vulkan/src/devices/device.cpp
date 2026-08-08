module;

#include <cstdint>
#include <vk_mem_alloc.h>

module graphics.vulkan.devices;

import std.compat;
import vulkan;
import vk_mem_alloc;
import graphics.vulkan.instances;

import concurrency;

namespace graphics::vulkan::devices {

namespace {

std::optional<uint32_t> findPresentQueue(vk::raii::SurfaceKHR *surface,
                                         vk::raii::PhysicalDevice *device) {
  auto queueFamilies = device->getQueueFamilyProperties();

  auto view = queueFamilies | std::views::enumerate;

  auto it = std::ranges::find_if(view, [&surface, &device](const auto &pair) {
    auto &&[i, family] = pair;
    return device->getSurfaceSupportKHR(static_cast<uint32_t>(i), *surface) !=
           0;
  });

  if (it == view.end()) {
    return std::nullopt;
  }
  return static_cast<uint32_t>(std::get<0>(*it));
}

} // namespace

struct TouchedDevice {
  Device *device;
  std::weak_ptr<uint8_t> alive;
};
thread_local std::vector<TouchedDevice> touchedDevices;

struct ThreadPoolCleanup {
  ~ThreadPoolCleanup() {

    std::ranges::for_each(touchedDevices, [](const auto &entry) {
      if (entry.device != nullptr && !entry.alive.expired()) {
        entry.device->removeThreadPools(std::this_thread::get_id());
      }
    });
  }
};
thread_local ThreadPoolCleanup cleanup;

Device::Device(const vk::raii::Instance &instance,
               vk::PhysicalDevice physicalDevice, const GPUInfo &info,
               const std::shared_ptr<concurrency::pool::ThreadPool> &gpuPool)
    : info_(info), gpuPool_(gpuPool),
      aliveToken_(std::make_shared<uint8_t>(0)) {

  physicalDevice_ =
      std::make_shared<vk::raii::PhysicalDevice>(instance, physicalDevice);

  // Collect unique queue families
  std::set<uint32_t> uniqueQueueFamilies;
  if (info.queueFamilies.hasGraphics()) {
    uniqueQueueFamilies.insert(info.queueFamilies.graphicsQueue.value());
  }
  if (info.queueFamilies.hasCompute()) {
    uniqueQueueFamilies.insert(info.queueFamilies.computeQueue.value());
  }
  if (info.queueFamilies.hasTransfer()) {
    uniqueQueueFamilies.insert(info.queueFamilies.transferQueue.value());
  }
  if (info.queueFamilies.hasSparseBinding()) {
    uniqueQueueFamilies.insert(info.queueFamilies.sparseBindingQueue.value());
  }
  if (info.queueFamilies.hasProtected()) {
    uniqueQueueFamilies.insert(info.queueFamilies.protectedQueue.value());
  }

  // Create queue create infos
  std::vector<vk::DeviceQueueCreateInfo> queueCreateInfos;
  float queuePriority = 1.0F;
  for (uint32_t family : uniqueQueueFamilies) {
    vk::DeviceQueueCreateInfo queueCreateInfo{{}, family, 1, &queuePriority};
    queueCreateInfos.push_back(queueCreateInfo);
  }

  // Get device features
  // TODO see to add optional features
  // TODO move this setup to a separete function for easier management

  auto availableFeatures = physicalDevice_->getFeatures2<
      vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan11Features,
      vk::PhysicalDeviceVulkan12Features, vk::PhysicalDeviceVulkan13Features,
      vk::PhysicalDeviceVulkan14Features>(); // TODO update to get from
  // available features

  auto &baseFeatures =
      availableFeatures.get<vk::PhysicalDeviceFeatures2>().features;

  vk::PhysicalDeviceFeatures deviceFeatures{};
  deviceFeatures.setSamplerAnisotropy(baseFeatures.samplerAnisotropy);
  deviceFeatures.setFillModeNonSolid(baseFeatures.fillModeNonSolid);
  deviceFeatures.setWideLines(baseFeatures.wideLines);
  deviceFeatures.setGeometryShader(baseFeatures.geometryShader);
  deviceFeatures.setTessellationShader(baseFeatures.tessellationShader);
  deviceFeatures.setShaderInt16(baseFeatures.shaderInt16);
  deviceFeatures.setShaderInt64(baseFeatures.shaderInt64);
  deviceFeatures.setShaderFloat64(baseFeatures.shaderFloat64);
  deviceFeatures.setVertexPipelineStoresAndAtomics(
      baseFeatures.vertexPipelineStoresAndAtomics);

  vk::PhysicalDeviceVulkan11Features deviceFeatures11{};
  deviceFeatures11.sType = vk::StructureType::ePhysicalDeviceVulkan11Features;
  deviceFeatures11.setShaderDrawParameters(vk::True);

  vk::PhysicalDeviceVulkan12Features deviceFeatures12{};
  deviceFeatures12.setPNext(&deviceFeatures11);

  deviceFeatures12.setDescriptorBindingPartiallyBound(vk::True);
  deviceFeatures12.setDescriptorBindingVariableDescriptorCount(vk::True);
  deviceFeatures12.setDescriptorBindingSampledImageUpdateAfterBind(vk::True);
  deviceFeatures12.setDescriptorBindingStorageBufferUpdateAfterBind(vk::True);
  deviceFeatures12.setDescriptorBindingUniformBufferUpdateAfterBind(vk::True);
  deviceFeatures12.setDescriptorBindingStorageImageUpdateAfterBind(vk::True);
  deviceFeatures12.setDescriptorBindingUpdateUnusedWhilePending(vk::True);
  deviceFeatures12.setDescriptorBindingStorageTexelBufferUpdateAfterBind(
      vk::True);
  deviceFeatures12.setDescriptorBindingUniformTexelBufferUpdateAfterBind(
      vk::True);

  deviceFeatures12.setDescriptorIndexing(vk::True);
  deviceFeatures12.setShaderSampledImageArrayNonUniformIndexing(vk::True);
  deviceFeatures12.setShaderStorageBufferArrayNonUniformIndexing(vk::True);
  deviceFeatures12.setRuntimeDescriptorArray(vk::True);
  deviceFeatures12.setTimelineSemaphore(vk::True);
  deviceFeatures12.setBufferDeviceAddress(vk::True);
  deviceFeatures12.setScalarBlockLayout(vk::True);
  deviceFeatures12.setUniformBufferStandardLayout(vk::True);
  deviceFeatures12.setVulkanMemoryModelDeviceScope(vk::True);
  deviceFeatures12.setVulkanMemoryModel(vk::True);
  deviceFeatures12.setShaderFloat16(vk::True);
  deviceFeatures12.setShaderInt8(vk::True);

  vk::PhysicalDeviceVulkan13Features deviceFeatures13{};
  deviceFeatures13.setPNext(&deviceFeatures12);
  deviceFeatures13.setDynamicRendering(vk::True);
  deviceFeatures13.setSynchronization2(vk::True);
  deviceFeatures13.setMaintenance4(vk::True);
  deviceFeatures13.setInlineUniformBlock(vk::True);
  deviceFeatures13.setSubgroupSizeControl(vk::True);
  deviceFeatures13.setComputeFullSubgroups(vk::True);
  deviceFeatures13.setShaderDemoteToHelperInvocation(vk::True);
  deviceFeatures13.setShaderIntegerDotProduct(vk::True);
  deviceFeatures13.setShaderZeroInitializeWorkgroupMemory(vk::True);
  deviceFeatures13.setShaderTerminateInvocation(vk::True);
  deviceFeatures13.setRobustImageAccess(vk::True);

  vk::PhysicalDeviceVulkan14Features deviceFeatures14{};
  deviceFeatures14.setPNext(&deviceFeatures13);
  deviceFeatures14.setPushDescriptor(vk::True);
  deviceFeatures14.setDynamicRenderingLocalRead(vk::True);
  deviceFeatures14.setHostImageCopy(vk::True);
  deviceFeatures14.setMaintenance6(vk::True);
  deviceFeatures14.setShaderFloatControls2(vk::True);

  // Prepare extensions
  auto extensionNames = getAvailableExtensions();
  std::vector<const char *> enabledExtensions =
      extensionNames | std::views::transform([](const std::string &ext) {
        return ext.c_str();
      }) |
      std::ranges::to<std::vector<const char *>>();

  // Create logical device
  vk::DeviceCreateInfo createInfo{{},
                                  queueCreateInfos,
                                  {}, // No layers for device
                                  enabledExtensions,
                                  &deviceFeatures,
                                  &deviceFeatures14};

  device_ = std::make_shared<vk::raii::Device>(*physicalDevice_, createInfo);

  // Get queues (raw handles)
  if (info.queueFamilies.hasGraphics()) {
    graphicsQueue_ =
        device_->getQueue(info.queueFamilies.graphicsQueue.value(), 0);
  }
  if (info.queueFamilies.hasCompute()) {
    computeQueue_ =
        device_->getQueue(info.queueFamilies.computeQueue.value(), 0);
  }
  if (info.queueFamilies.hasTransfer()) {
    transferQueue_ =
        device_->getQueue(info.queueFamilies.transferQueue.value(), 0);
  }
  if (info.queueFamilies.hasSparseBinding()) {
    sparseBidingQueue_ =
        device_->getQueue(info.queueFamilies.sparseBindingQueue.value(), 0);
  }
  if (info.queueFamilies.hasProtected()) {
    protectedQueue_ =
        device_->getQueue(info.queueFamilies.protectedQueue.value(), 0);
  }

  vma::AllocatorCreateInfo allocatorInfo{};
  allocatorInfo.vulkanApiVersion = instances::Config::minApiVersion;
  allocatorInfo.instance = nullptr; // must be null
  allocatorInfo.physicalDevice = *physicalDevice_;
  allocatorInfo.device = nullptr; // must be null

  // Use dynamic function dispatch
  allocatorInfo.flags = vma::AllocatorCreateFlagBits::eExtMemoryBudget |
                        vma::AllocatorCreateFlagBits::eExternallySynchronized;
  allocatorInfo.pVulkanFunctions = nullptr; // must be null

  allocator_ = std::make_unique<vma::raii::Allocator>(
      vma::raii::createAllocator(instance, *device_, allocatorInfo));

  // Initialize VMA frame index before any allocations
  vmaSetCurrentFrameIndex(**allocator_, 0);

  std::println("[Device] Initialized: {}", info.name);
}

Device::~Device() {

  windows_.clear();

  std::erase_if(touchedDevices, [this](const TouchedDevice &entry) {
    return entry.device == this;
  });

  graphicsPools_.clear();
  computePools_.clear();
  transferPools_.clear();
  sparseBidingPools_.clear();
  protectedPools_.clear();
  touchedThreads_.clear();
  gpuPool_.reset();
  aliveToken_.reset();

  allocator_.reset();
  device_.reset();
  physicalDevice_.reset();

  std::cout << "[Device] Shutdown: " << info_.name << '\n';
}

std::vector<std::string> Device::getAvailableExtensions() {
  auto available = physicalDevice_->enumerateDeviceExtensionProperties();
  std::unordered_set<std::string> available_set;
  std::ranges::for_each(available,
                        [&available_set](const vk::ExtensionProperties &ext) {
                          available_set.emplace(ext.extensionName);
                        });

  auto snapshot = instances::Config::instance().snapshot();
  const auto &required = snapshot.deviceExtensions;
  const auto &optional = snapshot.optionalDeviceExtensions;

  std::vector<std::string> suported;
  std::vector<std::string> unsuported;

  std::ranges::partition_copy(
      required.begin(), required.end(), std::back_inserter(suported),
      std::back_inserter(unsuported), [&available_set](const std::string &ext) {
        return available_set.contains(ext);
      });

  if (!unsuported.empty()) {
    std::cerr << std::format("[Device] Unsupported extensions:\n");
    std::ranges::for_each(
        unsuported.begin(), unsuported.end(),
        [](const auto &ext) { std::cerr << std::format("  - {}\n", ext); });
    throw std::runtime_error(
        std::format("Some extensions are unsupported on GPU {}", info_.name));
  }

  std::ranges::partition_copy(
      optional.begin(), optional.end(), std::back_inserter(suported),
      std::back_inserter(unsuported), [&available_set](const std::string &ext) {
        return available_set.contains(ext);
      });

  if (!unsuported.empty()) {
    std::cerr << std::format("[Device] Unsupported optional extensions:\n");
    std::ranges::for_each(
        unsuported.begin(), unsuported.end(),
        [](const auto &ext) { std::cerr << std::format("  - {}\n", ext); });
  }

  return suported;
}

void Device::createWindow(const std::shared_ptr<WindowInfo> &windowInfo,
                          uint32_t framesInFlight,
                          Swapchain::SwapchainInfo &swapInfo) {
  if (!windowInfo->surface) {
    throw std::logic_error("[Device] Surface not created");
    return;
  }

  std::optional<uint32_t> presentFamily;
  vk::Queue present;

  {
    std::unique_lock lock(deviceMtx_);

    presentFamily =
        findPresentQueue(windowInfo->surface.get(), physicalDevice_.get());
    if (!presentFamily
             .has_value()) { // TODO maybe change return to deal with errors
      throw std::runtime_error(
          "[Device] Didn't find a present queue for surface");
    }

    present = device_->getQueue(presentFamily.value(), 0);
  }

  std::unique_lock lock(windowMtx_);
  windowInfo->swapchain = std::make_unique<Swapchain>(
      this, physicalDevice_, device_, windowInfo, swapInfo, framesInFlight,
      present, presentFamily.value());

  windows_.push_back(windowInfo);
}

void Device::createWindow(const std::shared_ptr<WindowInfo> &windowInfo,
                          uint32_t framesInFlight) {
  if (!windowInfo->surface) {
    throw std::logic_error("[Device] Surface not created");
    return;
  }

  std::optional<uint32_t> presentFamily;
  vk::Queue present;

  {
    std::unique_lock lock(deviceMtx_);

    presentFamily =
        findPresentQueue(windowInfo->surface.get(), physicalDevice_.get());
    if (!presentFamily
             .has_value()) { // TODO maybe change return to deal with errors
      throw std::runtime_error(
          "[Device] Didn't find a present queue for surface");
    }

    present = device_->getQueue(presentFamily.value(), 0);
  }

  std::unique_lock lock(windowMtx_);

  Swapchain::SwapchainInfo swapInfo;

  windowInfo->swapchain = std::make_unique<Swapchain>(
      this, physicalDevice_, device_, windowInfo, swapInfo, framesInFlight,
      present, presentFamily.value());

  windows_.push_back(windowInfo);
}

bool Device::removeWindow(const std::shared_ptr<WindowInfo> &info) {
  std::unique_lock lock(windowMtx_);

  auto it = std::ranges::find(windows_, info);

  if (it == windows_.end()) {
    return false;
  }

  windows_.erase(it);
  return true;
}

AllocatedBuffer Device::createBuffer(const BufferCreateInfo &info) {
  std::lock_guard lock(deviceMtx_);

  AllocatedBuffer result{};
  result.size = info.size;
  result.name = info.debugName;

  vk::BufferCreateInfo bufferInfo{
      {}, info.size, info.usage, vk::SharingMode::eExclusive};

  vma::MemoryUsage resolvedMemUsage = info.memoryUsage;
  vma::AllocationCreateFlags resolvedFlags = info.flags;

  if (!info.flags && info.memoryUsage == vma::MemoryUsage::eAuto) {
    switch (info.access) {
    case BufferCreateInfo::Access::stagingUpload:
      resolvedFlags = vma::AllocationCreateFlagBits::eHostAccessSequentialWrite;
      break;
    case BufferCreateInfo::Access::stagingReadback:
      resolvedFlags = vma::AllocationCreateFlagBits::eHostAccessRandom;
      break;
    case BufferCreateInfo::Access::persistentMapping:
      resolvedFlags =
          vma::AllocationCreateFlagBits::eHostAccessSequentialWrite |
          vma::AllocationCreateFlagBits::eMapped;
      break;
    case BufferCreateInfo::Access::gpuOnly:
    default:
      break;
    }
  }

  vma::AllocationCreateInfo allocInfo{};
  allocInfo.usage = resolvedMemUsage;
  allocInfo.flags = resolvedFlags;

  result.buffer =
      std::make_unique<vma::raii::Buffer>(*allocator_, bufferInfo, allocInfo);

  return result;
}

AllocatedImage Device::createImage(const ImageCreateInfo &info) {
  std::lock_guard lock(deviceMtx_);

  AllocatedImage result{};
  result.format = info.format;
  result.extent = info.extent;
  result.mipLevels = info.mipLevels;
  result.arrayLayers = info.arrayLayers;
  result.currentLayout = info.layout;
  result.name = info.debugName;

  vk::ImageCreateInfo imageInfo{{},
                                info.imageType,
                                info.format,
                                info.extent,
                                info.mipLevels,
                                info.arrayLayers,
                                info.samples,
                                info.tiling,
                                info.usage,
                                vk::SharingMode::eExclusive};

  vma::AllocationCreateInfo allocInfo{};
  allocInfo.usage = info.memoryUsage;
  allocInfo.flags = info.flags;

  result.image =
      std::make_unique<vma::raii::Image>(*allocator_, imageInfo, allocInfo);

  // --- Create image view if requested ---
  if (info.createImageView) {
    vk::ImageViewCreateInfo viewInfo{};
    viewInfo.image = result.getImage();
    viewInfo.viewType = info.viewType;
    viewInfo.format = info.format;
    viewInfo.components = info.components;
    viewInfo.subresourceRange = info.subresourceRange;
    result.view = std::make_unique<vk::raii::ImageView>(*device_, viewInfo);
  }

  return result;
}

CommandBufferPool &Device::getGraphicsPool() {
  markThreadTouched();
  std::thread::id tid = std::this_thread::get_id();
  {
    std::shared_lock lock(poolsMtx_);
    auto it = graphicsPools_.find(tid);
    if (it != graphicsPools_.end()) {
      return *it->second;
    }
  }

  std::unique_lock lock(poolsMtx_);

  const CommandPoolCreateInfo createInfo{
      .queueFamily = info_.queueFamilies.graphicsQueue.value()};
  auto pool = std::make_unique<CommandBufferPool>(device_, createInfo);
  auto &&[it, inserted] = graphicsPools_.emplace(tid, std::move(pool));
  return *it->second;
}

CommandBufferPool &Device::getComputePool() {
  markThreadTouched();
  std::thread::id tid = std::this_thread::get_id();
  {
    std::shared_lock lock(poolsMtx_);
    auto it = computePools_.find(tid);
    if (it != computePools_.end()) {
      return *it->second;
    }
  }

  std::unique_lock lock(poolsMtx_);

  const CommandPoolCreateInfo createInfo{
      .queueFamily = info_.queueFamilies.computeQueue.value()};
  auto pool = std::make_unique<CommandBufferPool>(device_, createInfo);
  auto &&[it, inserted] = computePools_.emplace(tid, std::move(pool));
  return *it->second;
}

CommandBufferPool &Device::getTransferPool() {
  markThreadTouched();
  std::thread::id tid = std::this_thread::get_id();
  {
    std::shared_lock lock(poolsMtx_);
    auto it = transferPools_.find(tid);
    if (it != transferPools_.end()) {
      return *it->second;
    }
  }

  std::unique_lock lock(poolsMtx_);

  const CommandPoolCreateInfo createInfo{
      .queueFamily = info_.queueFamilies.transferQueue.value()};
  auto pool = std::make_unique<CommandBufferPool>(device_, createInfo);
  auto &&[it, inserted] = transferPools_.emplace(tid, std::move(pool));
  return *it->second;
}

CommandBufferPool &Device::getSparseBidingPool() {
  markThreadTouched();
  std::thread::id tid = std::this_thread::get_id();
  {
    std::shared_lock lock(poolsMtx_);
    auto it = sparseBidingPools_.find(tid);
    if (it != sparseBidingPools_.end()) {
      return *it->second;
    }
  }

  std::unique_lock lock(poolsMtx_);

  const CommandPoolCreateInfo createInfo{
      .queueFamily = info_.queueFamilies.sparseBindingQueue.value()};
  auto pool = std::make_unique<CommandBufferPool>(device_, createInfo);
  auto &&[it, inserted] = sparseBidingPools_.emplace(tid, std::move(pool));
  return *it->second;
}

CommandBufferPool &Device::getProtectedPool() {
  markThreadTouched();
  std::thread::id tid = std::this_thread::get_id();
  {
    std::shared_lock lock(poolsMtx_);
    auto it = protectedPools_.find(tid);
    if (it != protectedPools_.end()) {
      return *it->second;
    }
  }

  std::unique_lock lock(poolsMtx_);

  const CommandPoolCreateInfo createInfo{
      .queueFamily = info_.queueFamilies.protectedQueue.value()};
  auto pool = std::make_unique<CommandBufferPool>(device_, createInfo);
  auto &&[it, inserted] = protectedPools_.emplace(tid, std::move(pool));
  return *it->second;
}

void Device::markThreadTouched() {
  const auto tid = std::this_thread::get_id();
  std::unique_lock lock(poolsMtx_);
  auto [_, inserted] = touchedThreads_.insert(tid);
  if (inserted) {
    touchedDevices.emplace_back(this, aliveToken_);
  }
}

void Device::removeThreadPools(std::thread::id tid) {
  std::unique_lock lock(poolsMtx_);
  graphicsPools_.erase(tid);
  computePools_.erase(tid);
  transferPools_.erase(tid);
  sparseBidingPools_.erase(tid);
  protectedPools_.erase(tid);
  touchedThreads_.erase(tid);
}

} // namespace graphics::vulkan::devices
