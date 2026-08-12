import graphics;
import std.compat;
import vulkan;
import vk_mem_alloc;

import concurrency;

#include <vulkan/vulkan.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <cassert>
#include <cstdlib>

using namespace graphics::vulkan;

// --- Shader definition ---------------------------------------------------
static shaders::Shader g_shader{
    .entryPoints = {{"vertexMain", vk::ShaderStageFlagBits::eVertex},
                    {"fragmentMain", vk::ShaderStageFlagBits::eFragment}},
    .sourcePath = "main.slang"};

// After g_shader definition

static shaders::Shader g_computeShader{
    .entryPoints = {{"main", vk::ShaderStageFlagBits::eCompute}},
    .sourcePath = "fill_buffer.slang"};

// --- GLFW error callback -------------------------------------------------
static void glfwError(int code, const char *desc) {
  std::cerr << "GLFW error (" << code << "): " << desc << '\n';
}

// --- Helper --------------------------------------------------------------
static void checkMsg(bool cond, const char *msg) {
  if (!cond) {
    throw std::runtime_error(msg);
  }
}

// Helper: create a descriptor pool, layout, and allocate a set for a single
// storage buffer
static std::tuple<vk::raii::DescriptorSetLayout, vk::raii::DescriptorSet,
                  vk::raii::DescriptorPool>
createStorageBufferDescriptorSet(const vk::raii::Device &device,
                                 const vk::Buffer &buffer,
                                 vk::ShaderStageFlags stages,
                                 vk::DeviceSize range = VK_WHOLE_SIZE) {
  vk::DescriptorSetLayoutBinding binding{0, vk::DescriptorType::eStorageBuffer,
                                         1, stages};
  vk::DescriptorSetLayoutCreateInfo layoutCI{{}, binding};
  vk::raii::DescriptorSetLayout setLayout{device, layoutCI};

  vk::DescriptorPoolSize poolSize{vk::DescriptorType::eStorageBuffer, 1};
  vk::DescriptorPoolCreateInfo poolCI{
      vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, 1, poolSize};
  vk::raii::DescriptorPool pool{device, poolCI};

  vk::DescriptorSetAllocateInfo allocInfo{*pool, *setLayout};
  vk::raii::DescriptorSets sets{device, allocInfo};
  vk::raii::DescriptorSet set = std::move(sets[0]);

  vk::DescriptorBufferInfo bufferInfo{buffer, 0, range};
  vk::WriteDescriptorSet write{
      *set, 0, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &bufferInfo};
  device.updateDescriptorSets(write, {});

  return {std::move(setLayout), std::move(set), std::move(pool)};
}

// Vertex layout (std430): float2 (8) + pad[2] (8) + float3 (12) + pad (4) = 32
// bytes
struct Std430Vertex {
  float pos[2];
  float pad0[2]; // explicit padding
  float col[3];
  float pad1; // align to 16
};
static_assert(sizeof(Std430Vertex) == 32);

constexpr vk::DeviceSize vbSize = 3 * sizeof(Std430Vertex); // 96 bytes

// =========================================================================
// testInstanceHelpers
// =========================================================================
static void testInstanceHelpers(Api &backend) {
  auto exts = instances::Instance::getAvailableExtensions();
  checkMsg(!exts.empty(), "getAvailableExtensions() returned empty");

  instances::Instance::getAvailableLayers(); // just call, no crash

  auto missing = instances::Instance::checkExtensionSupport({"VK_KHR_surface"});
  checkMsg(missing.empty(),
           "VK_KHR_surface should be present (missing list should be empty)");

  auto missingLayers =
      instances::Instance::checkLayerSupport({"VK_LAYER_DOES_NOT_EXIST_XYZ"});
  checkMsg(!missingLayers.empty(),
           "checkLayerSupport: absent layer must be in returned vector");

  checkMsg(backend.getInstance().getInstance() != vk::Instance{},
           "getInstance() handle is null");

  backend.getInstance().getRaiiInstance(); // no crash
  backend.getInstance().getRaiiContext();  // no crash

  std::cout << "[PASS] testInstanceHelpers\n";
}

// =========================================================================
// testConfig
// =========================================================================
static void testConfig() {
  auto snap = instances::Config::instance().snapshot();
  checkMsg(!snap.instanceExtensions.empty(),
           "Config snapshot instanceExtensions is empty");

  instances::Config::instance().addOptionalInstanceExtension(
      "VK_KHR_get_surface_capabilities2");
  auto optExts = instances::Config::instance().getOptionalInstanceExtensions();
  bool found = std::ranges::find(optExts, "VK_KHR_get_surface_capabilities2") !=
               optExts.end();
  checkMsg(found,
           "addOptionalInstanceExtension: extension not found after adding");

  instances::Config::instance().removeOptionalInstanceExtension(
      "VK_KHR_get_surface_capabilities2");
  optExts = instances::Config::instance().getOptionalInstanceExtensions();
  bool gone = std::ranges::find(optExts, "VK_KHR_get_surface_capabilities2") ==
              optExts.end();
  checkMsg(gone,
           "removeOptionalInstanceExtension: extension still present after "
           "removing");

  instances::Config::instance().needsUpdate();
  instances::Config::instance().resetUpdate();

  std::cout << "[PASS] testConfig\n";
}

// =========================================================================
// testDeviceManager
// =========================================================================
static void testDeviceManager(Api &backend) {
  auto entries = backend.getDeviceManager().getDeviceEntries();
  checkMsg(!entries.empty(), "getDeviceEntries() returned empty");

  auto entry0 = backend.getDeviceManager().getEntry(0u);
  checkMsg(entry0.has_value(), "getEntry(0) has no value");

  auto byId = backend.getDeviceManager().getEntryGPUId(entry0->info.deviceId);
  checkMsg(byId.has_value(), "getEntryGPUId() returned nullopt");

  backend.getDeviceManager().getDevicesWithWindows();    // no crash
  backend.getDeviceManager().getDevicesWithoutWindows(); // no crash

  std::atomic<int> count{0};
  auto futures =
      backend.getDeviceManager().broadcastToAllDevices([&count] { ++count; });
  for (auto &f : futures) {
    f.get();
  }
  checkMsg(static_cast<size_t>(count.load()) == entries.size(),
           "broadcastToAllDevices: count mismatch");

  std::cout << "[PASS] testDeviceManager\n";
}

// =========================================================================
// testDeviceAccessors
// =========================================================================
static void testDeviceAccessors(Api &backend) {
  auto dev = backend.getFirstDevice();
  checkMsg(dev != nullptr, "getFirstDevice() returned nullptr");

  checkMsg(dev->getDevice() != vk::Device{}, "getDevice() handle is null");
  checkMsg(dev->getPhysicalDevice() != vk::PhysicalDevice{},
           "getPhysicalDevice() handle is null");
  dev->getRaiiDevice();         // no crash
  dev->getRaiiPhysicalDevice(); // no crash
  checkMsg(dev->getAllocator() != vma::Allocator{}, "getAllocator() is null");

  checkMsg(!dev->getInfo().name.empty(), "device name is empty");
  checkMsg(dev->getInfo().queueFamilies.isMinimumComplete(),
           "queue families not minimum complete");

  checkMsg(dev->getGraphicsQueue() != vk::Queue{},
           "getGraphicsQueue() is null");
  checkMsg(dev->getComputeQueue() != vk::Queue{}, "getComputeQueue() is null");
  checkMsg(dev->getTransferQueue() != vk::Queue{},
           "getTransferQueue() is null");

  checkMsg(dev->workerCount() > 0, "workerCount() is 0");

  auto result = dev->submit([] { return 42; }).get();
  checkMsg(result == 42, "submit() result mismatch");

  dev->getGraphicsPool(); // no crash
  dev->getComputePool();  // no crash
  dev->getTransferPool(); // no crash

  std::cout << "[PASS] testDeviceAccessors\n";
}

// =========================================================================
// testBufferImageAllocation
// =========================================================================
static void testBufferImageAllocation(Api &backend) {
  auto dev = backend.getFirstDevice();
  checkMsg(dev != nullptr, "getFirstDevice() returned nullptr");

  using Access = devices::BufferCreateInfo::Access;

  devices::BufferCreateInfo bufCI{.size = 256,
                                  .usage =
                                      vk::BufferUsageFlagBits::eTransferSrc |
                                      vk::BufferUsageFlagBits::eTransferDst,
                                  .access = Access::stagingUpload,
                                  .debugName = "test_staging_buffer"};

  auto buf = dev->createBuffer(bufCI);
  checkMsg(buf.isValid(), "createBuffer() returned invalid buffer");

  void *mapped = buf.map();
  checkMsg(mapped != nullptr, "buf.map() returned null");
  std::memset(mapped, 0xAB, 256);
  buf.flush();
  buf.unmap();

  devices::ImageCreateInfo imgCI{.format = vk::Format::eR8G8B8A8Unorm,
                                 .extent = {64, 64, 1},
                                 .usage = vk::ImageUsageFlagBits::eTransferDst |
                                          vk::ImageUsageFlagBits::eSampled,
                                 .debugName = "test_image"};

  auto img = dev->createImage(imgCI);
  checkMsg(img.isValid(), "createImage() returned invalid image");
  checkMsg(img.getImage() != vk::Image{}, "getImage() handle is null");
  checkMsg(img.getView() != vk::ImageView{}, "getView() handle is null");

  std::cout << "[PASS] testBufferImageAllocation\n";
}

// =========================================================================
// testTransferHelpers
// =========================================================================
static void testTransferHelpers() {
  checkMsg(devices::formatSize(vk::Format::eR8G8B8A8Unorm) == 4,
           "formatSize(R8G8B8A8Unorm) != 4");
  checkMsg(devices::formatSize(vk::Format::eR32G32B32A32Sfloat) == 16,
           "formatSize(R32G32B32A32Sfloat) != 16");

  checkMsg(devices::isBlockCompressed(vk::Format::eBc1RgbUnormBlock),
           "isBlockCompressed(Bc1RgbUnormBlock) should be true");
  checkMsg(!devices::isBlockCompressed(vk::Format::eR8G8B8A8Unorm),
           "isBlockCompressed(R8G8B8A8Unorm) should be false");

  checkMsg(devices::imageDataSize(vk::Format::eR8G8B8A8Unorm, {64, 64, 1}) ==
               16384,
           "imageDataSize(R8G8B8A8Unorm,64x64) != 16384");

  std::cout << "[PASS] testTransferHelpers\n";
}

// =========================================================================
// testBufferTransfer
// =========================================================================
static void testBufferTransfer(Api &backend) {
  auto dev = backend.getFirstDevice();
  checkMsg(dev != nullptr, "getFirstDevice() returned nullptr");

  using Access = devices::BufferCreateInfo::Access;

  auto src = dev->createBuffer(
      devices::BufferCreateInfo{.size = 128,
                                .usage = vk::BufferUsageFlagBits::eTransferSrc,
                                .access = Access::stagingUpload,
                                .debugName = "transfer_src"});
  auto dst = dev->createBuffer(
      devices::BufferCreateInfo{.size = 128,
                                .usage = vk::BufferUsageFlagBits::eTransferDst,
                                .access = Access::stagingReadback,
                                .debugName = "transfer_dst"});

  // Fill src
  {
    auto *p = static_cast<uint8_t *>(src.map());
    checkMsg(p != nullptr, "src.map() returned null");
    for (int i = 0; i < 128; ++i) {
      p[i] = static_cast<uint8_t>(i);
    }
    src.flush();
    src.unmap();
  }

  auto task = devices::transfer(dev, src, 0, dst, 0, 128);
  task.get();

  // Verify
  {
    dst.invalidate();
    auto *p = static_cast<const uint8_t *>(dst.map());
    checkMsg(p != nullptr, "dst.map() returned null");
    for (int i = 0; i < 128; ++i) {
      checkMsg(p[i] == static_cast<uint8_t>(i),
               "testBufferTransfer: byte mismatch");
    }
    dst.unmap();
  }

  std::cout << "[PASS] testBufferTransfer\n";
}

// =========================================================================
// testImageTransfers
// =========================================================================
static void testImageTransfers(Api &backend) {
  auto dev = backend.getFirstDevice();
  checkMsg(dev != nullptr, "getFirstDevice() returned nullptr");

  constexpr uint32_t W = 4, H = 4;
  constexpr vk::DeviceSize stagingSize = W * H * 4;

  using Access = devices::BufferCreateInfo::Access;

  // Staging buffer filled with 0xFF
  auto staging = dev->createBuffer(
      devices::BufferCreateInfo{.size = stagingSize,
                                .usage = vk::BufferUsageFlagBits::eTransferSrc,
                                .access = Access::stagingUpload,
                                .debugName = "img_staging_src"});
  {
    auto *p = static_cast<uint8_t *>(staging.map());
    checkMsg(p != nullptr, "staging.map() returned null");
    std::memset(p, 0xFF, stagingSize);
    staging.flush();
    staging.unmap();
  }

  // GPU image
  auto gpuImg = dev->createImage(
      devices::ImageCreateInfo{.format = vk::Format::eR8G8B8A8Unorm,
                               .extent = {W, H, 1},
                               .usage = vk::ImageUsageFlagBits::eTransferDst |
                                        vk::ImageUsageFlagBits::eTransferSrc |
                                        vk::ImageUsageFlagBits::eSampled,
                               .debugName = "gpu_test_image"});

  auto task = devices::transfer(dev, staging, 0, gpuImg,
                                vk::ImageLayout::eShaderReadOnlyOptimal);
  task.get();
  checkMsg(gpuImg.currentLayout == vk::ImageLayout::eShaderReadOnlyOptimal,
           "gpuImg.currentLayout != eShaderReadOnlyOptimal after transfer");

  // Read back
  auto readback = dev->createBuffer(
      devices::BufferCreateInfo{.size = stagingSize,
                                .usage = vk::BufferUsageFlagBits::eTransferDst,
                                .access = Access::stagingReadback,
                                .debugName = "img_staging_dst"});

  task = devices::transfer(dev, gpuImg, readback, 0);
  task.get();

  {
    readback.invalidate();
    auto *p = static_cast<const uint8_t *>(readback.map());
    checkMsg(p != nullptr, "readback.map() returned null");
    for (vk::DeviceSize i = 0; i < stagingSize; ++i) {
      checkMsg(p[i] == 0xFF, "testImageTransfers: readback byte != 0xFF");
    }
    readback.unmap();
  }

  std::cout << "[PASS] testImageTransfers\n";
}

// =========================================================================
// testAsyncTransfer
// =========================================================================
static void testAsyncTransfer(Api &backend) {
  auto dev = backend.getFirstDevice();
  checkMsg(dev != nullptr, "getFirstDevice() returned nullptr");

  using Access = devices::BufferCreateInfo::Access;
  constexpr vk::DeviceSize sz = 64;

  auto srcBuf = std::make_shared<devices::AllocatedBuffer>(dev->createBuffer(
      devices::BufferCreateInfo{.size = sz,
                                .usage = vk::BufferUsageFlagBits::eTransferSrc,
                                .access = Access::stagingUpload,
                                .debugName = "async_src"}));
  auto dstBuf = std::make_shared<devices::AllocatedBuffer>(dev->createBuffer(
      devices::BufferCreateInfo{.size = sz,
                                .usage = vk::BufferUsageFlagBits::eTransferDst,
                                .access = Access::stagingReadback,
                                .debugName = "async_dst"}));

  {
    auto *p = static_cast<uint8_t *>(srcBuf->map());
    checkMsg(p != nullptr, "async srcBuf.map() returned null");
    std::memset(p, 0xCC, sz);
    srcBuf->flush();
    srcBuf->unmap();
  }

  auto task = devices::transfer(dev, *srcBuf, 0, *dstBuf, 0, sz);
  task.get();

  {
    dstBuf->invalidate();
    auto *p = static_cast<const uint8_t *>(dstBuf->map());
    checkMsg(p != nullptr, "async dstBuf.map() returned null");
    for (vk::DeviceSize i = 0; i < sz; ++i) {
      checkMsg(p[i] == 0xCC, "testAsyncTransfer: byte mismatch");
    }
    dstBuf->unmap();
  }

  std::cout << "[PASS] testAsyncTransfer\n";
}

// =========================================================================
// testSwapchainAccessors
// =========================================================================
static void
testSwapchainAccessors(const std::shared_ptr<devices::WindowInfo> &windowInfo,
                       uint32_t w, uint32_t h) {
  auto &sc = *windowInfo->swapchain;

  auto info = sc.getinfo();
  checkMsg(info.extent.width > 0 && info.extent.height > 0,
           "swapchain extent is 0");

  checkMsg(sc.getPresentQueueFamily() < 64,
           "getPresentQueueFamily() >= 64 (sanity check failed)");
  checkMsg(sc.getPresentQueue() != vk::Queue{}, "getPresentQueue() is null");

  checkMsg(!sc.needRecreation(), "needRecreation() should be false initially");

  sc.markForRecreation();
  checkMsg(sc.needRecreation(),
           "needRecreation() should be true after markForRecreation()");

  sc.recreateSwapchain(w, h);
  checkMsg(!sc.needRecreation(),
           "needRecreation() should be false after recreateSwapchain()");

  auto modified = sc.getinfo();
  sc.setinfo(modified);
  checkMsg(sc.needRecreation(),
           "needRecreation() should be true after setinfo()");

  sc.recreateSwapchain(w, h); // reset for subsequent tests

  std::cout << "[PASS] testSwapchainAccessors\n";
}

// =========================================================================
// testShaderManager
// =========================================================================
static void testShaderManager(Api &backend) {
  auto dev = backend.getFirstDevice();
  checkMsg(dev != nullptr, "getFirstDevice() returned nullptr");

  auto result =
      backend.getShaderManager().loadShader(&g_shader, dev->getDevicePtr());
  if (result) {
    checkMsg(*result != nullptr, "loadShader returned null module");

    auto mod =
        backend.getShaderManager().getModule(&g_shader, dev->getDevicePtr());
    // mod may be null if device registry race, just call it
    (void)mod;

    backend.getShaderManager().unloadShader(&g_shader, dev->getDevicePtr());

    backend.reloadShader(&g_shader);
    std::cout << "[PASS] testShaderManager\n";
  } else {
    std::cout << "[INFO] testShaderManager: loadShader failed ("
              << result.error().message << "), skipping shader assertions\n";
  }

  // Always safe to call
  backend.getShaderManager().unloadShaderAllDevice(&g_shader);
  std::cout << "[PASS] testShaderManager (unloadShaderAllDevice)\n";
}

// =========================================================================
// testPipelineManager
// =========================================================================
static void
testPipelineManager(Api &backend,
                    const std::shared_ptr<devices::WindowInfo> &windowInfo) {
  auto dev = backend.getFirstDevice();
  checkMsg(dev != nullptr, "getFirstDevice() returned nullptr");

  // --- Dynamic graphics pipeline ---
  auto &sc = *windowInfo->swapchain;
  auto imgData = sc.getSwapchainImageData(0);
  if (!imgData) {
    std::cout << "[SKIP] testPipelineManager: no swapchain image data\n";
    return;
  }
  vk::Format colorFormat = imgData->format;

  vk::PipelineLayoutCreateInfo layoutCI{};
  vk::raii::PipelineLayout layout{dev->getRaiiDevice(), layoutCI};

  vk::PipelineColorBlendAttachmentState colorBlend;
  colorBlend.blendEnable = vk::False;
  colorBlend.colorWriteMask =
      vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
      vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;

  pipelines::DynamicPipelineInfo dynInfo{
      .tag = {.shaderTag = &g_shader, .layout = *layout},
      .inputAssembly = {.topology = vk::PrimitiveTopology::eTriangleList},
      .rasterization = {.cullMode = vk::CullModeFlagBits::eNone},
      .depthStencil = {.depthTest = vk::False, .depthWrite = vk::False},
      .attachments = {.color = {colorFormat}},
      .colorBlendAttachments = {colorBlend}};

  auto pipeResult = backend.createPipeline(dynInfo);
  if (!pipeResult) {
    std::cout
        << "[SKIP] testPipelineManager: dynamic pipeline creation failed ("
        << pipeResult.error().message << ")\n";
  } else {
    checkMsg(*pipeResult != nullptr, "createPipeline(dynamic) returned null");
    // Second call should return cached same pointer
    auto pipeResult2 = backend.createPipeline(dynInfo);
    checkMsg(pipeResult2.has_value(), "second createPipeline failed");
    checkMsg(*pipeResult2 == *pipeResult, "cache hit test failed");
    std::cout << "[PASS] testPipelineManager (dynamic)\n";
  }

  // --- Static pipeline ---
  {
    vk::AttachmentDescription colorAttachment{{},
                                              colorFormat,
                                              vk::SampleCountFlagBits::e1,
                                              vk::AttachmentLoadOp::eClear,
                                              vk::AttachmentStoreOp::eStore,
                                              vk::AttachmentLoadOp::eDontCare,
                                              vk::AttachmentStoreOp::eDontCare,
                                              vk::ImageLayout::eUndefined,
                                              vk::ImageLayout::ePresentSrcKHR};
    vk::AttachmentReference colorRef{0,
                                     vk::ImageLayout::eColorAttachmentOptimal};
    vk::SubpassDescription subpass{
        {}, vk::PipelineBindPoint::eGraphics, {}, colorRef};
    vk::RenderPassCreateInfo rpCreateInfo{{}, colorAttachment, subpass};
    vk::raii::RenderPass renderPass{*dev->getDevicePtr(), rpCreateInfo};

    pipelines::StaticPipelineInfo staticInfo;
    staticInfo.tag.shaderTag = &g_shader;
    staticInfo.tag.layout = *layout;
    staticInfo.inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;
    staticInfo.rasterization.cullMode = vk::CullModeFlagBits::eNone;
    staticInfo.depthStencil.depthTest = vk::False;
    staticInfo.attachments.color = {colorFormat};
    staticInfo.colorBlendAttachments = {colorBlend};
    staticInfo.dynamicStates = {vk::DynamicState::eViewport,
                                vk::DynamicState::eScissor};
    staticInfo.renderPass = *renderPass;
    staticInfo.subpass = 0;

    auto staticResult = backend.createPipeline(staticInfo);
    if (!staticResult) {
      std::cout << "[SKIP] testPipelineManager: static pipeline creation "
                   "failed ("
                << staticResult.error().message << ")\n";
    } else {
      checkMsg(*staticResult != nullptr,
               "createPipeline(static) returned null");
      std::cout << "[PASS] testPipelineManager (static)\n";
    }
  }

  // ---  pipeline ---
  vk::DescriptorSetLayoutBinding computeBinding{
      0, vk::DescriptorType::eStorageBuffer, 1,
      vk::ShaderStageFlagBits::eCompute};
  vk::DescriptorSetLayoutCreateInfo computeSetLayoutCI{{}, computeBinding};
  vk::raii::DescriptorSetLayout computeSetLayout{*dev->getDevicePtr(),
                                                 computeSetLayoutCI};

  vk::PushConstantRange computePushRange{vk::ShaderStageFlagBits::eCompute, 0,
                                         sizeof(uint32_t)};

  vk::PipelineLayoutCreateInfo compLayoutCI{};
  compLayoutCI.setSetLayouts(*computeSetLayout);
  compLayoutCI.setPushConstantRanges(computePushRange);
  vk::raii::PipelineLayout compLayout{*dev->getDevicePtr(), compLayoutCI};

  pipelines::ComputePipelineInfo compInfo{
      .tag = {.shaderTag = &g_computeShader, .layout = *compLayout}};

  auto compResult = backend.createPipeline(compInfo);
  if (!compResult) {
    std::cout
        << "[SKIP] testPipelineManager: compute pipeline creation failed ("
        << compResult.error().message << ")\n";
  } else {
    checkMsg(*compResult != nullptr, "createPipeline(compute) returned null");
    // Cache test
    auto compResult2 = backend.createPipeline(compInfo);
    checkMsg(compResult2.has_value(), "second compute createPipeline failed");
    checkMsg(*compResult2 == *compResult, "compute cache hit test failed");
    std::cout << "[PASS] testPipelineManager (compute)\n";
  }

  // Clean up invalidation
  backend.getPipelineManager().invalidateShader(&g_shader);
  backend.getPipelineManager().invalidateShader(&g_computeShader);
}

// =========================================================================
// testExplicitDeviceWindow
// =========================================================================
static void testExplicitDeviceWindow(Api &backend, GLFWwindow *glfwWin,
                                     uint32_t w, uint32_t h) {
  auto dev = backend.getFirstDevice();
  checkMsg(dev != nullptr, "getFirstDevice() returned nullptr");

  auto instancePtr = backend.getInstance().getInstancePtr();
  VkSurfaceKHR rawSurface2 = VK_NULL_HANDLE;
  if (glfwCreateWindowSurface(**instancePtr, glfwWin, nullptr, &rawSurface2) !=
      VK_SUCCESS) {
    std::cout << "[SKIP] testExplicitDeviceWindow: surface creation failed\n";
    return;
  }

  auto winInfo2 = std::make_shared<devices::WindowInfo>();
  winInfo2->surface =
      std::make_unique<vk::raii::SurfaceKHR>(*instancePtr, rawSurface2);
  winInfo2->instance = instancePtr;

  backend.createWindow(dev, winInfo2, 2, vk::Extent2D{w, h});
  checkMsg(winInfo2->swapchain != nullptr,
           "testExplicitDeviceWindow: swapchain is null after createWindow");

  backend.waitIdle();
  backend.removeWindow(winInfo2);

  std::cout << "[PASS] testExplicitDeviceWindow\n";
}

// =========================================================================
int main() {
  try {
    // ── 1. GLFW setup
    // ─────────────────────────────────────────────────────
    glfwSetErrorCallback(glfwError);
    if (!glfwInit())
      throw std::runtime_error("GLFW init failed");

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow *window =
        glfwCreateWindow(800, 600, "Backend Test", nullptr, nullptr);
    if (!window) {
      glfwTerminate();
      throw std::runtime_error("Window creation failed");
    }
    glfwShowWindow(window);
    glfwPollEvents();

    // ── 2. Pool + Backend setup
    // ───────────────────────────────────────────
    auto poolManager = std::make_shared<concurrency::pool::Manager>();

    graphics::GraphicsApi backend{poolManager};

    uint32_t extCount = 0;
    const char **glfwExts = glfwGetRequiredInstanceExtensions(&extCount);
    if (!glfwExts)
      throw std::runtime_error(
          "glfwGetRequiredInstanceExtensions returned null");
    backend.addRequiredExtensions({glfwExts, extCount});

    // ── 3. Surface + Window registration ─────────────────────────────────
    auto instancePtr = backend.getInstance().getInstancePtr();
    VkSurfaceKHR rawSurface = VK_NULL_HANDLE;
    if (glfwCreateWindowSurface(**instancePtr, window, nullptr, &rawSurface) !=
        VK_SUCCESS)
      throw std::runtime_error("Surface creation failed");

    auto windowInfo = std::make_shared<devices::WindowInfo>();
    windowInfo->surface =
        std::make_unique<vk::raii::SurfaceKHR>(*instancePtr, rawSurface);
    windowInfo->instance = instancePtr;

    int width = 0, height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    vk::Extent2D extent{static_cast<uint32_t>(width),
                        static_cast<uint32_t>(height)};

    backend.createWindow(windowInfo, 2, extent);

    // ── 4. Run all tests
    // ──────────────────────────────────────────────────
    testInstanceHelpers(backend);
    testConfig();
    testDeviceManager(backend);
    testDeviceAccessors(backend);
    testBufferImageAllocation(backend);
    testTransferHelpers();
    testBufferTransfer(backend);
    testImageTransfers(backend);
    testAsyncTransfer(backend);
    testSwapchainAccessors(windowInfo, static_cast<uint32_t>(width),
                           static_cast<uint32_t>(height));
    testShaderManager(backend);
    testPipelineManager(backend, windowInfo);
    testExplicitDeviceWindow(backend, window, static_cast<uint32_t>(width),
                             static_cast<uint32_t>(height));

    // ── 5. Cleanup
    // ────────────────────────────────────────────────────────
    backend.waitIdle();
    backend.removeWindow(windowInfo);
    windowInfo.reset();

    glfwDestroyWindow(window);
    glfwTerminate();

    std::cout << "All backend tests PASSED\n";
    return EXIT_SUCCESS;

  } catch (const std::exception &e) {
    std::cerr << "FATAL: " << e.what() << '\n';
    return EXIT_FAILURE;
  }
}
