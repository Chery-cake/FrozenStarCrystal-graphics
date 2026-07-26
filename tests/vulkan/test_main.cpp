
import graphics;

import std.compat;
import vulkan;

#include <vulkan/vulkan.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cstdio>

int main() {
  // ----- 1. Initialise GLFW (Wayland) -----
  // glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
  if (!glfwInit()) {
    std::println(stderr, "glfwInit failed");
    return 1;
  }
  std::println("GLFW platform ID: {}",
               glfwGetPlatform()); // may print odd value, ignore

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  GLFWwindow *window =
      glfwCreateWindow(800, 600, "FSC Graphics Test", nullptr, nullptr);
  if (!window) {
    std::println(stderr, "Window creation failed");
    glfwTerminate();
    return 1;
  }

  {
    // ----- 2. Create Vulkan instance (yours) -----
    graphics::vulkan::instances::Instance inst;
    auto instancePtr = inst.getInstancePtr();

    // ----- 3. Enumerate GPUs, pick best -----
    graphics::vulkan::devices::Manager manager(instancePtr);
    auto devices = manager.getDeviceEntries();
    if (devices.empty()) {
      std::println(stderr, "No Vulkan-capable GPU found.");
      return 1;
    }
    auto &bestEntry = devices.front();
    std::println("Selected GPU: {} (score: {})", bestEntry.info.name,
                 bestEntry.score);

    // ----- 4. Create surface & attach swapchain -----
    VkSurfaceKHR cSurface;
    if (glfwCreateWindowSurface(**instancePtr, window, nullptr, &cSurface) !=
        VK_SUCCESS) {
      std::println(stderr, "Failed to create window surface");
      return 1;
    }
    vk::raii::SurfaceKHR surface(*instancePtr, cSurface);

    auto windowInfo = std::make_shared<graphics::vulkan::devices::WindowInfo>();
    windowInfo->surface =
        std::make_unique<vk::raii::SurfaceKHR>(std::move(surface));
    windowInfo->instance = instancePtr;

    uint32_t framesInFlight = 2;
    graphics::vulkan::devices::Swapchain::SwapchainInfo swapInfo;
    swapInfo.imageCount = 3;
    swapInfo.vsync = true;

    swapInfo.imageUseFlags = vk::ImageUsageFlagBits::eColorAttachment |
                             vk::ImageUsageFlagBits::eTransferDst;

    try {
      bestEntry.device->createWindow(windowInfo, framesInFlight, swapInfo);
    } catch (const std::exception &e) {
      std::println(stderr, "Failed to create window/swapchain: {}", e.what());
      return 1;
    }

    // ----- 5. Show the window with one clear‑to‑blue frame -----
    auto &swapchain = *windowInfo->swapchain;

    auto imageIndex = swapchain.acquireNextImage();
    if (!imageIndex.has_value()) {
      std::println(stderr, "Failed to acquire first image: {}",
                   imageIndex.error().message);
      return 1;
    }
    auto imageData = swapchain.getSwapchainImageData(*imageIndex);
    if (!imageData.has_value()) {
      std::println(stderr, "Failed to get swapchain image data");
      return 1;
    }

    auto &pool = bestEntry.device->getGraphicsPool();
    pool.allocate(1);
    auto &cmd = pool.buffers.front();

    vk::CommandBufferBeginInfo beginInfo{
        vk::CommandBufferUsageFlagBits::eOneTimeSubmit};
    cmd.begin(beginInfo);

    vk::ImageSubresourceRange subresource{vk::ImageAspectFlagBits::eColor, 0, 1,
                                          0, 1};

    // 1. Transition from Undefined to Transfer Dst (valid because usage
    // includes eTransferDst)
    vk::ImageMemoryBarrier barrier{vk::AccessFlagBits::eNone,
                                   vk::AccessFlagBits::eTransferWrite,
                                   vk::ImageLayout::eUndefined,
                                   vk::ImageLayout::eTransferDstOptimal,
                                   vk::QueueFamilyIgnored,
                                   vk::QueueFamilyIgnored,
                                   imageData->image,
                                   subresource};
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
                        vk::PipelineStageFlagBits::eTransfer,
                        vk::DependencyFlags{}, {}, {}, barrier);

    // 2. Clear the image (blue – R=0, G=0, B=1, A=1)
    vk::ClearColorValue clearColor{
        std::array<float, 4>{0.0f, 0.0f, 1.0f, 1.0f}};
    cmd.clearColorImage(barrier.image, vk::ImageLayout::eTransferDstOptimal,
                        clearColor, subresource);

    // 3. Transition from Transfer Dst to Present
    barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
    barrier.dstAccessMask = vk::AccessFlagBits::eMemoryRead;
    barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
    barrier.newLayout = vk::ImageLayout::ePresentSrcKHR;
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                        vk::PipelineStageFlagBits::eBottomOfPipe,
                        vk::DependencyFlags{}, {}, {}, barrier);

    cmd.end();

    // Submit & present
    vk::SubmitInfo submitInfo;
    submitInfo.setCommandBuffers(*cmd);
    std::vector<vk::SubmitInfo> submits = {submitInfo};

    auto presentResult = swapchain.submitAndPresent(
        bestEntry.device->getGraphicsQueue(), submits, *imageIndex);
    if (!presentResult.has_value()) {
      std::println(stderr, "Present failed: {}", presentResult.error().message);
      return 1;
    }

    bestEntry.device->waitIdle();
    pool.reset();

    std::println("Window opened – should be blue. Close it to exit.");

    // ----- 6. Event loop (keep window open) -----
    while (!glfwWindowShouldClose(window)) {
      glfwPollEvents();
      glfwWaitEventsTimeout(0.001);
    }

    windowInfo.reset();
  }

  // Cleanup
  glfwDestroyWindow(window);
  glfwTerminate();

  std::println("Test completed.");
  return 0;
}
