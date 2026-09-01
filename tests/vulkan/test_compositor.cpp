import vulkan_helper;

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
