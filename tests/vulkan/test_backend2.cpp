import vulkan_helper;

#include <vulkan/vulkan.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <cassert>
#include <cstdlib>

using namespace graphics::vulkan;

// --- Shader definition ---------------------------------------------------
shaders::Shader &getShader() {
  static shaders::Shader shader{
      .entryPoints = {{"vertexMain", vk::ShaderStageFlagBits::eVertex},
                      {"fragmentMain", vk::ShaderStageFlagBits::eFragment}},
      .sourcePath = "main.slang"};
  return shader;
}

// After g_shader definition
shaders::Shader &getComputeShader() {
  static shaders::Shader shader{
      .entryPoints = {{"main", vk::ShaderStageFlagBits::eCompute}},
      .sourcePath = "fill_buffer.slang"};
  return shader;
}

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

// ---------- Manual frame submission helper ----------
static void
submitAndPresent(const std::shared_ptr<devices::WindowInfo> &winInfo,
                 std::shared_ptr<devices::Device> dev, vk::CommandBuffer cmd,
                 uint32_t imageIndex) {
  cmd.end();
  vk::SubmitInfo submit{};
  submit.setCommandBuffers(cmd);
  auto queue = dev->getGraphicsQueue();
  auto res = winInfo->swapchain->submitAndPresent(
      queue, std::span<vk::SubmitInfo>(&submit, 1), imageIndex);
  // ignore result
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

static std::vector<std::unique_ptr<vk::raii::ImageView>>
createSwapchainImageViews(
    const std::shared_ptr<devices::WindowInfo> &windowInfo,
    const std::shared_ptr<devices::Device> &dev) {
  auto swapInfo = windowInfo->swapchain->getinfo();
  uint32_t count = swapInfo.imageCount;
  std::vector<std::unique_ptr<vk::raii::ImageView>> views;
  views.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    auto data = windowInfo->swapchain->getSwapchainImageData(i);
    if (!data)
      continue;
    vk::ImageViewCreateInfo viewInfo{};
    viewInfo.image = data->image;
    viewInfo.viewType = swapInfo.imageViewType;
    viewInfo.format = data->format;
    viewInfo.components = swapInfo.imageViewComponents;
    viewInfo.subresourceRange = swapInfo.imageViewSubresourceRange;
    views.push_back(
        std::make_unique<vk::raii::ImageView>(*dev->getDevicePtr(), viewInfo));
  }
  return views;
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
// testGraphicsLoop
// =========================================================================
static void
testGraphicsLoop(std::shared_ptr<devices::Device> dev,
                 const std::shared_ptr<devices::WindowInfo> &windowInfo,
                 GLFWwindow *glfwWin) {
  int frameCount = 0;
  int w = 0, h = 0;
  for (int i = 0; i < 120; ++i) {
    glfwPollEvents();
    glfwGetFramebufferSize(glfwWin, &w, &h);

    if (windowInfo->swapchain->needRecreation()) {
      windowInfo->swapchain->recreateSwapchain(w, h);
      continue;
    }

    auto acq = windowInfo->swapchain->acquireNextImage();
    if (!acq) {
      if (acq.error().code == devices::Swapchain::PresentError::Code::outOfDate)
        continue;
      throw std::runtime_error(acq.error().message);
    }
    uint32_t imageIndex = *acq;

    auto &pool = dev->getGraphicsPool();
    pool.allocatePrimary(1);
    vk::CommandBuffer cmd = *pool.primary.back();
    cmd.begin(vk::CommandBufferBeginInfo{
        vk::CommandBufferUsageFlagBits::eOneTimeSubmit});

    // Get the swapchain image
    auto swapchainImgData =
        windowInfo->swapchain->getSwapchainImageData(imageIndex);
    checkMsg(swapchainImgData.has_value(),
             "testGraphicsLoop: missing swapchain image data");
    vk::Image swapchainImage = swapchainImgData->image;

    // Transition to PRESENT_SRC_KHR
    vk::ImageMemoryBarrier preBarrier{
        vk::AccessFlagBits::eNone,
        vk::AccessFlagBits::eMemoryRead,
        vk::ImageLayout::eUndefined,
        vk::ImageLayout::ePresentSrcKHR,
        VK_QUEUE_FAMILY_IGNORED,
        VK_QUEUE_FAMILY_IGNORED,
        swapchainImage,
        vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
                        vk::PipelineStageFlagBits::eBottomOfPipe,
                        vk::DependencyFlags{}, {}, {}, preBarrier);

    submitAndPresent(windowInfo, dev, cmd, imageIndex);
    ++frameCount;
  }
  dev->waitIdle();
  checkMsg(frameCount > 0, "testGraphicsLoop: no frames rendered");
  std::cout << "[PASS] testGraphicsLoop (" << frameCount << " frames)\n";
}

// =========================================================================
// testRenderLoop
// =========================================================================
static void
testRenderLoop(std::shared_ptr<devices::Device> dev,
               std::shared_ptr<pipelines::Manager> pipelineManager,
               const std::shared_ptr<devices::WindowInfo> &windowInfo,
               GLFWwindow *glfwWin) {
  auto imgData = windowInfo->swapchain->getSwapchainImageData(0);
  checkMsg(imgData.has_value(), "testRenderLoop: no swapchain image data");
  vk::Format colorFormat = imgData->format;

  vk::PipelineLayoutCreateInfo layoutCI{};
  vk::raii::PipelineLayout pipelineLayout{*dev->getDevicePtr(), layoutCI};

  pipelines::DynamicPipelineInfo dynInfo;
  dynInfo.tag.shaderTag = &getShader();
  dynInfo.tag.layout = *pipelineLayout;
  dynInfo.inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;
  dynInfo.rasterization.cullMode = vk::CullModeFlagBits::eNone;
  dynInfo.depthStencil.depthTest = false;
  dynInfo.attachments.color = {colorFormat};
  dynInfo.multisample.samples = vk::SampleCountFlagBits::e1;

  auto pipeResult = pipelineManager->getOrCreate(dynInfo, dev->getDevicePtr());
  checkMsg(pipeResult.has_value(), "testRenderLoop: pipeline creation failed");
  auto pipeline = *pipeResult;

  // Create image views for swapchain images
  auto swapchainViews = createSwapchainImageViews(windowInfo, dev);

  int frameCount = 0;
  int w = 0, h = 0;
  for (int i = 0; i < 120; ++i) {
    glfwPollEvents();
    glfwGetFramebufferSize(glfwWin, &w, &h);

    if (windowInfo->swapchain->needRecreation()) {
      windowInfo->swapchain->recreateSwapchain(w, h);
      // Recreate views because swapchain images changed
      swapchainViews = createSwapchainImageViews(windowInfo, dev);
      continue;
    }

    auto acq = windowInfo->swapchain->acquireNextImage();
    if (!acq) {
      if (acq.error().code == devices::Swapchain::PresentError::Code::outOfDate)
        continue;
      throw std::runtime_error(acq.error().message);
    }
    uint32_t imageIndex = *acq;

    auto &pool = dev->getGraphicsPool();
    pool.allocatePrimary(1);
    vk::CommandBuffer cmd = *pool.primary.back();
    cmd.begin(vk::CommandBufferBeginInfo{
        vk::CommandBufferUsageFlagBits::eOneTimeSubmit});

    // Get swapchain image for this index
    auto swapchainImgData =
        windowInfo->swapchain->getSwapchainImageData(imageIndex);
    checkMsg(swapchainImgData.has_value(),
             "testRenderLoop: missing swapchain image data");
    vk::Image swapchainImage = swapchainImgData->image;

    // Transition swapchain image to color attachment optimal
    vk::ImageMemoryBarrier preBarrier{
        vk::AccessFlagBits::eNone,
        vk::AccessFlagBits::eColorAttachmentWrite,
        vk::ImageLayout::eUndefined,
        vk::ImageLayout::eColorAttachmentOptimal,
        VK_QUEUE_FAMILY_IGNORED,
        VK_QUEUE_FAMILY_IGNORED,
        swapchainImage,
        vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
                        vk::PipelineStageFlagBits::eColorAttachmentOutput,
                        vk::DependencyFlags{}, {}, {}, preBarrier);

    // Begin dynamic rendering
    vk::RenderingAttachmentInfo colorAttachment{
        **swapchainViews[imageIndex],
        vk::ImageLayout::eColorAttachmentOptimal,
        vk::ResolveModeFlagBits::eNone,
        nullptr,
        vk::ImageLayout::eUndefined,
        vk::AttachmentLoadOp::eClear,
        vk::AttachmentStoreOp::eStore,
        vk::ClearValue{std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}}};
    vk::RenderingInfo renderingInfo{
        {},
        vk::Rect2D{{0, 0},
                   {static_cast<uint32_t>(w), static_cast<uint32_t>(h)}},
        1,
        0,
        1,
        &colorAttachment,
        nullptr,
        nullptr};
    cmd.beginRendering(renderingInfo);

    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);
    vk::Viewport viewport{0, 0, (float)w, (float)h, 0, 1};
    cmd.setViewport(0, viewport);
    cmd.setScissor(
        0, vk::Rect2D{{0, 0},
                      {static_cast<uint32_t>(w), static_cast<uint32_t>(h)}});
    cmd.draw(3, 1, 0, 0);

    cmd.endRendering();

    // Transition back to present layout
    vk::ImageMemoryBarrier postBarrier{
        vk::AccessFlagBits::eColorAttachmentWrite,
        vk::AccessFlagBits::eMemoryRead,
        vk::ImageLayout::eColorAttachmentOptimal,
        vk::ImageLayout::ePresentSrcKHR,
        VK_QUEUE_FAMILY_IGNORED,
        VK_QUEUE_FAMILY_IGNORED,
        swapchainImage,
        vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput,
                        vk::PipelineStageFlagBits::eBottomOfPipe,
                        vk::DependencyFlags{}, {}, {}, postBarrier);

    submitAndPresent(windowInfo, dev, cmd, imageIndex);
    ++frameCount;
  }
  dev->waitIdle();
  checkMsg(frameCount > 0, "testRenderLoop: no frames rendered");
  std::cout << "[PASS] testRenderLoop (" << frameCount << " frames)\n";
}

static void
testComputeDispatch(std::shared_ptr<devices::Device> dev,
                    std::shared_ptr<pipelines::Manager> pipelineManager) {
  constexpr uint32_t bufferElements = 256;
  constexpr vk::DeviceSize bufferSize = bufferElements * sizeof(uint32_t);

  auto storageBuf = dev->createBuffer(devices::BufferCreateInfo{
      .size = bufferSize,
      .usage = vk::BufferUsageFlagBits::eStorageBuffer |
               vk::BufferUsageFlagBits::eTransferSrc,
      .access = devices::BufferCreateInfo::Access::gpuOnly,
      .debugName = "compute_storage"});

  auto [setLayout, set, pool] = createStorageBufferDescriptorSet(
      *dev->getDevicePtr(), storageBuf.getBuffer(),
      vk::ShaderStageFlagBits::eCompute, bufferSize);

  vk::PushConstantRange pushRange{vk::ShaderStageFlagBits::eCompute, 0,
                                  sizeof(uint32_t)};
  vk::PipelineLayoutCreateInfo layoutCI{};
  layoutCI.setSetLayouts(*setLayout);
  layoutCI.setPushConstantRanges(pushRange);
  vk::raii::PipelineLayout pipelineLayout{*dev->getDevicePtr(), layoutCI};

  pipelines::ComputePipelineInfo compInfo{
      .tag = {.shaderTag = &getComputeShader(), .layout = *pipelineLayout}};
  auto compResult = pipelineManager->getOrCreate(compInfo, dev->getDevicePtr());
  checkMsg(compResult.has_value(), "compute pipeline creation failed");
  auto pipeline = *compResult;

  auto staging = dev->createBuffer(devices::BufferCreateInfo{
      .size = bufferSize,
      .usage = vk::BufferUsageFlagBits::eTransferDst,
      .access = devices::BufferCreateInfo::Access::stagingReadback,
      .debugName = "compute_staging"});

  {
    auto &cmdPool = dev->getGraphicsPool();
    cmdPool.allocatePrimary(1);
    vk::CommandBuffer cmd = *cmdPool.primary.back();
    cmd.begin(vk::CommandBufferBeginInfo{
        vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
    cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *pipeline);
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *pipelineLayout, 0,
                           *set, {});
    uint32_t fillValue = 0xABABABAB;
    cmd.pushConstants<uint32_t>(
        *pipelineLayout, vk::ShaderStageFlagBits::eCompute, 0, fillValue);
    cmd.dispatch((bufferElements + 63) / 64, 1, 1);
    cmd.end();
    vk::SubmitInfo submit{};
    submit.setCommandBuffers(cmd);
    dev->getGraphicsQueue().submit(submit);
    dev->getGraphicsQueue().waitIdle();
  }

  auto task = devices::transfer(dev, storageBuf, 0, staging, 0, bufferSize);
  task.get();
  staging.invalidate();
  auto *data = static_cast<const uint32_t *>(staging.map());
  for (uint32_t i = 0; i < bufferElements; ++i) {
    checkMsg(data[i] == 0xABABABAB, "testComputeDispatch: mismatch");
  }
  staging.unmap();
  std::cout << "[PASS] testComputeDispatch\n";
}

static void testComputeWithGraphicsSingleShader(
    std::shared_ptr<devices::Device> dev,
    std::shared_ptr<pipelines::Manager> pipelineManager,
    const std::shared_ptr<devices::WindowInfo> &windowInfo,
    GLFWwindow *glfwWin) {

  // ── Shader that contains all three stages ──────────────────────────
  static shaders::Shader singleShader{
      .entryPoints = {{"compMain", vk::ShaderStageFlagBits::eCompute},
                      {"vertexMain", vk::ShaderStageFlagBits::eVertex},
                      {"fragmentMain", vk::ShaderStageFlagBits::eFragment}},
      .sourcePath = "graphics_with_compute.slang"};

  // ── GPU‑only buffer for vertex data (std430 layout) ────────────────
  auto vertexStorage = dev->createBuffer(devices::BufferCreateInfo{
      .size = vbSize, // 96 bytes, 3 × 32
      .usage = vk::BufferUsageFlagBits::eStorageBuffer |
               vk::BufferUsageFlagBits::eVertexBuffer,
      .access = devices::BufferCreateInfo::Access::gpuOnly,
      .debugName = "single_vertex_storage"});

  // ── Shared descriptor set layout (SSBO for compute + vertex) ──────
  vk::DescriptorSetLayoutBinding sharedBinding{
      0, vk::DescriptorType::eStorageBuffer, 1,
      vk::ShaderStageFlagBits::eCompute | vk::ShaderStageFlagBits::eVertex};
  vk::DescriptorSetLayoutCreateInfo sharedSetLayoutCI{{}, sharedBinding};
  vk::raii::DescriptorSetLayout sharedSetLayout{*dev->getDevicePtr(),
                                                sharedSetLayoutCI};

  // ── Pipeline layout (one set, for both pipelines) ──────────────────
  vk::PipelineLayoutCreateInfo layoutCI{};
  layoutCI.setSetLayouts(*sharedSetLayout);
  vk::raii::PipelineLayout pipelineLayout{*dev->getDevicePtr(), layoutCI};

  // ── Compute pipeline ───────────────────────────────────────────────
  pipelines::ComputePipelineInfo compInfo{
      .tag = {.shaderTag = &singleShader, .layout = *pipelineLayout}};
  auto compResult = pipelineManager->getOrCreate(compInfo, dev->getDevicePtr());
  checkMsg(compResult.has_value(), "single shader: compute pipeline failed");
  auto compPipe = *compResult;

  // ── Descriptor set (pointing to the whole buffer) ──────────────────
  auto [setLayoutRet, descSet, pool] = createStorageBufferDescriptorSet(
      *dev->getDevicePtr(), vertexStorage.getBuffer(),
      vk::ShaderStageFlagBits::eCompute | vk::ShaderStageFlagBits::eVertex,
      vbSize);

  // ── Fill the buffer with a compute dispatch ────────────────────────
  {
    auto &cmdPool = dev->getGraphicsPool();
    cmdPool.allocatePrimary(1);
    vk::CommandBuffer cmd = *cmdPool.primary.back();
    vk::CommandBufferBeginInfo beginInfo{
        vk::CommandBufferUsageFlagBits::eOneTimeSubmit};
    cmd.begin(beginInfo);

    cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *compPipe);
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *pipelineLayout, 0,
                           *descSet, {});
    cmd.dispatch(1, 1, 1);

    // Barrier: make the written data visible to the vertex stage
    vk::BufferMemoryBarrier2 barrier2;
    barrier2.srcStageMask = vk::PipelineStageFlagBits2::eComputeShader;
    barrier2.srcAccessMask = vk::AccessFlagBits2::eShaderWrite;
    barrier2.dstStageMask = vk::PipelineStageFlagBits2::eAllCommands;
    barrier2.dstAccessMask =
        vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite;
    barrier2.buffer = vertexStorage.getBuffer();
    barrier2.offset = 0;
    barrier2.size = VK_WHOLE_SIZE;
    barrier2.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier2.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

    vk::DependencyInfo depInfo{};
    depInfo.setBufferMemoryBarriers(barrier2);
    cmd.pipelineBarrier2(depInfo);
    cmd.end();

    vk::SubmitInfo submit{};
    submit.setCommandBuffers(cmd);
    dev->getGraphicsQueue().submit(submit);
    dev->getGraphicsQueue().waitIdle();
  }

  // ── Graphics pipeline (dynamic rendering, no vertex inputs) ────────
  auto imgData = windowInfo->swapchain->getSwapchainImageData(0);
  checkMsg(imgData.has_value(), "no swapchain image data");
  vk::Format colorFormat = imgData->format;

  vk::VertexInputBindingDescription bindingDesc{
      0,                    // binding
      sizeof(Std430Vertex), // stride = 32
      vk::VertexInputRate::eVertex};
  std::vector<vk::VertexInputAttributeDescription> attrDescs = {
      {0, 0, vk::Format::eR32G32Sfloat, 0},    // position at offset  0
      {1, 0, vk::Format::eR32G32B32Sfloat, 16} // color    at offset 16
  };

  pipelines::DynamicPipelineInfo dynInfo;
  dynInfo.tag.shaderTag = &singleShader;
  dynInfo.tag.layout = *pipelineLayout;
  dynInfo.vertexBindings = {bindingDesc};
  dynInfo.vertexAttributes = attrDescs;
  dynInfo.inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;
  dynInfo.rasterization.cullMode = vk::CullModeFlagBits::eNone;
  dynInfo.depthStencil.depthTest = vk::False;
  dynInfo.attachments.color = {colorFormat};

  auto gfxResult = pipelineManager->getOrCreate(dynInfo, dev->getDevicePtr());
  checkMsg(gfxResult.has_value(), "single shader: graphics pipeline failed");
  auto gfxPipe = *gfxResult;

  // Create swapchain image views
  auto swapchainViews = createSwapchainImageViews(windowInfo, dev);

  // ── Render 120 frames ──────────────────────────────────────────────
  int frameCount = 0;
  int w = 0, h = 0;

  for (int i = 0; i < 120; ++i) {
    glfwPollEvents();
    glfwGetFramebufferSize(glfwWin, &w, &h);

    if (windowInfo->swapchain->needRecreation()) {
      windowInfo->swapchain->recreateSwapchain(w, h);
      swapchainViews = createSwapchainImageViews(windowInfo, dev);
      continue;
    }

    auto acq = windowInfo->swapchain->acquireNextImage();
    if (!acq) {
      if (acq.error().code == devices::Swapchain::PresentError::Code::outOfDate)
        continue;
      throw std::runtime_error(acq.error().message);
    }
    uint32_t imageIndex = *acq;

    auto &pool = dev->getGraphicsPool();
    pool.allocatePrimary(1);
    vk::CommandBuffer cmd = *pool.primary.back();
    cmd.begin(vk::CommandBufferBeginInfo{
        vk::CommandBufferUsageFlagBits::eOneTimeSubmit});

    // Get swapchain image
    auto swapchainImgData =
        windowInfo->swapchain->getSwapchainImageData(imageIndex);
    checkMsg(swapchainImgData.has_value(), "missing swapchain image data");
    vk::Image swapchainImage = swapchainImgData->image;

    // Transition to color attachment optimal
    vk::ImageMemoryBarrier preBarrier{
        vk::AccessFlagBits::eNone,
        vk::AccessFlagBits::eColorAttachmentWrite,
        vk::ImageLayout::eUndefined,
        vk::ImageLayout::eColorAttachmentOptimal,
        VK_QUEUE_FAMILY_IGNORED,
        VK_QUEUE_FAMILY_IGNORED,
        swapchainImage,
        vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
                        vk::PipelineStageFlagBits::eColorAttachmentOutput,
                        vk::DependencyFlags{}, {}, {}, preBarrier);

    // Begin dynamic rendering
    vk::RenderingAttachmentInfo colorAttachment{
        **swapchainViews[imageIndex],
        vk::ImageLayout::eColorAttachmentOptimal,
        vk::ResolveModeFlagBits::eNone,
        nullptr,
        vk::ImageLayout::eUndefined,
        vk::AttachmentLoadOp::eClear,
        vk::AttachmentStoreOp::eStore,
        vk::ClearValue{std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}}};
    vk::RenderingInfo renderingInfo{
        {},
        vk::Rect2D{{0, 0},
                   {static_cast<uint32_t>(w), static_cast<uint32_t>(h)}},
        1,
        0,
        1,
        &colorAttachment,
        nullptr,
        nullptr};
    cmd.beginRendering(renderingInfo);

    // Draw commands
    cmd.bindVertexBuffers(0, vertexStorage.getBuffer(), {0});
    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *gfxPipe);
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipelineLayout, 0,
                           *descSet, {});
    vk::Viewport vp{0, 0, (float)w, (float)h, 0, 1};
    cmd.setViewport(0, vp);
    cmd.setScissor(
        0, vk::Rect2D{{0, 0},
                      {static_cast<uint32_t>(w), static_cast<uint32_t>(h)}});
    cmd.draw(3, 1, 0, 0);

    cmd.endRendering();

    // Transition to present
    vk::ImageMemoryBarrier postBarrier{
        vk::AccessFlagBits::eColorAttachmentWrite,
        vk::AccessFlagBits::eMemoryRead,
        vk::ImageLayout::eColorAttachmentOptimal,
        vk::ImageLayout::ePresentSrcKHR,
        VK_QUEUE_FAMILY_IGNORED,
        VK_QUEUE_FAMILY_IGNORED,
        swapchainImage,
        vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput,
                        vk::PipelineStageFlagBits::eBottomOfPipe,
                        vk::DependencyFlags{}, {}, {}, postBarrier);

    submitAndPresent(windowInfo, dev, cmd, imageIndex);
    ++frameCount;
  }

  dev->waitIdle();
  checkMsg(frameCount > 0,
           "testComputeWithGraphicsSingleShader: no frames rendered");
  std::cout << "[PASS] testComputeWithGraphicsSingleShader (" << frameCount
            << " frames)\n";
}

static void testComputeWithGraphicsMultipleShaders(
    std::shared_ptr<devices::Device> dev,
    std::shared_ptr<pipelines::Manager> pipelineManager,
    const std::shared_ptr<devices::WindowInfo> &windowInfo,
    GLFWwindow *glfwWin) {

  static shaders::Shader compShader{
      .entryPoints = {{"main", vk::ShaderStageFlagBits::eCompute}},
      .sourcePath = "generate_triangle.slang"};

  static shaders::Shader gfxShader{
      .entryPoints = {{"vertexMain", vk::ShaderStageFlagBits::eVertex},
                      {"fragmentMain", vk::ShaderStageFlagBits::eFragment}},
      .sourcePath = "graphics_with_ssbo.slang"};

  auto vertexStorage = dev->createBuffer(devices::BufferCreateInfo{
      .size = vbSize,
      .usage = vk::BufferUsageFlagBits::eStorageBuffer |
               vk::BufferUsageFlagBits::eVertexBuffer,
      .access = devices::BufferCreateInfo::Access::gpuOnly,
      .debugName = "multi_vertex_storage"});

  // Compute pipeline
  vk::DescriptorSetLayoutBinding compBinding{
      0, vk::DescriptorType::eStorageBuffer, 1,
      vk::ShaderStageFlagBits::eCompute};
  vk::DescriptorSetLayoutCreateInfo compSetLayoutCI{{}, compBinding};
  vk::raii::DescriptorSetLayout compSetLayout{*dev->getDevicePtr(),
                                              compSetLayoutCI};
  vk::PipelineLayoutCreateInfo compLayoutCI{};
  compLayoutCI.setSetLayouts(*compSetLayout);
  vk::raii::PipelineLayout compPipelineLayout{*dev->getDevicePtr(),
                                              compLayoutCI};

  pipelines::ComputePipelineInfo compInfo{
      .tag = {.shaderTag = &compShader, .layout = *compPipelineLayout}};
  auto compResult = pipelineManager->getOrCreate(compInfo, dev->getDevicePtr());
  checkMsg(compResult.has_value(), "multiple shaders: compute pipeline failed");
  auto compPipe = *compResult;

  auto [compSetLayoutReturn, compSet, compPool] =
      createStorageBufferDescriptorSet(
          *dev->getDevicePtr(), vertexStorage.getBuffer(),
          vk::ShaderStageFlagBits::eCompute, vbSize);

  // Dispatch compute once
  {
    auto &cmdPool = dev->getComputePool();
    cmdPool.allocatePrimary(1);
    vk::CommandBuffer cmd = *cmdPool.primary.back();
    vk::CommandBufferBeginInfo beginInfo{
        vk::CommandBufferUsageFlagBits::eOneTimeSubmit};
    cmd.begin(beginInfo);

    cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *compPipe);
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *compPipelineLayout,
                           0, *compSet, {});
    cmd.dispatch(1, 1, 1);

    vk::BufferMemoryBarrier2 barrier2;
    barrier2.srcStageMask = vk::PipelineStageFlagBits2::eComputeShader;
    barrier2.srcAccessMask = vk::AccessFlagBits2::eShaderWrite;
    barrier2.dstStageMask = vk::PipelineStageFlagBits2::eAllCommands;
    barrier2.dstAccessMask =
        vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite;
    barrier2.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier2.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier2.buffer = vertexStorage.getBuffer();
    barrier2.offset = 0;
    barrier2.size = VK_WHOLE_SIZE;

    vk::DependencyInfo depInfo{};
    depInfo.setBufferMemoryBarriers(barrier2);
    cmd.pipelineBarrier2(depInfo);
    cmd.end();

    vk::SubmitInfo submit{};
    submit.setCommandBuffers(cmd);
    dev->getComputeQueue().submit(submit);
    dev->getComputeQueue().waitIdle();
  }

  // Graphics pipeline
  vk::DescriptorSetLayoutBinding gfxBinding{
      0, vk::DescriptorType::eStorageBuffer, 1,
      vk::ShaderStageFlagBits::eVertex};
  vk::DescriptorSetLayoutCreateInfo gfxSetLayoutCI{{}, gfxBinding};
  vk::raii::DescriptorSetLayout gfxSetLayout{*dev->getDevicePtr(),
                                             gfxSetLayoutCI};
  vk::PipelineLayoutCreateInfo gfxLayoutCI{};
  gfxLayoutCI.setSetLayouts(*gfxSetLayout);
  vk::raii::PipelineLayout gfxPipelineLayout{*dev->getDevicePtr(), gfxLayoutCI};

  auto imgData = windowInfo->swapchain->getSwapchainImageData(0);
  checkMsg(imgData.has_value(), "no swapchain image data");
  vk::Format colorFormat = imgData->format;

  vk::VertexInputBindingDescription bindingDesc{
      0,                    // binding
      sizeof(Std430Vertex), // stride = 32
      vk::VertexInputRate::eVertex};
  std::vector<vk::VertexInputAttributeDescription> attrDescs = {
      {0, 0, vk::Format::eR32G32Sfloat, 0},    // position at offset  0
      {1, 0, vk::Format::eR32G32B32Sfloat, 16} // color    at offset 16
  };

  pipelines::DynamicPipelineInfo dynInfo;
  dynInfo.tag.shaderTag = &gfxShader;
  dynInfo.tag.layout = *gfxPipelineLayout;
  dynInfo.vertexBindings = {bindingDesc};
  dynInfo.vertexAttributes = attrDescs;
  dynInfo.inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;
  dynInfo.rasterization.cullMode = vk::CullModeFlagBits::eNone;
  dynInfo.depthStencil.depthTest = vk::False;
  dynInfo.attachments.color = {colorFormat};

  auto gfxResult = pipelineManager->getOrCreate(dynInfo, dev->getDevicePtr());
  checkMsg(gfxResult.has_value(), "multiple shaders: graphics pipeline failed");
  auto gfxPipe = *gfxResult;

  auto [gfxSetLayoutR, gfxSet, gfxPool] = createStorageBufferDescriptorSet(
      *dev->getDevicePtr(), vertexStorage.getBuffer(),
      vk::ShaderStageFlagBits::eVertex, vbSize);

  // Create swapchain image views
  auto swapchainViews = createSwapchainImageViews(windowInfo, dev);

  // Render 120 frames
  int frameCount = 0;
  int w = 0, h = 0;
  for (int i = 0; i < 120; ++i) {
    glfwPollEvents();
    glfwGetFramebufferSize(glfwWin, &w, &h);

    if (windowInfo->swapchain->needRecreation()) {
      windowInfo->swapchain->recreateSwapchain(w, h);
      swapchainViews = createSwapchainImageViews(windowInfo, dev);
      continue;
    }

    auto acq = windowInfo->swapchain->acquireNextImage();
    if (!acq) {
      if (acq.error().code == devices::Swapchain::PresentError::Code::outOfDate)
        continue;
      throw std::runtime_error(acq.error().message);
    }
    uint32_t imageIndex = *acq;

    auto &pool = dev->getGraphicsPool();
    pool.allocatePrimary(1);
    vk::CommandBuffer cmd = *pool.primary.back();
    cmd.begin(vk::CommandBufferBeginInfo{
        vk::CommandBufferUsageFlagBits::eOneTimeSubmit});

    // Get swapchain image
    auto swapchainImgData =
        windowInfo->swapchain->getSwapchainImageData(imageIndex);
    checkMsg(swapchainImgData.has_value(), "missing swapchain image data");
    vk::Image swapchainImage = swapchainImgData->image;

    // Transition to color attachment optimal
    vk::ImageMemoryBarrier preBarrier{
        vk::AccessFlagBits::eNone,
        vk::AccessFlagBits::eColorAttachmentWrite,
        vk::ImageLayout::eUndefined,
        vk::ImageLayout::eColorAttachmentOptimal,
        VK_QUEUE_FAMILY_IGNORED,
        VK_QUEUE_FAMILY_IGNORED,
        swapchainImage,
        vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
                        vk::PipelineStageFlagBits::eColorAttachmentOutput,
                        vk::DependencyFlags{}, {}, {}, preBarrier);

    // Begin dynamic rendering
    vk::RenderingAttachmentInfo colorAttachment{
        **swapchainViews[imageIndex],
        vk::ImageLayout::eColorAttachmentOptimal,
        vk::ResolveModeFlagBits::eNone,
        nullptr,
        vk::ImageLayout::eUndefined,
        vk::AttachmentLoadOp::eClear,
        vk::AttachmentStoreOp::eStore,
        vk::ClearValue{std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}}};
    vk::RenderingInfo renderingInfo{
        {},
        vk::Rect2D{{0, 0},
                   {static_cast<uint32_t>(w), static_cast<uint32_t>(h)}},
        1,
        0,
        1,
        &colorAttachment,
        nullptr,
        nullptr};
    cmd.beginRendering(renderingInfo);

    // Draw commands
    cmd.bindVertexBuffers(0, vertexStorage.getBuffer(), {0});
    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *gfxPipe);
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *gfxPipelineLayout,
                           0, *gfxSet, {});
    vk::Viewport vp{0, 0, (float)w, (float)h, 0, 1};
    cmd.setViewport(0, vp);
    cmd.setScissor(
        0, vk::Rect2D{{0, 0},
                      {static_cast<uint32_t>(w), static_cast<uint32_t>(h)}});
    cmd.draw(3, 1, 0, 0);

    cmd.endRendering();

    // Transition to present
    vk::ImageMemoryBarrier postBarrier{
        vk::AccessFlagBits::eColorAttachmentWrite,
        vk::AccessFlagBits::eMemoryRead,
        vk::ImageLayout::eColorAttachmentOptimal,
        vk::ImageLayout::ePresentSrcKHR,
        VK_QUEUE_FAMILY_IGNORED,
        VK_QUEUE_FAMILY_IGNORED,
        swapchainImage,
        vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput,
                        vk::PipelineStageFlagBits::eBottomOfPipe,
                        vk::DependencyFlags{}, {}, {}, postBarrier);

    submitAndPresent(windowInfo, dev, cmd, imageIndex);
    ++frameCount;
  }

  dev->waitIdle();
  checkMsg(frameCount > 0,
           "testComputeWithGraphicsMultipleShaders: no frames rendered");
  std::cout << "[PASS] testComputeWithGraphicsMultipleShaders (" << frameCount
            << " frames)\n";
}

// =========================================================================
int main() {
  try {
    // GLFW setup
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

    // Pool & managers
    auto poolManager = std::make_shared<concurrency::pool::Manager>();
    auto instance = std::make_shared<instances::Instance>();

    // Add required GLFW extensions
    uint32_t extCount = 0;
    const char **glfwExts = glfwGetRequiredInstanceExtensions(&extCount);
    if (!glfwExts)
      throw std::runtime_error("glfwGetRequiredInstanceExtensions failed");
    for (uint32_t i = 0; i < extCount; ++i)
      instances::Config::instance().addInstanceExtension(glfwExts[i]);

    auto deviceManager = std::make_shared<devices::Manager>(
        instance->getInstancePtr(), poolManager);
    auto shaderManager = std::make_shared<shaders::Manager>();
    auto pipelineManager = std::make_shared<pipelines::Manager>(shaderManager);

    auto entries = deviceManager->getDeviceEntries();
    if (entries.empty())
      throw std::runtime_error("No Vulkan device found");
    auto device = entries.front().device;

    // Create window surface
    VkSurfaceKHR rawSurface;
    if (glfwCreateWindowSurface(**instance->getInstancePtr(), window, nullptr,
                                &rawSurface) != VK_SUCCESS)
      throw std::runtime_error("Surface creation failed");

    auto windowInfo = std::make_shared<devices::WindowInfo>();
    windowInfo->surface = std::make_unique<vk::raii::SurfaceKHR>(
        *instance->getInstancePtr(), rawSurface);
    windowInfo->instance = instance->getInstancePtr();

    int width, height;
    glfwGetFramebufferSize(window, &width, &height);
    devices::Swapchain::SwapchainInfo swapInfo;
    swapInfo.extent = vk::Extent2D{static_cast<uint32_t>(width),
                                   static_cast<uint32_t>(height)};
    device->createWindow(windowInfo, 2, swapInfo);

    // Run tests
    testGraphicsLoop(device, windowInfo, window);
    testRenderLoop(device, pipelineManager, windowInfo, window);
    testComputeDispatch(device, pipelineManager);

    testComputeWithGraphicsSingleShader(device, pipelineManager, windowInfo,
                                        window);
    testComputeWithGraphicsMultipleShaders(device, pipelineManager, windowInfo,
                                           window);

    // Cleanup
    device->waitIdle();
    device->removeWindow(windowInfo);
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
