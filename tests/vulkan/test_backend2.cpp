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
// testGraphicsLoop
// =========================================================================
static void
testGraphicsLoop(Api &backend,
                 const std::shared_ptr<devices::WindowInfo> &windowInfo,
                 GLFWwindow *glfwWin) {
  int frameCount = 0;
  int w = 0, h = 0;

  for (int i = 0; i < 120; ++i) {
    glfwPollEvents();
    glfwGetFramebufferSize(glfwWin, &w, &h);

    if (windowInfo->swapchain && windowInfo->swapchain->needRecreation()) {
      Api::resizeWindow(windowInfo, static_cast<uint32_t>(w),
                        static_cast<uint32_t>(h));
      continue;
    }

    auto frame = backend.beginFrame(windowInfo);
    bool anyValid = false;
    if (!frame.valid) {
      Api::resizeWindow(frame.windowInfo, static_cast<uint32_t>(w),
                        static_cast<uint32_t>(h));
    } else {
      anyValid = true;
    }

    if (!anyValid) {
      Api::endFrame(frame); // ← always call endFrame to clear the
                            // acquired context
      continue;
    }

    Api::endFrame(frame);
    ++frameCount;
  }

  backend.waitIdle(); // ← drain GPU before cleanup asserts on allocations

  checkMsg(frameCount > 0, "testGraphicsLoop: no frames were rendered");
  std::cout << "[PASS] testGraphicsLoop (" << frameCount << " frames)\n";
}

// =========================================================================
// testRenderLoop
// =========================================================================
static void
testRenderLoop(Api &backend,
               const std::shared_ptr<devices::WindowInfo> &windowInfo,
               GLFWwindow *glfwWin) {
  // ── Prepare the pipeline ─────────────────────────────────────────────
  auto dev = backend.getFirstDevice();
  checkMsg(dev != nullptr, "getFirstDevice() returned nullptr");

  // Get swapchain colour format (all images share the same format)
  auto imgData = windowInfo->swapchain->getSwapchainImageData(0);
  checkMsg(imgData.has_value(),
           "testRenderLoop: could not retrieve swapchain image data");
  vk::Format colorFormat = imgData->format;

  // Create a simple empty pipeline layout
  vk::PipelineLayoutCreateInfo layoutCI{};
  vk::raii::PipelineLayout pipelineLayout{*dev->getDevicePtr(), layoutCI};

  // Build  pipeline info (exactly like test_main)
  pipelines::DynamicPipelineInfo dynInfo;
  dynInfo.tag.shaderTag = &g_shader;
  dynInfo.tag.layout = *pipelineLayout;
  dynInfo.inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;
  dynInfo.rasterization.cullMode = vk::CullModeFlagBits::eNone;
  dynInfo.depthStencil.depthTest = false;
  dynInfo.attachments.color = {colorFormat};
  dynInfo.multisample.samples = vk::SampleCountFlagBits::e1;

  // Get or create the pipeline
  auto pipelineResult = backend.createPipeline(dynInfo);
  checkMsg(pipelineResult.has_value(),
           "testRenderLoop: failed to create dynamic pipeline");
  auto pipeline = *pipelineResult; // shared_ptr<vk::raii::Pipeline>

  // ── Render loop ──────────────────────────────────────────────────────
  int frameCount = 0;
  int w = 0, h = 0;

  for (int i = 0; i < 120; ++i) {
    glfwPollEvents();
    glfwGetFramebufferSize(glfwWin, &w, &h);

    if (windowInfo->swapchain && windowInfo->swapchain->needRecreation()) {
      Api::resizeWindow(windowInfo, static_cast<uint32_t>(w),
                        static_cast<uint32_t>(h));
      continue;
    }

    auto frame = backend.beginFrame(windowInfo);
    bool anyValid = false;
    if (!frame.valid) {
      Api::resizeWindow(frame.windowInfo, static_cast<uint32_t>(w),
                        static_cast<uint32_t>(h));
    } else {
      anyValid = true;

      // Record draw commands into the already‑opened command buffer
      vk::CommandBuffer cmd = frame.cmd;
      cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);

      vk::Viewport viewport{0.0f,
                            0.0f,
                            static_cast<float>(frame.extent.width),
                            static_cast<float>(frame.extent.height),
                            0.0f,
                            1.0f};
      cmd.setViewport(0, viewport);
      cmd.setScissor(0, vk::Rect2D{{0, 0}, frame.extent});
      cmd.draw(3, 1, 0, 0); // 3 vertices → triangle
    }

    if (!anyValid) {
      Api::endFrame(frame);
      continue;
    }

    Api::endFrame(frame);
    ++frameCount;
  }

  backend.waitIdle(); // drain GPU before cleanup

  checkMsg(frameCount > 0, "testRenderLoop: no frames were rendered");
  std::cout << "[PASS] testRenderLoop (" << frameCount << " frames)\n";
}

static void testComputeDispatch(Api &backend) {
  auto dev = backend.getFirstDevice();
  checkMsg(dev != nullptr, "getFirstDevice() returned nullptr");

  constexpr uint32_t bufferElements = 256;
  constexpr vk::DeviceSize bufferSize = bufferElements * sizeof(uint32_t);

  // 1. Create storage buffer
  auto storageBuf = dev->createBuffer(devices::BufferCreateInfo{
      .size = bufferSize,
      .usage = vk::BufferUsageFlagBits::eStorageBuffer |
               vk::BufferUsageFlagBits::eTransferSrc,
      .access = devices::BufferCreateInfo::Access::gpuOnly,
      .debugName = "compute_storage"});

  // 2. Create descriptor set layout + pool (for compute stage)
  auto [setLayout, set, pool] = createStorageBufferDescriptorSet(
      *dev->getDevicePtr(), storageBuf.getBuffer(),
      vk::ShaderStageFlagBits::eCompute, bufferSize);

  // 3. Create pipeline layout with descriptor set + push constant range
  vk::PushConstantRange pushRange{vk::ShaderStageFlagBits::eCompute, 0,
                                  sizeof(uint32_t)};
  vk::PipelineLayoutCreateInfo finalLayoutCI{};
  finalLayoutCI.setSetLayouts(*setLayout);
  finalLayoutCI.setPushConstantRanges(pushRange);
  vk::raii::PipelineLayout finalLayout{*dev->getDevicePtr(), finalLayoutCI};

  // 4. Create compute pipeline with the correct layout
  pipelines::ComputePipelineInfo compInfo{
      .tag = {.shaderTag = &g_computeShader, .layout = *finalLayout}};
  auto compResult = backend.createPipeline(compInfo);
  checkMsg(compResult.has_value(), "compute pipeline creation failed");
  auto pipeline = *compResult;

  // 5. Create staging readback buffer
  auto staging = dev->createBuffer(devices::BufferCreateInfo{
      .size = bufferSize,
      .usage = vk::BufferUsageFlagBits::eTransferDst,
      .access = devices::BufferCreateInfo::Access::stagingReadback,
      .debugName = "compute_staging"});

  // 6. Record compute commands
  {
    auto &cmdPool = dev->getGraphicsPool();
    cmdPool.allocatePrimary(1);
    vk::CommandBuffer cmd = *cmdPool.primary[0];

    vk::CommandBufferBeginInfo beginInfo{
        vk::CommandBufferUsageFlagBits::eOneTimeSubmit};
    cmd.begin(beginInfo); // ← was missing

    cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *pipeline);
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *finalLayout, 0,
                           *set, {});

    // If the shader uses a push constant to control the fill value:
    uint32_t fillValue = 0xABABABAB;
    cmd.pushConstants<uint32_t>(
        *finalLayout, vk::ShaderStageFlagBits::eCompute, 0,
        fillValue); // adjust offset/size to match shader

    cmd.dispatch(static_cast<uint32_t>((bufferElements + 63) / 64), 1, 1);
    cmd.end();

    vk::SubmitInfo submit{};
    submit.setCommandBuffers(cmd);
    dev->getGraphicsQueue().submit(submit);
    dev->getGraphicsQueue().waitIdle();
  }

  // 7. Copy to staging and verify
  auto task = devices::transfer(dev, storageBuf, 0, staging, 0, bufferSize);
  task.get();
  staging.invalidate();
  auto *data = static_cast<const uint32_t *>(staging.map());
  for (uint32_t i = 0; i < bufferElements; ++i) {
    checkMsg(data[i] == 0xABABABAB,
             "testComputeDispatch: buffer element mismatch");
  }
  staging.unmap();

  std::cout << "[PASS] testComputeDispatch\n";
}

static void testComputeWithGraphicsSingleShader(
    Api &backend, const std::shared_ptr<devices::WindowInfo> &windowInfo,
    GLFWwindow *glfwWin) {

  auto dev = backend.getFirstDevice();
  checkMsg(dev != nullptr, "getFirstDevice() returned nullptr");

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
  auto compResult = backend.createPipeline(compInfo);
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
    vk::CommandBuffer cmd = *cmdPool.primary[0];
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
  // vertexBindings / vertexAttributes left empty – shader reads from SSBO

  auto gfxResult = backend.createPipeline(dynInfo);
  checkMsg(gfxResult.has_value(), "single shader: graphics pipeline failed");
  auto gfxPipe = *gfxResult;

  // ── Render 120 frames ──────────────────────────────────────────────
  int frameCount = 0;
  int w = 0, h = 0;

  for (int i = 0; i < 120; ++i) {
    glfwPollEvents();
    glfwGetFramebufferSize(glfwWin, &w, &h);

    if (windowInfo->swapchain && windowInfo->swapchain->needRecreation()) {
      Api::resizeWindow(windowInfo, static_cast<uint32_t>(w),
                        static_cast<uint32_t>(h));
      continue;
    }

    auto frame = backend.beginFrame(windowInfo);
    bool anyValid = false;
    if (!frame.valid) {
      Api::resizeWindow(frame.windowInfo, static_cast<uint32_t>(w),
                        static_cast<uint32_t>(h));
    } else {
      anyValid = true;
      vk::CommandBuffer cmd = frame.cmd;

      cmd.bindVertexBuffers(0, vertexStorage.getBuffer(), {0});
      cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *gfxPipe);
      cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipelineLayout,
                             0, *descSet, {});

      vk::Viewport vp{0,
                      0,
                      static_cast<float>(frame.extent.width),
                      static_cast<float>(frame.extent.height),
                      0,
                      1};
      cmd.setViewport(0, vp);
      cmd.setScissor(0, vk::Rect2D{{0, 0}, frame.extent});
      cmd.draw(3, 1, 0, 0);
    }

    if (!anyValid) {
      Api::endFrame(frame);
      continue;
    }
    Api::endFrame(frame);
    ++frameCount;
  }

  backend.waitIdle();
  checkMsg(frameCount > 0,
           "testComputeWithGraphicsSingleShader: no frames rendered");
  std::cout << "[PASS] testComputeWithGraphicsSingleShader (" << frameCount
            << " frames)\n";
}

static void testComputeWithGraphicsMultipleShaders(
    Api &backend, const std::shared_ptr<devices::WindowInfo> &windowInfo,
    GLFWwindow *glfwWin) {
  auto dev = backend.getFirstDevice();
  checkMsg(dev != nullptr, "getFirstDevice() returned nullptr");

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
  auto compResult = backend.createPipeline(compInfo);
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
    vk::CommandBuffer cmd = *cmdPool.primary[0];
    vk::CommandBufferBeginInfo beginInfo{
        vk::CommandBufferUsageFlagBits::eOneTimeSubmit};
    cmd.begin(beginInfo);

    cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *compPipe);
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *compPipelineLayout,
                           0, *compSet, {});
    cmd.dispatch(1, 1, 1);

    // Barrier: make the compute writes available to all subsequent commands
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

  auto gfxResult = backend.createPipeline(dynInfo);
  checkMsg(gfxResult.has_value(), "multiple shaders: graphics pipeline failed");
  auto gfxPipe = *gfxResult;

  auto [gfxSetLayoutR, gfxSet, gfxPool] = createStorageBufferDescriptorSet(
      *dev->getDevicePtr(), vertexStorage.getBuffer(),
      vk::ShaderStageFlagBits::eVertex, vbSize);

  // Render 120 frames
  int frameCount = 0;
  int w = 0, h = 0;
  for (int i = 0; i < 120; ++i) {
    glfwPollEvents();
    glfwGetFramebufferSize(glfwWin, &w, &h);

    if (windowInfo->swapchain && windowInfo->swapchain->needRecreation()) {
      Api::resizeWindow(windowInfo, static_cast<uint32_t>(w),
                        static_cast<uint32_t>(h));
      continue;
    }

    auto frame = backend.beginFrame(windowInfo);
    bool anyValid = false;

    if (!frame.valid) {
      Api::resizeWindow(frame.windowInfo, static_cast<uint32_t>(w),
                        static_cast<uint32_t>(h));
    } else {
      anyValid = true;
      vk::CommandBuffer cmd = frame.cmd;
      cmd.bindVertexBuffers(0, vertexStorage.getBuffer(), {0});
      cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *gfxPipe);
      cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                             *gfxPipelineLayout, 0, *gfxSet, {});
      vk::Viewport vp{
          0, 0, (float)frame.extent.width, (float)frame.extent.height, 0, 1};
      cmd.setViewport(0, vp);
      cmd.setScissor(0, vk::Rect2D{{0, 0}, frame.extent});
      cmd.draw(3, 1, 0, 0);
    }

    if (!anyValid) {
      Api::endFrame(frame);
      continue;
    }
    Api::endFrame(frame);
    ++frameCount;
  }

  backend.waitIdle();
  checkMsg(frameCount > 0,
           "testComputeWithGraphicsMultipleShaders: no frames rendered");
  std::cout << "[PASS] testComputeWithGraphicsMultipleShaders (" << frameCount
            << " frames)\n";
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

    testGraphicsLoop(backend, windowInfo, window);
    testRenderLoop(backend, windowInfo, window);

    testComputeDispatch(backend);
    testComputeWithGraphicsSingleShader(backend, windowInfo, window);
    testComputeWithGraphicsMultipleShaders(backend, windowInfo, window);

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
