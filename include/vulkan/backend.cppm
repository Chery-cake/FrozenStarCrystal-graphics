module;
#include "FrozenStarCrystal-graphics_export.h"

export module graphics:vulkan_backend;

import std.compat;
import vulkan;
import concurrency;

import graphics.vulkan.instances;
import graphics.vulkan.devices;
import graphics.vulkan.shaders;
import graphics.vulkan.pipelines;
import graphics.vulkan.media;

export namespace graphics {

// RenderContext is the value returned by VulkanBackend::beginFrame().
// It carries per-window frame data needed to record rendering commands.
struct RenderContext {
    bool valid = false; // false → swapchain out-of-date on ≥1 window; retry after resize

    // Per-window frame data
    struct WindowFrame {
        std::shared_ptr<vulkan::devices::WindowInfo> windowInfo;
        uint32_t imageIndex = 0;
        vk::CommandBuffer cmd;   // primary command buffer, already begun
        vk::Image         image; // swapchain image for this frame
        vk::ImageView     view;  // swapchain image view (backed by ownedView)
        vk::Extent2D      extent;
        vk::Format        format;
        // Owns the image view for this frame
        std::unique_ptr<vk::raii::ImageView> ownedView;
    };
    std::vector<WindowFrame> windows;
};

class FROZENSTARCRYSTAL_GRAPHICS_API VulkanBackend {
public:
    VulkanBackend() = default;
    ~VulkanBackend() = default;
    VulkanBackend(const VulkanBackend &) = delete;
    VulkanBackend &operator=(const VulkanBackend &) = delete;
    VulkanBackend(VulkanBackend &&) = delete;
    VulkanBackend &operator=(VulkanBackend &&) = delete;

    // ── ApiCheck-required interface ────────────────────────────────────────

    // Creates the VkInstance, enumerates physical devices, and builds the
    // shader + pipeline managers.
    void initialize();

    // Drains GPU work, destroys pipelines, shaders, devices, and the instance.
    void shutdown();

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

    // Add required instance extensions (e.g. from glfwGetRequiredInstanceExtensions)
    // before calling initialize().
    void addRequiredExtensions(std::span<const char *const> extensions);

    // Register a GLFW (or other) surface as a window.
    // Must be called after initialize() and before the first beginFrame().
    // framesInFlight defaults to 2.
    void createWindow(const std::shared_ptr<vulkan::devices::WindowInfo> &windowInfo,
                      uint32_t framesInFlight = 2);

    // Resize the swapchain for a window (call from your resize callback).
    void resizeWindow(const std::shared_ptr<vulkan::devices::WindowInfo> &windowInfo,
                      uint32_t newWidth, uint32_t newHeight);

    // Remove and destroy a window's swapchain.
    bool removeWindow(const std::shared_ptr<vulkan::devices::WindowInfo> &windowInfo);

    // Hot-reload: recompile a shader and invalidate dependent pipelines.
    void reloadShader(const vulkan::shaders::Shader *tag);

    // ── Accessors ──────────────────────────────────────────────────────────
    [[nodiscard]] vulkan::instances::Instance       &getInstance();
    [[nodiscard]] const vulkan::instances::Instance &getInstance() const;
    [[nodiscard]] vulkan::devices::Manager          &getDeviceManager();
    [[nodiscard]] const vulkan::devices::Manager    &getDeviceManager() const;
    [[nodiscard]] vulkan::shaders::Manager          &getShaderManager();
    [[nodiscard]] const vulkan::shaders::Manager    &getShaderManager() const;
    [[nodiscard]] vulkan::pipelines::Manager        &getPipelineManager();
    [[nodiscard]] const vulkan::pipelines::Manager  &getPipelineManager() const;

private:
    std::unique_ptr<vulkan::instances::Instance>  instance_;
    std::unique_ptr<vulkan::devices::Manager>     deviceManager_;
    std::unique_ptr<vulkan::shaders::Manager>     shaderManager_;
    std::unique_ptr<vulkan::pipelines::Manager>   pipelineManager_;
    std::shared_ptr<concurrency::pool::ThreadPool> gpuPool_;
    std::unique_ptr<concurrency::pool::Manager>   poolManager_;
    concurrency::pool::Pool                        gpuPoolDesc_{.name = "gpuPool"};

    // State kept between beginFrame() and endFrame()
    RenderContext currentFrame_;

    // Per-window command buffer pools (one pool per WindowInfo pointer)
    std::unordered_map<
        vulkan::devices::WindowInfo *,
        vulkan::devices::CommandBufferPool>  frameCmdPools_;
};

// ── VulkanBackend method implementations ─────────────────────────────────────

inline void VulkanBackend::addRequiredExtensions(std::span<const char *const> extensions) {
    for (const char *ext : extensions) {
        vulkan::instances::Config::instance().addInstanceExtension(ext);
    }
}

inline void VulkanBackend::initialize() {
    poolManager_ = std::make_unique<concurrency::pool::Manager>();
    poolManager_->createPool(&gpuPoolDesc_, 2);
    gpuPool_ = poolManager_->getPool(&gpuPoolDesc_).lock();

    instance_        = std::make_unique<vulkan::instances::Instance>();
    deviceManager_   = std::make_unique<vulkan::devices::Manager>(instance_->getInstancePtr(), gpuPool_);
    shaderManager_   = std::make_unique<vulkan::shaders::Manager>();
    // Use a no-op deleter shared_ptr: pipelineManager_ is always destroyed
    // before shaderManager_ (shutdown() / destructor ordering guarantees this).
    pipelineManager_ = std::make_unique<vulkan::pipelines::Manager>(
        std::shared_ptr<vulkan::shaders::Manager>(shaderManager_.get(), [](auto*){})
    );
}

inline void VulkanBackend::shutdown() {
    frameCmdPools_.clear();
    pipelineManager_.reset();
    shaderManager_.reset();
    deviceManager_.reset();
    instance_.reset();
    gpuPool_.reset();
    poolManager_.reset();
}

inline RenderContext VulkanBackend::beginFrame() {
    currentFrame_ = RenderContext{};

    auto devicesWithWindows = deviceManager_->getDevicesWithWindows();

    for (auto &entry : devicesWithWindows) {
        auto windows = entry.device->getWindows();
        for (auto &windowInfo : windows) {
            // Check if swapchain needs recreation
            if (!windowInfo->swapchain || windowInfo->swapchain->needRecreation()) {
                return RenderContext{.valid = false};
            }

            auto acqResult = windowInfo->swapchain->acquireNextImage();
            if (!acqResult) {
                if (acqResult.error().code ==
                    vulkan::devices::Swapchain::PresentError::Code::outOfDate) {
                    return RenderContext{.valid = false};
                }
                return RenderContext{.valid = false};
            }
            uint32_t imageIndex = *acqResult;

            // Get image data for this frame
            auto imageData = windowInfo->swapchain->getSwapchainImageData(imageIndex);
            if (!imageData) {
                return RenderContext{.valid = false};
            }

            // Fetch or create command buffer pool for this window
            auto *key = windowInfo.get();
            auto it = frameCmdPools_.find(key);
            if (it == frameCmdPools_.end()) {
                uint32_t queueFamily = entry.device->getQueueFamilies().graphicsQueue.value_or(0);
                vulkan::devices::CommandPoolCreateInfo cpInfo{
                    .queueFamily = queueFamily,
                    .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
                };
                frameCmdPools_.emplace(
                    key,
                    vulkan::devices::CommandBufferPool{entry.device->getDevicePtr(), cpInfo});
                it = frameCmdPools_.find(key);
                it->second.allocate(1);
            } else {
                it->second.reset();
                it->second.allocate(1);
            }

            auto &cmdPool = it->second;
            vk::CommandBuffer cmd = *cmdPool.buffers.front();

            // Begin command buffer
            vk::CommandBufferBeginInfo beginInfo{
                vk::CommandBufferUsageFlagBits::eOneTimeSubmit};
            cmd.begin(beginInfo);

            vk::Image swapchainImage = imageData->image;
            vk::Extent2D extent      = imageData->extent;
            vk::Format format        = imageData->format;

            // Barrier: UNDEFINED → COLOR_ATTACHMENT_OPTIMAL
            vk::ImageMemoryBarrier preBarrier{
                vk::AccessFlagBits::eNone,
                vk::AccessFlagBits::eColorAttachmentWrite,
                vk::ImageLayout::eUndefined,
                vk::ImageLayout::eColorAttachmentOptimal,
                VK_QUEUE_FAMILY_IGNORED,
                VK_QUEUE_FAMILY_IGNORED,
                swapchainImage,
                vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};
            cmd.pipelineBarrier(
                vk::PipelineStageFlagBits::eTopOfPipe,
                vk::PipelineStageFlagBits::eColorAttachmentOutput,
                {}, {}, {}, preBarrier);

            // Create the swapchain image view for this frame
            vk::ImageViewCreateInfo viewInfo{
                {},
                swapchainImage,
                vk::ImageViewType::e2D,
                format,
                {},
                vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};
            auto ownedView = std::make_unique<vk::raii::ImageView>(
                *entry.device->getDevicePtr(), viewInfo);
            vk::ImageView viewHandle = **ownedView;

            // beginRendering with clear-to-black
            vk::RenderingAttachmentInfo colorAttachment{
                viewHandle,
                vk::ImageLayout::eColorAttachmentOptimal,
                vk::ResolveModeFlagBits::eNone,
                nullptr,
                vk::ImageLayout::eUndefined,
                vk::AttachmentLoadOp::eClear,
                vk::AttachmentStoreOp::eStore,
                vk::ClearValue{std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}}};
            vk::RenderingInfo renderingInfo{
                {},
                vk::Rect2D{{0, 0}, extent},
                1u, 0u, 1u,
                &colorAttachment,
                nullptr, nullptr};
            cmd.beginRendering(renderingInfo);

            currentFrame_.windows.push_back(RenderContext::WindowFrame{
                .windowInfo = windowInfo,
                .imageIndex = imageIndex,
                .cmd        = cmd,
                .image      = swapchainImage,
                .view       = viewHandle,
                .extent     = extent,
                .format     = format,
                .ownedView  = std::move(ownedView),
            });
        }
    }

    currentFrame_.valid = true;
    return currentFrame_;
}

inline void VulkanBackend::endFrame() {
    if (!currentFrame_.valid) {
        return;
    }

    for (auto &wf : currentFrame_.windows) {
        vk::CommandBuffer cmd = wf.cmd;

        cmd.endRendering();

        // Barrier: COLOR_ATTACHMENT_OPTIMAL → PRESENT_SRC_KHR
        vk::ImageMemoryBarrier postBarrier{
            vk::AccessFlagBits::eColorAttachmentWrite,
            vk::AccessFlagBits::eMemoryRead,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageLayout::ePresentSrcKHR,
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            wf.image,
            vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};
        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eColorAttachmentOutput,
            vk::PipelineStageFlagBits::eBottomOfPipe,
            {}, {}, {}, postBarrier);

        cmd.end();

        vk::SubmitInfo submitInfo{};
        submitInfo.setCommandBuffers(cmd);

        vk::Queue graphicsQueue{};
        // Retrieve graphics queue from the device owning this window
        auto devicesWithWindows = deviceManager_->getDevicesWithWindows();
        for (auto &entry : devicesWithWindows) {
            auto windows = entry.device->getWindows();
            for (auto &wi : windows) {
                if (wi.get() == wf.windowInfo.get()) {
                    graphicsQueue = entry.device->getGraphicsQueue();
                    break;
                }
            }
            if (graphicsQueue) break;
        }

        auto presentResult = wf.windowInfo->swapchain->submitAndPresent(
            graphicsQueue,
            std::span<vk::SubmitInfo>(&submitInfo, 1),
            wf.imageIndex);
        if (!presentResult) {
            // Mark for recreation on next frame
            wf.windowInfo->swapchain->markForRecreation();
        }
    }

    currentFrame_ = RenderContext{};
}

inline void VulkanBackend::waitIdle() {
    if (!deviceManager_) return;
    auto entries = deviceManager_->getDeviceEntries();
    for (auto &entry : entries) {
        entry.device->waitIdle();
    }
}

inline void VulkanBackend::createWindow(
    const std::shared_ptr<vulkan::devices::WindowInfo> &windowInfo,
    uint32_t framesInFlight)
{
    auto entries = deviceManager_->getDeviceEntries();
    if (entries.empty()) {
        throw std::runtime_error("[VulkanBackend] No Vulkan device available");
    }
    // Register on the first (highest-scored) device
    entries.front().device->createWindow(windowInfo, framesInFlight);
}

inline void VulkanBackend::resizeWindow(
    const std::shared_ptr<vulkan::devices::WindowInfo> &windowInfo,
    uint32_t newWidth, uint32_t newHeight)
{
    if (windowInfo->swapchain) {
        windowInfo->swapchain->recreateSwapchain(newWidth, newHeight);
    }
}

inline bool VulkanBackend::removeWindow(
    const std::shared_ptr<vulkan::devices::WindowInfo> &windowInfo)
{
    // Remove from cmd pool map
    frameCmdPools_.erase(windowInfo.get());

    auto entries = deviceManager_->getDeviceEntries();
    for (auto &entry : entries) {
        if (entry.device->removeWindow(windowInfo)) {
            return true;
        }
    }
    return false;
}

inline void VulkanBackend::reloadShader(const vulkan::shaders::Shader *tag) {
    shaderManager_->reloadShader(tag);
    pipelineManager_->invalidateShader(tag);
}

inline vulkan::instances::Instance &VulkanBackend::getInstance() {
    return *instance_;
}
inline const vulkan::instances::Instance &VulkanBackend::getInstance() const {
    return *instance_;
}
inline vulkan::devices::Manager &VulkanBackend::getDeviceManager() {
    return *deviceManager_;
}
inline const vulkan::devices::Manager &VulkanBackend::getDeviceManager() const {
    return *deviceManager_;
}
inline vulkan::shaders::Manager &VulkanBackend::getShaderManager() {
    return *shaderManager_;
}
inline const vulkan::shaders::Manager &VulkanBackend::getShaderManager() const {
    return *shaderManager_;
}
inline vulkan::pipelines::Manager &VulkanBackend::getPipelineManager() {
    return *pipelineManager_;
}
inline const vulkan::pipelines::Manager &VulkanBackend::getPipelineManager() const {
    return *pipelineManager_;
}

} // namespace graphics
