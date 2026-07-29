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

    // 6. Shader compilation
    shaders::Manager shaderManager;
    auto vertexModule =
        shaderManager.loadShader(&g_shader, device->getDevicePtr());
    auto fragmentModule =
        shaderManager.loadShader(&g_shader, device->getDevicePtr());
    if (!vertexModule || !fragmentModule)
      throw std::runtime_error("Shader compilation / loading failed");

    // 7. Obtain swapchain details
    auto swapchainData = windowInfo->swapchain->getSwapchainImageData(0);
    if (!swapchainData)
      throw std::runtime_error("Could not retrieve swapchain image data");
    vk::Format colorFormat = swapchainData->format;

    // 8. Render pass – one color attachment
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
        {}, vk::PipelineBindPoint::eGraphics, 0, nullptr, 1, &colorRef};
    vk::RenderPassCreateInfo renderPassInfo{
        {}, 1, &colorAttachment, 1, &subpass};
    auto renderPass =
        vk::raii::RenderPass(*device->getDevicePtr(), renderPassInfo);

    // 9. Framebuffers – use public API to avoid accessing private frames_
    auto swapInfoCopy = windowInfo->swapchain->getinfo();
    uint32_t imagesCount = swapInfoCopy.imageCount;
    std::vector<std::unique_ptr<vk::raii::ImageView>>
        swapchainImageViews; // keep alive
    std::vector<vk::raii::Framebuffer> framebuffers;

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
      auto imageView = std::make_unique<vk::raii::ImageView>(
          *device->getDevicePtr(), viewInfo);

      vk::ImageView attachments[] = {**imageView};
      vk::FramebufferCreateInfo fbInfo{
          {}, *renderPass, 1, attachments, sd->extent.width, sd->extent.height,
          1};
      framebuffers.emplace_back(*device->getDevicePtr(), fbInfo);
      swapchainImageViews.push_back(std::move(imageView));
    }

    // 10. Pipeline layout (empty for this simple shader)
    vk::PipelineLayoutCreateInfo layoutInfo{};
    auto pipelineLayout =
        vk::raii::PipelineLayout(*device->getDevicePtr(), layoutInfo);

    // 11. Graphics pipeline
    vk::PipelineShaderStageCreateInfo vertexStage{
        {}, vk::ShaderStageFlagBits::eVertex, **vertexModule, "vertexMain"};
    vk::PipelineShaderStageCreateInfo fragmentStage{
        {},
        vk::ShaderStageFlagBits::eFragment,
        **fragmentModule,
        "fragmentMain"};
    std::vector<vk::PipelineShaderStageCreateInfo> stages = {vertexStage,
                                                             fragmentStage};

    vk::PipelineVertexInputStateCreateInfo vertexInput{
        {}, 0, nullptr, 0, nullptr};
    vk::PipelineInputAssemblyStateCreateInfo inputAssembly{
        {}, vk::PrimitiveTopology::eTriangleList, false};
    vk::PipelineViewportStateCreateInfo viewportState{
        {}, 1, nullptr, 1, nullptr};
    vk::PipelineRasterizationStateCreateInfo rasterizer{
        {},
        false,
        false,
        vk::PolygonMode::eFill,
        vk::CullModeFlagBits::eNone,
        vk::FrontFace::eCounterClockwise,
        false,
        0.0f,
        0.0f,
        0.0f,
        1.0f};
    vk::PipelineMultisampleStateCreateInfo multisample{
        {}, vk::SampleCountFlagBits::e1, false};
    vk::PipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask =
        vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
        vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
    vk::PipelineColorBlendStateCreateInfo colorBlend{
        {}, false, vk::LogicOp::eCopy, 1, &blendAttachment};
    vk::DynamicState dynamicStates[] = {vk::DynamicState::eViewport,
                                        vk::DynamicState::eScissor};
    vk::PipelineDynamicStateCreateInfo dynamicState{{}, 2, dynamicStates};

    vk::GraphicsPipelineCreateInfo pipelineInfo{{},
                                                stages,
                                                &vertexInput,
                                                &inputAssembly,
                                                nullptr,
                                                &viewportState,
                                                &rasterizer,
                                                &multisample,
                                                nullptr,
                                                &colorBlend,
                                                &dynamicState,
                                                *pipelineLayout,
                                                *renderPass,
                                                0};
    auto pipeline =
        vk::raii::Pipeline(*device->getDevicePtr(), nullptr, pipelineInfo);

    // 12. Command buffers – one per framebuffer
    auto &cmdPool = device->getGraphicsPool();
    cmdPool.allocate(imagesCount);
    std::vector<vk::CommandBuffer> commandBuffers;
    for (uint32_t i = 0; i < imagesCount; ++i)
      commandBuffers.push_back(*cmdPool.buffers[i]);

    for (uint32_t i = 0; i < imagesCount; ++i) {
      vk::CommandBuffer cmd = commandBuffers[i];
      vk::CommandBufferBeginInfo beginInfo{};
      cmd.begin(beginInfo);

      vk::ClearValue clearColor{std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}};
      vk::RenderPassBeginInfo rpInfo{*renderPass, *framebuffers[i],
                                     vk::Rect2D{{0, 0}, swapchainData->extent},
                                     1, &clearColor};
      cmd.beginRenderPass(rpInfo, vk::SubpassContents::eInline);
      cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);

      vk::Viewport viewport{0.0f,
                            0.0f,
                            static_cast<float>(swapchainData->extent.width),
                            static_cast<float>(swapchainData->extent.height),
                            0.0f,
                            1.0f};
      cmd.setViewport(0, viewport);
      cmd.setScissor(0, vk::Rect2D{{0, 0}, swapchainData->extent});

      cmd.draw(3, 1, 0, 0);
      cmd.endRenderPass();
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
        // (recreation of framebuffers/command buffers is omitted
        //  for brevity – you would do it in a full application)
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

    // 14. Cleanup – destroy Vulkan resources that depend on the window
    //     BEFORE the window is destroyed
    device->waitIdle();

    // Remove the window (destroys swapchain & surface)
    device->removeWindow(windowInfo);
    windowInfo.reset(); // make sure shared_ptr releases

    // Now it's safe to destroy the GLFW window
    glfwDestroyWindow(window);
    glfwTerminate();

    // deviceManager and other locals will now clean up safely
    return EXIT_SUCCESS;

  } catch (const std::exception &e) {
    std::cerr << "Fatal error: " << e.what() << '\n';
    return EXIT_FAILURE;
  }
}
