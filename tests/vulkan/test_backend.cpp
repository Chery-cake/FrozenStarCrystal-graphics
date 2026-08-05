import graphics;
import std.compat;
import vulkan;

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

// --- GLFW error callback -------------------------------------------------
static void glfwError(int code, const char *desc) {
  std::cerr << "GLFW error (" << code << "): " << desc << '\n';
}

// =========================================================================
int main() {
  try {
    // ── 1. GLFW setup ──────────────────────────────────────────────────
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

    // ── 2. Add GLFW surface extensions before initialize ───────────────
    auto poolManager = std::make_shared<concurrency::pool::Manager>();
    poolManager->createPool(&gpuPoolDesc, 2);

    graphics::GraphicsBackend backend{poolManager};

    uint32_t extCount = 0;
    const char **glfwExts = glfwGetRequiredInstanceExtensions(&extCount);
    if (!glfwExts)
      throw std::runtime_error(
          "glfwGetRequiredInstanceExtensions returned null");
    backend.addRequiredExtensions({glfwExts, extCount});

    // ── 3. Backend lifecycle ───────────────────────────────────────────
    // backend.initialize();

    // ── 4. Register the GLFW surface ───────────────────────────────────
    auto instancePtr = backend.getInstance().getInstancePtr();
    VkSurfaceKHR rawSurface;
    if (glfwCreateWindowSurface(**instancePtr, window, nullptr, &rawSurface) !=
        VK_SUCCESS)
      throw std::runtime_error("Surface creation failed");

    auto windowInfo = std::make_shared<devices::WindowInfo>();
    windowInfo->surface =
        std::make_unique<vk::raii::SurfaceKHR>(*instancePtr, rawSurface);
    windowInfo->instance = instancePtr;

    // Get actual window size
    int width, height;
    glfwGetFramebufferSize(window, &width, &height);
    vk::Extent2D extent{static_cast<uint32_t>(width),
                        static_cast<uint32_t>(height)};

    backend.createWindow(windowInfo, 2, extent);

    // ── 5. Main loop ───────────────────────────────────────────────────
    int frameCount = 0;
    while (!glfwWindowShouldClose(window) && frameCount < 120) {
      glfwPollEvents();

      int w, h;
      glfwGetFramebufferSize(window, &w, &h);

      // Resize handling
      if (windowInfo->swapchain && windowInfo->swapchain->needRecreation()) {
        backend.resizeWindow(windowInfo, static_cast<uint32_t>(w),
                             static_cast<uint32_t>(h));
        continue;
      }

      // beginFrame returns a RenderContext
      auto ctx = backend.beginFrame();
      bool anyValid = false;
      for (auto &wf : ctx.windows) {
        if (!wf.valid) {
          backend.resizeWindow(wf.windowInfo, static_cast<uint32_t>(w),
                               static_cast<uint32_t>(h));
        } else {
          anyValid = true;
        }
      }
      if (!anyValid)
        continue;

      // User rendering code would go here — ctx.windows[i].cmd is ready
      // for draw calls. For this test we just let the backend clear to
      // black.

      backend.endFrame();
      ++frameCount;
    }

    // ── 6. Assertions ──────────────────────────────────────────────────
    assert(frameCount > 0 && "No frames were rendered");

    // ── 7. Cleanup ─────────────────────────────────────────────────────
    backend.waitIdle();
    backend.removeWindow(windowInfo);
    windowInfo.reset();

    // backend.shutdown();

    glfwDestroyWindow(window);
    glfwTerminate();

    std::cout << "backend test PASSED (" << frameCount << " frames)\n";
    return EXIT_SUCCESS;

  } catch (const std::exception &e) {
    std::cerr << "FATAL: " << e.what() << '\n';
    return EXIT_FAILURE;
  }
}
