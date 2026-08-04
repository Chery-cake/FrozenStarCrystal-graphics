module;

#include "FrozenStarCrystal-graphics_export.h"

export module graphics:vulkan.backend;

import std.compat;
import vulkan;
import concurrency;

import graphics.vulkan;

export namespace graphics {

inline constexpr concurrency::pool::Pool gpuPoolDesc{.name = "gpuPool"};

// RenderContext is the value returned by VulkanBackend::beginFrame().
// It carries per-window frame data needed to record rendering commands.
struct FROZENSTARCRYSTAL_GRAPHICS_API RenderContext {

  // Per-window frame data
  struct WindowFrame {
    std::shared_ptr<vulkan::devices::Device> device;
    std::shared_ptr<vulkan::devices::WindowInfo> windowInfo;
    uint32_t imageIndex = 0;
    vk::CommandBuffer cmd; // primary command buffer, already begun
    vk::Image image;       // swapchain image for this frame
    vk::ImageView view;    // swapchain image view (backed by ownedView)
    vk::Extent2D extent;
    vk::Format format;
    // Owns the image view for this frame
    std::shared_ptr<vk::raii::ImageView> ownedView;

    bool valid = false; // false → swapchain out-of-date; retry after resize
  };
  std::vector<WindowFrame> windows;
};

struct FROZENSTARCRYSTAL_GRAPHICS_API WindowCmdData {
  vulkan::devices::CommandBufferPool pool;
  uint32_t framesInFlight = 0;
};

class FROZENSTARCRYSTAL_GRAPHICS_API Backend {
private:
  std::unique_ptr<vulkan::instances::Instance> instance_;
  std::unique_ptr<vulkan::devices::Manager> deviceManager_;
  std::unique_ptr<vulkan::shaders::Manager> shaderManager_;
  std::unique_ptr<vulkan::pipelines::Manager> pipelineManager_;
  std::shared_ptr<concurrency::pool::ThreadPool> gpuPool_;
  std::shared_ptr<concurrency::pool::Manager> poolManager_;

  // State kept between beginFrame() and endFrame()
  RenderContext currentFrame_;

  // Per-window command buffer pools (one pool per WindowInfo pointer)
  std::unordered_map<std::shared_ptr<vulkan::devices::WindowInfo>,
                     WindowCmdData>
      frameCmdPools_;

public:
  Backend(const std::shared_ptr<concurrency::pool::Manager> &poolManager);
  ~Backend();

  Backend(const Backend &) = delete;
  Backend &operator=(const Backend &) = delete;
  Backend(Backend &&) = delete;
  Backend &operator=(Backend &&) = delete;

  // ── ApiCheck-required interface ────────────────────────────────────────

  // Acquires the next swapchain image on every registered window.
  // Allocates/resets a primary command buffer per window and records:
  //   • pipeline barrier UNDEFINED → COLOR_ATTACHMENT_OPTIMAL
  //   • vkCmdBeginRendering with a clear-to-black colour attachment
  // Returns a RenderContext with valid=false if any swapchain is out-of-date
  // (caller must resize and call beginFrame again).
  [[nodiscard]] RenderContext beginFrame();

  // For each window in the last RenderContext:
  //   • vkCmdEndRendering
  //   • pipeline barrier COLOR_ATTACHMENT_OPTIMAL → PRESENT_SRC_KHR
  //   • end command buffer
  //   • submitAndPresent via the window's swapchain
  void endFrame();

  // Blocks until all devices report idle.
  void waitIdle();

  // ── Vulkan-specific extras (NOT part of ApiCheck) ──────────────────────

  // Add required instance extensions (e.g. from
  // glfwGetRequiredInstanceExtensions) before calling initialize().
  static void addRequiredExtensions(std::span<const char *const> extensions);

  // Register a GLFW (or other) surface as a window.
  // Must be called after initialize() and before the first beginFrame().
  // framesInFlight defaults to 2.
  void
  createWindow(const std::shared_ptr<vulkan::devices::WindowInfo> &windowInfo,
               uint32_t framesInFlight, vk::Extent2D extent);
  void
  createWindow(const std::shared_ptr<vulkan::devices::Device> &device,
               const std::shared_ptr<vulkan::devices::WindowInfo> &windowInfo,
               uint32_t framesInFlight, vk::Extent2D extent);

  // Resize the swapchain for a window (call from your resize callback).
  static void
  resizeWindow(const std::shared_ptr<vulkan::devices::WindowInfo> &windowInfo,
               uint32_t newWidth, uint32_t newHeight);

  // Remove and destroy a window's swapchain.
  bool
  removeWindow(const std::shared_ptr<vulkan::devices::WindowInfo> &windowInfo);

  // Hot-reload: recompile a shader and invalidate dependent pipelines.
  void reloadShader(const vulkan::shaders::Shader *tag);

  // ── Accessors ──────────────────────────────────────────────────────────
  [[nodiscard]] vulkan::instances::Instance &getInstance() {
    return *instance_;
  }
  [[nodiscard]] const vulkan::instances::Instance &getInstance() const {
    return *instance_;
  }
  [[nodiscard]] vulkan::devices::Manager &getDeviceManager() {
    return *deviceManager_;
  }
  [[nodiscard]] const vulkan::devices::Manager &getDeviceManager() const {
    return *deviceManager_;
  }
  [[nodiscard]] vulkan::shaders::Manager &getShaderManager() {
    return *shaderManager_;
  }
  [[nodiscard]] const vulkan::shaders::Manager &getShaderManager() const {
    return *shaderManager_;
  }
  [[nodiscard]] vulkan::pipelines::Manager &getPipelineManager() {
    return *pipelineManager_;
  }
  [[nodiscard]] const vulkan::pipelines::Manager &getPipelineManager() const {
    return *pipelineManager_;
  }
};

} // namespace graphics
