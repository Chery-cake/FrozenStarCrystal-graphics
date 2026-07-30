import graphics;

import std.compat;
import vulkan;

#include <vulkan/vulkan.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cstdlib>

using namespace graphics::vulkan;

// --- Shader definition --------------------------------------------------
static shaders::Shader g_shader{
    .entryPoints = {{"vertexMain", vk::ShaderStageFlagBits::eVertex},
                    {"fragmentMain", vk::ShaderStageFlagBits::eFragment}},
    .sourcePath = "main.slang"};

// --- Helper to convert a VkResult into an exception ---------------------
static void check(VkResult result, const char *msg) {
  if (result != VK_SUCCESS)
    throw std::runtime_error(msg);
}

// --- GLFW error callback -------------------------------------------------
static void glfwError(int code, const char *description) {
  std::cerr << "GLFW error (" << code << "): " << description << '\n';
}

// =========================================================================
int main() {
  try {
    // 1. Initialise GLFW and create a window with no API
    glfwSetErrorCallback(glfwError);
    if (!glfwInit())
      throw std::runtime_error("Failed to initialise GLFW");

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow *window =
        glfwCreateWindow(800, 600, "Triangle Test", nullptr, nullptr);
    if (!window) {
      glfwTerminate();
      throw std::runtime_error("Failed to create GLFW window");
    }

    // Show the window (important on Wayland to get a valid surface size)
    glfwShowWindow(window);
    glfwPollEvents(); // let the window manager process the map event

    // 2. Create Vulkan instance (automatically adds required extensions)
    instances::Instance vulkanInstance;
    auto instancePtr = vulkanInstance.getInstancePtr();

    // 3. Create surface from the GLFW window
    VkSurfaceKHR rawSurface;
    check(glfwCreateWindowSurface(**instancePtr, window, nullptr, &rawSurface),
          "Failed to create GLFW surface");
    auto surface =
        std::make_unique<vk::raii::SurfaceKHR>(*instancePtr, rawSurface);

    // 4. Pick a device (the one with the highest score)
    devices::Manager deviceManager(instancePtr);
    auto entries = deviceManager.getDeviceEntries();
    if (entries.empty())
      throw std::runtime_error("No Vulkan device found");
    auto &bestEntry = entries.front();
    auto device = bestEntry.device;

    // 5. Attach the surface to the device and create a swapchain
    auto windowInfo = std::make_shared<devices::WindowInfo>();
    windowInfo->surface = std::move(surface);
    windowInfo->instance = instancePtr;

    // Build swapchain info with the actual window size
    int width, height;
    glfwGetFramebufferSize(window, &width, &height);

    devices::Swapchain::SwapchainInfo swapInfo;
    swapInfo.extent = vk::Extent2D{static_cast<uint32_t>(width),
                                   static_cast<uint32_t>(height)};

    device->createWindow(windowInfo, 3, swapInfo); // 3 frames in flight

    // 6. Shader & pipeline managers
    auto shaderManager = std::make_shared<shaders::Manager>();
    pipelines::Manager pipelineManager(shaderManager);

    // 7. Obtain swapchain details
    auto swapchainData = windowInfo->swapchain->getSwapchainImageData(0);
    if (!swapchainData)
      throw std::runtime_error("Could not retrieve swapchain image data");
    vk::Format colorFormat = swapchainData->format;

    // 8. Create a simple pipeline layout (empty) for the dynamic pipeline
    vk::PipelineLayoutCreateInfo layoutInfo{};
    vk::raii::PipelineLayout pipelineLayout(*device->getDevicePtr(),
                                            layoutInfo);

    // 9. Build the dynamic pipeline info
    pipelines::DynamicPipelineInfo dynamicInfo;
    dynamicInfo.tag.shaderTag = &g_shader;
    dynamicInfo.tag.layout = *pipelineLayout; // raw handle
    dynamicInfo.tag.flags = {};
    dynamicInfo.topology = vk::PrimitiveTopology::eTriangleList;
    dynamicInfo.colorFormats = {colorFormat};
    dynamicInfo.colorCount = 1;
    dynamicInfo.depthFormat = vk::Format::eUndefined;
    dynamicInfo.stencilFormat = vk::Format::eUndefined;
    dynamicInfo.samples = vk::SampleCountFlagBits::e1;

    // ** Fix back‑face culling and depth test **
    dynamicInfo.cullMode = vk::CullModeFlagBits::eNone;
    dynamicInfo.depthTest = false;

    // 10. Get or create the dynamic pipeline
    auto pipelineResult =
        pipelineManager.getOrCreate(dynamicInfo, device->getDevicePtr());
    if (!pipelineResult)
      throw std::runtime_error("Failed to create dynamic pipeline");
    auto pipeline = *pipelineResult;

    // 11. Create image views for the swapchain images
    auto swapInfoCopy = windowInfo->swapchain->getinfo();
    uint32_t imagesCount = swapInfoCopy.imageCount;
    std::vector<std::unique_ptr<vk::raii::ImageView>> swapchainImageViews;

    for (uint32_t i = 0; i < imagesCount; ++i) {
      auto sd = windowInfo->swapchain->getSwapchainImageData(i);
      if (!sd)
        continue;

      vk::ImageViewCreateInfo viewInfo{swapInfoCopy.imageViewFlags,
                                       sd->image,
                                       swapInfoCopy.imageViewType,
                                       sd->format,
                                       swapInfoCopy.imageViewComponents,
                                       swapInfoCopy.imageViewSubresourceRange};
      swapchainImageViews.push_back(std::make_unique<vk::raii::ImageView>(
          *device->getDevicePtr(), viewInfo));
    }

    // 12. Allocate command buffers and record dynamic rendering commands
    auto &cmdPool = device->getGraphicsPool();
    cmdPool.allocate(imagesCount);
    std::vector<vk::CommandBuffer> commandBuffers;
    for (uint32_t i = 0; i < imagesCount; ++i)
      commandBuffers.push_back(*cmdPool.buffers[i]);

    for (uint32_t i = 0; i < imagesCount; ++i) {
      vk::CommandBuffer cmd = commandBuffers[i];
      auto &sd = swapInfoCopy; // extent, format
      auto extent = vk::Extent2D{sd.extent.width, sd.extent.height};

      // Retrieve the swapchain image handle for this index
      auto imageData = windowInfo->swapchain->getSwapchainImageData(i);
      if (!imageData)
        continue;
      vk::Image swapchainImage = imageData->image;

      vk::CommandBufferBeginInfo beginInfo{};
      cmd.begin(beginInfo);

      // Transition the image to COLOR_ATTACHMENT_OPTIMAL before rendering
      vk::ImageMemoryBarrier preRenderBarrier{
          vk::AccessFlagBits::eNone,                 // srcAccessMask (discard)
          vk::AccessFlagBits::eColorAttachmentWrite, // dstAccessMask
          vk::ImageLayout::eUndefined,               // oldLayout (discard)
          vk::ImageLayout::eColorAttachmentOptimal,  // newLayout
          VK_QUEUE_FAMILY_IGNORED,
          VK_QUEUE_FAMILY_IGNORED,
          swapchainImage,
          vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0,
                                    1}};
      cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
                          vk::PipelineStageFlagBits::eColorAttachmentOutput,
                          vk::DependencyFlags{}, {}, {}, preRenderBarrier);

      // Dynamic rendering attachment
      vk::RenderingAttachmentInfo colorAttachment{
          **swapchainImageViews[i],                 // imageView
          vk::ImageLayout::eColorAttachmentOptimal, // imageLayout
          vk::ResolveModeFlagBits::eNone,
          nullptr,                     // resolveImageView
          vk::ImageLayout::eUndefined, // resolveImageLayout (unused)
          vk::AttachmentLoadOp::eClear,
          vk::AttachmentStoreOp::eStore,
          vk::ClearValue{std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}}};

      vk::RenderingInfo renderingInfo{
          {},                         // flags
          vk::Rect2D{{0, 0}, extent}, // renderArea
          1u,                         // layerCount
          0u,                         // viewMask
          1u,                         // colorAttachmentCount
          &colorAttachment,
          nullptr,
          nullptr // depth, stencil
      };

      cmd.beginRendering(renderingInfo);
      cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);

      vk::Viewport viewport{0.0f,
                            0.0f,
                            static_cast<float>(extent.width),
                            static_cast<float>(extent.height),
                            0.0f,
                            1.0f};
      cmd.setViewport(0, viewport);
      cmd.setScissor(0, vk::Rect2D{{0, 0}, extent});

      cmd.draw(3, 1, 0, 0);
      cmd.endRendering();

      // Transition the swapchain image back to present layout after
      // rendering
      vk::ImageMemoryBarrier postRenderBarrier{
          vk::AccessFlagBits::eColorAttachmentWrite, // srcAccessMask
          vk::AccessFlagBits::eMemoryRead,           // dstAccessMask
          vk::ImageLayout::eColorAttachmentOptimal,  // oldLayout
          vk::ImageLayout::ePresentSrcKHR,           // newLayout
          VK_QUEUE_FAMILY_IGNORED,
          VK_QUEUE_FAMILY_IGNORED,
          swapchainImage,
          vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0,
                                    1}};

      cmd.pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput,
                          vk::PipelineStageFlagBits::eBottomOfPipe, {}, {}, {},
                          postRenderBarrier);

      cmd.end();
    }

    // 13. Main loop
    vk::Queue graphicsQueue = device->getGraphicsQueue();
    while (!glfwWindowShouldClose(window)) {
      glfwPollEvents();

      if (windowInfo->swapchain->needRecreation()) {
        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        windowInfo->swapchain->recreateSwapchain(w, h);
        // (full recreation of image views and command buffers is
        // omitted
        //  for brevity – you would do it in a complete application)
      }

      auto acquireResult = windowInfo->swapchain->acquireNextImage();
      if (!acquireResult) {
        if (acquireResult.error().code ==
            devices::Swapchain::PresentError::Code::outOfDate)
          continue;
        throw std::runtime_error(acquireResult.error().message);
      }
      uint32_t imageIndex = *acquireResult;

      vk::SubmitInfo submit{};
      submit.setCommandBuffers(commandBuffers[imageIndex]);
      auto presentResult = windowInfo->swapchain->submitAndPresent(
          graphicsQueue, std::span<vk::SubmitInfo>(&submit, 1), imageIndex);
      if (!presentResult) {
        std::cerr << "Present error: " << presentResult.error().message << '\n';
      }
    }

    // 14. Cleanup
    device->waitIdle();

    device->removeWindow(windowInfo);
    windowInfo.reset();

    glfwDestroyWindow(window);
    glfwTerminate();

    return EXIT_SUCCESS;

  } catch (const std::exception &e) {
    std::cerr << "Fatal error: " << e.what() << '\n';
    return EXIT_FAILURE;
  }
}
