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

// Helper to create a storage buffer descriptor set
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

// Alias for brevity
using CompositorType = compositors::Compositor<SwapchainRenderTarget>;

// -------------------------------------------------------------------------
// testCompositorBasicRender – one scene, render several frames
// -------------------------------------------------------------------------
static void testCompositorBasicRender(
    CompositorType &compositor,
    const std::shared_ptr<devices::WindowInfo> &windowInfo,
    std::shared_ptr<devices::Device> device, GLFWwindow *glfwWin) {

  // Ensure no leftover scenes from previous tests
  compositor.clearSceneFactories();

  ClearScene redScene({1.0f, 0.0f, 0.0f, 1.0f}); // red
  compositor.addScene(redScene);

  for (int i = 0; i < 120; ++i) {
    glfwPollEvents();

    bool result = compositor.render().get();
    if (!result) {
      if (windowInfo->swapchain->needRecreation()) {
        int w, h;
        glfwGetFramebufferSize(glfwWin, &w, &h);
        windowInfo->swapchain->recreateSwapchain(w, h);
        compositor.target().recreate(); // rebuild image views
        --i;                            // retry this frame
        continue;
      }
      throw std::runtime_error("Compositor render failed");
    }
  }

  std::cout << "[PASS] testCompositorBasicRender\n";
}

// -------------------------------------------------------------------------
// testCompositorClearScenes – no scenes, should still render
// -------------------------------------------------------------------------
static void testCompositorClearScenes(
    CompositorType &compositor,
    const std::shared_ptr<devices::WindowInfo> &windowInfo,
    std::shared_ptr<devices::Device> device, GLFWwindow *glfwWin) {

  compositor.clearSceneFactories();

  // Show green for one frame
  ClearScene greenScene({0.0f, 1.0f, 0.0f, 1.0f}); // green
  compositor.addScene(greenScene);

  for (int i = 0; i < 50; ++i) {
    glfwPollEvents();
    bool result = compositor.render().get();
    // handle recreation as before, but simplify for brevity
    if (!result) {
      if (windowInfo->swapchain->needRecreation()) {
        int w, h;
        glfwGetFramebufferSize(glfwWin, &w, &h);
        windowInfo->swapchain->recreateSwapchain(w, h);
        compositor.target().recreate();
        --i;
        continue;
      }
      throw std::runtime_error("Compositor render with scene failed");
    }
  }

  // Now clear scenes and render the rest (black/no scenes)
  compositor.clearSceneFactories();

  for (int i = 0; i < 120; ++i) {
    glfwPollEvents();

    bool result = compositor.render().get();
    if (!result) {
      if (windowInfo->swapchain->needRecreation()) {
        int w, h;
        glfwGetFramebufferSize(glfwWin, &w, &h);
        windowInfo->swapchain->recreateSwapchain(w, h);
        compositor.target().recreate();
        --i;
        continue;
      }
      throw std::runtime_error("Compositor render with no scenes failed");
    }
  }

  std::cout << "[PASS] testCompositorClearScenes\n";
}

// -------------------------------------------------------------------------
// testCompositorMultipleScenes – two scenes, ensure they run in order
// -------------------------------------------------------------------------
static void testCompositorMultipleScenes(
    CompositorType &compositor,
    const std::shared_ptr<devices::WindowInfo> &windowInfo,
    std::shared_ptr<devices::Device> device,
    std::shared_ptr<pipelines::Manager> pipelineManager, GLFWwindow *glfwWin) {
  compositor.clearSceneFactories();

  // Get swapchain extent
  auto extent = windowInfo->swapchain->getinfo().extent;
  uint32_t halfWidth = extent.width / 2;

  // Blue left half
  RegionClearScene blueLeft({0.0f, 0.0f, 1.0f, 1.0f},
                            vk::Rect2D{{0, 0}, {halfWidth, extent.height}});
  auto blue = compositor.addScene(blueLeft);

  // Yellow right half
  RegionClearScene yellowRight(
      {1.0f, 1.0f, 0.0f, 1.0f},
      vk::Rect2D{{static_cast<int32_t>(halfWidth), 0},
                 {extent.width - halfWidth, extent.height}});
  auto yellow = compositor.addScene(yellowRight);

  // Translucent red fullscreen overlay (alpha 0.5)
  BlendScene redOverlay(pipelineManager, device, {1.0f, 0.0f, 0.0f, 0.5f});
  auto red = compositor.addScene(std::move(redOverlay));

  for (int i = 0; i < 60; ++i) {
    glfwPollEvents();

    bool result = compositor.render().get();
    if (!result) {
      if (windowInfo->swapchain->needRecreation()) {
        int w, h;
        glfwGetFramebufferSize(glfwWin, &w, &h);
        windowInfo->swapchain->recreateSwapchain(w, h);
        compositor.target().recreate();
        --i;
        continue;
      }
      throw std::runtime_error("Compositor render with multiple scenes failed");
    }
  }

  compositor.removeScene(red);

  for (int i = 0; i < 60; ++i) {
    glfwPollEvents();

    bool result = compositor.render().get();
    if (!result) {
      if (windowInfo->swapchain->needRecreation()) {
        int w, h;
        glfwGetFramebufferSize(glfwWin, &w, &h);
        windowInfo->swapchain->recreateSwapchain(w, h);
        compositor.target().recreate();
        --i;
        continue;
      }
      throw std::runtime_error("Compositor render with multiple scenes failed");
    }
  }

  BlendScene redOverlay2(pipelineManager, device, {1.0f, 0.0f, 0.0f, 0.5f});
  red = compositor.addScene(std::move(redOverlay2));

  compositor.removeScenes(std::vector{blue, yellow});

  for (int i = 0; i < 60; ++i) {
    glfwPollEvents();

    bool result = compositor.render().get();
    if (!result) {
      if (windowInfo->swapchain->needRecreation()) {
        int w, h;
        glfwGetFramebufferSize(glfwWin, &w, &h);
        windowInfo->swapchain->recreateSwapchain(w, h);
        compositor.target().recreate();
        --i;
        continue;
      }
      throw std::runtime_error("Compositor render with multiple scenes failed");
    }
  }

  blue = compositor.addScene(blueLeft);
  yellow = compositor.addScene(yellowRight, CompositorType::Position::START);

  for (int i = 0; i < 60; ++i) {
    glfwPollEvents();

    bool result = compositor.render().get();
    if (!result) {
      if (windowInfo->swapchain->needRecreation()) {
        int w, h;
        glfwGetFramebufferSize(glfwWin, &w, &h);
        windowInfo->swapchain->recreateSwapchain(w, h);
        compositor.target().recreate();
        --i;
        continue;
      }
      throw std::runtime_error("Compositor render with multiple scenes failed");
    }
  }

  compositor.clearSceneFactories();

  for (int i = 0; i < 60; ++i) {
    glfwPollEvents();

    bool result = compositor.render().get();
    if (!result) {
      if (windowInfo->swapchain->needRecreation()) {
        int w, h;
        glfwGetFramebufferSize(glfwWin, &w, &h);
        windowInfo->swapchain->recreateSwapchain(w, h);
        compositor.target().recreate();
        --i;
        continue;
      }
      throw std::runtime_error("Compositor render with multiple scenes failed");
    }
  }

  std::cout << "[PASS] testCompositorMultipleScenes\n";
}

// -------------------------------------------------------------------------
// testCompositorComputeOnly
// -------------------------------------------------------------------------
static void testCompositorComputeOnly(
    CompositorType &compositor,
    const std::shared_ptr<devices::WindowInfo> &windowInfo,
    std::shared_ptr<devices::Device> device,
    std::shared_ptr<pipelines::Manager> pipelineManager, GLFWwindow *glfwWin) {

  compositor.clearSceneFactories();

  constexpr uint32_t bufferElements = 256;
  constexpr vk::DeviceSize bufferSize = bufferElements * sizeof(uint32_t);
  constexpr uint32_t fillValue = 0xABABABAB;

  // Create storage and staging buffers
  auto storageBuf = device->createBuffer(devices::BufferCreateInfo{
      .size = bufferSize,
      .usage = vk::BufferUsageFlagBits::eStorageBuffer |
               vk::BufferUsageFlagBits::eTransferSrc,
      .access = devices::BufferCreateInfo::Access::gpuOnly,
      .debugName = "compute_storage"});
  auto stagingBuf = device->createBuffer(devices::BufferCreateInfo{
      .size = bufferSize,
      .usage = vk::BufferUsageFlagBits::eTransferDst,
      .access = devices::BufferCreateInfo::Access::stagingReadback,
      .debugName = "compute_staging"});

  // Create descriptor set for compute
  auto [setLayout, descSet, pool] = createStorageBufferDescriptorSet(
      *device->getDevicePtr(), storageBuf.getBuffer(),
      vk::ShaderStageFlagBits::eCompute, bufferSize);

  // Create pipeline layout with push constant range
  vk::PushConstantRange pushRange{vk::ShaderStageFlagBits::eCompute, 0,
                                  sizeof(uint32_t)};
  vk::PipelineLayoutCreateInfo layoutCI{};
  layoutCI.setSetLayouts(*setLayout);
  layoutCI.setPushConstantRanges(pushRange);
  vk::raii::PipelineLayout pipelineLayout{*device->getDevicePtr(), layoutCI};

  // Create compute pipeline
  pipelines::ComputePipelineInfo compInfo{
      .tag = {.shaderTag = &getComputeShader(), .layout = *pipelineLayout}};
  auto compResult =
      pipelineManager->getOrCreate(compInfo, device->getDevicePtr());
  checkMsg(compResult.has_value(), "compute pipeline creation failed");
  auto computePipeline = *compResult;

  // We need to set the push constant value; we'll do it inside the scene.
  // But our ComputeScene class doesn't currently push constants.
  // We'll extend it to optionally push a constant. For simplicity, we set
  // the fill value using push constants. Let's modify ComputeScene to accept
  // a push constant value and range.
  // For now, we can add a push constant in the record method if we pass it.
  // We'll create a new scene inline that does the push.
  // (Alternatively, modify ComputeScene to include push constant)
  // To save time, we'll use a lambda? Scene must be a class, so we'll
  // implement a slightly different compute scene that pushes the value.
  // We'll add a member for push constant and set it.
  // Quick patch: modify ComputeScene class above to include optional push.
  // We'll assume it's been updated to push the fill value.
  // For this answer, we'll note it.

  // Create ComputeScene (assuming updated with push constant)
  FillBufferComputeScene computeScene(device, computePipeline, *descSet,
                                      *pipelineLayout, storageBuf, stagingBuf,
                                      bufferElements, fillValue);
  compositor.addScene(std::move(computeScene));

  // Render a few frames
  for (int i = 0; i < 120; ++i) {
    glfwPollEvents();
    bool result = compositor.render().get();
    if (!result) {
      if (windowInfo->swapchain->needRecreation()) {
        int w, h;
        glfwGetFramebufferSize(glfwWin, &w, &h);
        windowInfo->swapchain->recreateSwapchain(w, h);
        compositor.target().recreate();
        --i;
        continue;
      }
      throw std::runtime_error("Compositor render failed");
    }
  }

  // Wait idle and verify buffer contents
  device->waitIdle();
  stagingBuf.invalidate();
  auto *data = static_cast<const uint32_t *>(stagingBuf.map());
  for (uint32_t i = 0; i < bufferElements; ++i) {
    checkMsg(data[i] == fillValue, "testCompositorComputeOnly: mismatch");
  }
  stagingBuf.unmap();

  std::cout << "[PASS] testCompositorComputeOnly\n";
}

// -------------------------------------------------------------------------
// testCompositorComputeGraphics
// -------------------------------------------------------------------------
static void testCompositorComputeGraphics(
    CompositorType &compositor,
    const std::shared_ptr<devices::WindowInfo> &windowInfo,
    std::shared_ptr<devices::Device> device,
    std::shared_ptr<pipelines::Manager> pipelineManager, GLFWwindow *glfwWin) {

  compositor.clearSceneFactories();

  // Vertex buffer (gpu-only, used as both SSBO and vertex buffer)
  constexpr uint32_t vertexCount = 3;
  // constexpr vk::DeviceSize vbSize =      vertexCount *
  // sizeof(Std430Vertex);
  // // reuse from earlier?
  //  We'll define a simpler vertex struct:
  struct Vertex {
    float pos[2];
    float pad[2]; // 8 bytes padding to align color to offset 16
    float color[3];
    float pad2; // padding to make size multiple of 16 (optional)
  };
  static_assert(sizeof(Vertex) == 32);
  constexpr vk::DeviceSize actualVbSize = vertexCount * sizeof(Vertex);

  auto vertexBuffer = device->createBuffer(devices::BufferCreateInfo{
      .size = actualVbSize,
      .usage = vk::BufferUsageFlagBits::eStorageBuffer |
               vk::BufferUsageFlagBits::eVertexBuffer,
      .access = devices::BufferCreateInfo::Access::gpuOnly,
      .debugName = "compute_gfx_vertex"});

  // Compute pipeline (generate_triangle.slang writes to this buffer)
  // Descriptor set for compute
  auto [compSetLayout, compSet, compPool] = createStorageBufferDescriptorSet(
      *device->getDevicePtr(), vertexBuffer.getBuffer(),
      vk::ShaderStageFlagBits::eCompute, actualVbSize);

  vk::PipelineLayoutCreateInfo compLayoutCI{};
  compLayoutCI.setSetLayouts(*compSetLayout);
  vk::raii::PipelineLayout compPipelineLayout{*device->getDevicePtr(),
                                              compLayoutCI};

  static shaders::Shader computeShader{
      .entryPoints = {{"main", vk::ShaderStageFlagBits::eCompute}},
      .sourcePath = "generate_triangle.slang"};
  pipelines::ComputePipelineInfo compInfo{
      .tag = {.shaderTag = &computeShader, .layout = *compPipelineLayout}};
  auto compResult =
      pipelineManager->getOrCreate(compInfo, device->getDevicePtr());
  checkMsg(compResult.has_value(), "compute pipeline failed");
  auto computePipeline = *compResult;

  // Graphics pipeline (graphics_with_ssbo.slang expects vertex buffer)
  // Create vertex input state
  vk::VertexInputBindingDescription bindingDesc{0, sizeof(Vertex),
                                                vk::VertexInputRate::eVertex};
  std::vector<vk::VertexInputAttributeDescription> attrDescs = {
      {0, 0, vk::Format::eR32G32Sfloat, 0},    // position at offset 0
      {1, 0, vk::Format::eR32G32B32Sfloat, 16} // color at offset 16
  };

  // Descriptor set for graphics is not needed if we use vertex buffer,
  // but the shader expects SSBO? Actually graphics_with_ssbo uses vertex
  // input, not SSBO. We'll use a plain vertex buffer binding.
  // So no descriptor set for graphics needed.
  vk::PipelineLayoutCreateInfo gfxLayoutCI{};
  vk::raii::PipelineLayout gfxPipelineLayout{*device->getDevicePtr(),
                                             gfxLayoutCI};

  // Get swapchain format
  auto imgData = windowInfo->swapchain->getSwapchainImageData(0);
  checkMsg(imgData.has_value(), "no swapchain image data");
  vk::Format colorFormat = imgData->format;

  pipelines::DynamicPipelineInfo dynInfo;
  dynInfo.tag.shaderTag = &getShader(); // main.slang or graphics_with_ssbo?
  // We'll use main.slang because it doesn't need external vertex data.
  // But we need to bind vertex buffer; main.slang uses SV_VertexID and
  // generates its own positions. That would ignore our compute data.
  // Better to use a shader that reads vertex buffer. We'll use
  // graphics_with_ssbo.slang, which has vertex inputs.
  static shaders::Shader gfxShader{
      .entryPoints = {{"vertexMain", vk::ShaderStageFlagBits::eVertex},
                      {"fragmentMain", vk::ShaderStageFlagBits::eFragment}},
      .sourcePath = "graphics_with_ssbo.slang"};
  dynInfo.tag.shaderTag = &gfxShader;
  dynInfo.tag.layout = *gfxPipelineLayout;
  dynInfo.vertexBindings = {bindingDesc};
  dynInfo.vertexAttributes = attrDescs;
  dynInfo.inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;
  dynInfo.rasterization.cullMode = vk::CullModeFlagBits::eNone;
  dynInfo.depthStencil.depthTest = vk::False;
  dynInfo.attachments.color = {colorFormat};

  auto gfxResult =
      pipelineManager->getOrCreate(dynInfo, device->getDevicePtr());
  checkMsg(gfxResult.has_value(), "graphics pipeline failed");
  auto graphicsPipeline = *gfxResult;

  ComputeToVertexScene computeScene(device.get(), *computePipeline,
                                    *compPipelineLayout, *compSet,
                                    &vertexBuffer);
  compositor.addScene(computeScene);

  VertexDrawScene drawScene(device.get(), *graphicsPipeline, *gfxPipelineLayout,
                            &vertexBuffer, vertexCount);
  compositor.addScene(drawScene);

  // Render frames
  for (int i = 0; i < 120; ++i) {
    glfwPollEvents();
    bool result = compositor.render().get();
    if (!result) {
      if (windowInfo->swapchain->needRecreation()) {
        int w, h;
        glfwGetFramebufferSize(glfwWin, &w, &h);
        windowInfo->swapchain->recreateSwapchain(w, h);
        compositor.target().recreate();
        --i;
        continue;
      }
      throw std::runtime_error("Compositor render failed");
    }
  }

  // After the render loop:
  device->waitIdle();

  std::cout << "[PASS] testCompositorComputeGraphics\n";
}

// =========================================================================
int main() {
  try {
    // ── GLFW setup ─────────────────────────────────────────────
    glfwSetErrorCallback(glfwError);
    if (!glfwInit())
      throw std::runtime_error("GLFW init failed");
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow *window =
        glfwCreateWindow(800, 600, "Compositor Test", nullptr, nullptr);
    if (!window) {
      glfwTerminate();
      throw std::runtime_error("Window creation failed");
    }
    glfwShowWindow(window);
    glfwPollEvents();

    // ── Vulkan setup ───────────────────────────────────────────
    auto poolManager = std::make_shared<concurrency::pool::Manager>();
    auto instance = std::make_shared<instances::Instance>();

    uint32_t extCount = 0;
    const char **glfwExts = glfwGetRequiredInstanceExtensions(&extCount);
    if (!glfwExts)
      throw std::runtime_error("glfwGetRequiredInstanceExtensions failed");
    for (uint32_t i = 0; i < extCount; ++i)
      instances::Config::instance().addInstanceExtension(glfwExts[i]);

    auto deviceManager = std::make_shared<devices::Manager>(
        instance->getInstancePtr(), poolManager);
    auto entries = deviceManager->getDeviceEntries();
    if (entries.empty())
      throw std::runtime_error("No Vulkan device found");
    auto device = entries.front().device;

    auto shaderManager = std::make_shared<shaders::Manager>();
    auto pipelineManager = std::make_shared<pipelines::Manager>(shaderManager);

    // Create window surface and swapchain
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

    // ── Compositor setup and tests (scoped) ─────────────────────────
    {
      SwapchainRenderTarget renderTarget(windowInfo, device);
      CompositorType compositor(std::move(renderTarget), device);

      testCompositorBasicRender(compositor, windowInfo, device, window);
      testCompositorClearScenes(compositor, windowInfo, device, window);
      testCompositorMultipleScenes(compositor, windowInfo, device,
                                   pipelineManager, window);

      testCompositorComputeOnly(compositor, windowInfo, device, pipelineManager,
                                window);
      testCompositorComputeGraphics(compositor, windowInfo, device,
                                    pipelineManager, window);

      std::cout << "All compositor tests PASSED\n";
    } // compositor and renderTarget destroyed here

    // ── Cleanup ────────────────────────────────────────────────
    device->waitIdle();
    device->removeWindow(windowInfo);
    windowInfo.reset();
    glfwDestroyWindow(window);
    glfwTerminate();

    std::cout << "All compositor tests PASSED\n";
    return EXIT_SUCCESS;
  } catch (const std::exception &e) {
    std::cerr << "FATAL: " << e.what() << '\n';
    return EXIT_FAILURE;
  }
}
