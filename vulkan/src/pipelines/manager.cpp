module;

module graphics.vulkan.pipelines;

import std.compat;
import vulkan;

import graphics.vulkan.shaders;

namespace graphics::vulkan::pipelines {

namespace {

PipelineError shaderToPipelineError(const shaders::ShaderError &e) {
  return PipelineError{.code = PipelineError::Code::shaderLoadFailed,
                       .message = e.message};
}

} // namespace

Manager::Manager(const std::shared_ptr<shaders::Manager> &shaderManager)
    : shaderManager_(shaderManager) {}

Manager::~Manager() {
  {
    std::unique_lock lock(cacheMtx_);
    cache_.clear();
  }

  shaderManager_.reset();

  std::cout << "[Manager] Cleared\n";
}

std::expected<std::shared_ptr<vk::raii::Pipeline>, PipelineError>
Manager::buildDynamic(const DynamicPipelineInfo &info,
                      const std::shared_ptr<vk::raii::Device> &device) {
  // 1. Load (or retrieve) the compiled shader module
  const auto *shaderDef = info.tag.shaderTag;
  auto loadResult = shaderManager_->loadShader(shaderDef, device);
  if (!loadResult) {
    return std::unexpected<PipelineError>(
        shaderToPipelineError(loadResult.error()));
  }
  std::shared_ptr<vk::raii::ShaderModule> shaderModule = std::move(*loadResult);

  // 2. Build shader stage create infos
  std::vector<vk::PipelineShaderStageCreateInfo> stages;

  std::ranges::for_each(shaderDef->entryPoints,
                        [&stages, &shaderModule](const auto &ep) {
                          vk::PipelineShaderStageCreateInfo stageInfo{};
                          stageInfo.setStage(ep.type)
                              .setModule(*shaderModule)
                              .setPName(ep.entry.c_str());
                          stages.push_back(stageInfo);
                        });

  // 3. Vertex input state
  vk::PipelineVertexInputStateCreateInfo vertexInput{};
  vertexInput.setVertexBindingDescriptions(info.vertexBindings)
      .setVertexAttributeDescriptions(info.vertexAttributes);

  // 4. Input assembly
  vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
  inputAssembly.setTopology(info.inputAssembly.topology)
      .setPrimitiveRestartEnable(info.inputAssembly.primitiveRestart);

  // 5. Viewport & scissor (dynamic)
  vk::PipelineViewportStateCreateInfo viewportState{};
  viewportState.setViewportCount(1).setScissorCount(1);

  // 6. Rasterization
  vk::PipelineRasterizationStateCreateInfo rasterizer{};
  rasterizer.setPolygonMode(info.rasterization.polygonMode)
      .setCullMode(info.rasterization.cullMode)
      .setFrontFace(info.rasterization.frontFace)
      .setDepthClampEnable(info.rasterization.depthClamp)
      .setDepthBiasEnable(info.rasterization.depthBias)
      .setLineWidth(info.rasterization.lineWidth);

  // 7. Multisample
  vk::PipelineMultisampleStateCreateInfo multisample{};
  multisample.setRasterizationSamples(info.multisample.samples);

  // 8. Depth / stencil
  vk::PipelineDepthStencilStateCreateInfo depthStencil{};
  depthStencil.setDepthTestEnable(info.depthStencil.depthTest)
      .setDepthWriteEnable(info.depthStencil.depthWrite)
      .setDepthCompareOp(info.depthStencil.depthCompare)
      .setStencilTestEnable(info.depthStencil.stencilTest);

  // 9. Colour blend
  std::vector<vk::PipelineColorBlendAttachmentState> blendAttachments;
  if (info.colorBlendAttachments.empty()) {
    // Default: no blending, write all channels
    vk::PipelineColorBlendAttachmentState defaultBlend{};
    defaultBlend.setColorWriteMask(
        vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
        vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);
    blendAttachments.resize(info.attachments.color.size(), defaultBlend);
  } else {
    blendAttachments = info.colorBlendAttachments;
  }

  vk::PipelineColorBlendStateCreateInfo colorBlend{};
  colorBlend.setAttachments(blendAttachments);

  // 10. Dynamic states (viewport + scissor always, plus extras)
  std::vector<vk::DynamicState> dynamicStates = {vk::DynamicState::eViewport,
                                                 vk::DynamicState::eScissor};
  dynamicStates.insert(dynamicStates.end(), info.extraDynamicStates.begin(),
                       info.extraDynamicStates.end());
  vk::PipelineDynamicStateCreateInfo dynamicStateInfo{};
  dynamicStateInfo.setDynamicStates(dynamicStates);

  // 11. Dynamic rendering create info (pNext chain)
  vk::PipelineRenderingCreateInfo renderingInfo{};
  renderingInfo.setColorAttachmentFormats(info.attachments.color)
      .setDepthAttachmentFormat(info.attachments.depth)
      .setStencilAttachmentFormat(info.attachments.stencil);

  // 12. Assemble the pipeline
  vk::GraphicsPipelineCreateInfo pipelineCreateInfo{};
  pipelineCreateInfo.setStages(stages)
      .setPVertexInputState(&vertexInput)
      .setPInputAssemblyState(&inputAssembly)
      .setPViewportState(&viewportState)
      .setPRasterizationState(&rasterizer)
      .setPMultisampleState(&multisample)
      .setPDepthStencilState(&depthStencil)
      .setPColorBlendState(&colorBlend)
      .setPDynamicState(&dynamicStateInfo)
      .setLayout(info.tag.layout)
      .setFlags(info.tag.flags)
      .setPNext(&renderingInfo); // dynamic rendering

  try {
    auto pipeline = std::make_shared<vk::raii::Pipeline>(*device, nullptr,
                                                         pipelineCreateInfo);
    return pipeline;
  } catch (const vk::SystemError &err) {
    return std::unexpected<PipelineError>(
        {.code = PipelineError::Code::creationFailed, .message = err.what()});
  }
}

std::expected<std::shared_ptr<vk::raii::Pipeline>, PipelineError>
Manager::buildStatic(const StaticPipelineInfo &info,
                     const std::shared_ptr<vk::raii::Device> &device) {
  // 1. Load shader module
  const auto *shaderDef = info.tag.shaderTag;
  auto loadResult = shaderManager_->loadShader(shaderDef, device);
  if (!loadResult) {
    return std::unexpected<PipelineError>(
        shaderToPipelineError(loadResult.error()));
  }
  std::shared_ptr<vk::raii::ShaderModule> shaderModule = std::move(*loadResult);

  // 2. Shader stages
  std::vector<vk::PipelineShaderStageCreateInfo> stages;
  std::ranges::for_each(shaderDef->entryPoints,
                        [&stages, &shaderModule](const auto &ep) {
                          vk::PipelineShaderStageCreateInfo stageInfo{};
                          stageInfo.setStage(ep.type)
                              .setModule(*shaderModule)
                              .setPName(ep.entry.c_str());
                          stages.push_back(stageInfo);
                        });

  // 3. Vertex input
  vk::PipelineVertexInputStateCreateInfo vertexInput{};
  vertexInput.setVertexBindingDescriptions(info.vertexBindings)
      .setVertexAttributeDescriptions(info.vertexAttributes);

  // 4. Input assembly
  vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
  inputAssembly.setTopology(info.inputAssembly.topology)
      .setPrimitiveRestartEnable(info.inputAssembly.primitiveRestart);

  // 5. Viewport (static, but we still provide a count)
  vk::PipelineViewportStateCreateInfo viewportState{};
  viewportState.setViewportCount(1).setScissorCount(1);

  // 6. Rasterization, depth/stencil, multisample
  vk::PipelineRasterizationStateCreateInfo rasterizer{};
  rasterizer.setPolygonMode(info.rasterization.polygonMode)
      .setCullMode(info.rasterization.cullMode)
      .setFrontFace(info.rasterization.frontFace)
      .setDepthClampEnable(info.rasterization.depthClamp)
      .setDepthBiasEnable(info.rasterization.depthBias)
      .setLineWidth(info.rasterization.lineWidth);

  vk::PipelineDepthStencilStateCreateInfo depthStencil{};
  depthStencil.setDepthTestEnable(info.depthStencil.depthTest)
      .setDepthWriteEnable(info.depthStencil.depthWrite)
      .setDepthCompareOp(info.depthStencil.depthCompare)
      .setStencilTestEnable(info.depthStencil.stencilTest);

  vk::PipelineMultisampleStateCreateInfo multisample{};
  multisample.setRasterizationSamples(info.multisample.samples);

  // 7. Colour blend
  vk::PipelineColorBlendStateCreateInfo colorBlend = info.colorBlend;
  if (info.colorBlendAttachments.empty()) {
    vk::PipelineColorBlendAttachmentState defaultBlend{};
    defaultBlend.setColorWriteMask(
        vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
        vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);
    colorBlend.setAttachments(defaultBlend);
  } else {
    colorBlend.setAttachments(info.colorBlendAttachments);
  }

  // 8. Dynamic states
  vk::PipelineDynamicStateCreateInfo dynamicState{};
  dynamicState.setDynamicStates(info.dynamicStates);

  // 9. Pipeline creation
  vk::GraphicsPipelineCreateInfo pipelineCreateInfo{};
  pipelineCreateInfo.setStages(stages)
      .setPVertexInputState(&vertexInput)
      .setPInputAssemblyState(&inputAssembly)
      .setPViewportState(&viewportState)
      .setPRasterizationState(&rasterizer)
      .setPMultisampleState(&multisample)
      .setPDepthStencilState(&depthStencil)
      .setPColorBlendState(&colorBlend)
      .setPDynamicState(&dynamicState)
      .setLayout(info.tag.layout)
      .setRenderPass(info.renderPass)
      .setSubpass(info.subpass)
      .setFlags(info.tag.flags);

  try {
    auto pipeline = std::make_shared<vk::raii::Pipeline>(*device, nullptr,
                                                         pipelineCreateInfo);
    return pipeline;
  } catch (const vk::SystemError &err) {
    return std::unexpected<PipelineError>(
        {.code = PipelineError::Code::creationFailed, .message = err.what()});
  }
}

std::expected<std::shared_ptr<vk::raii::Pipeline>, PipelineError>
Manager::getOrCreate(const DynamicPipelineInfo &info,
                     const std::shared_ptr<vk::raii::Device> &device) {
  // 1. Optimistic read
  {
    std::shared_lock rlock(cacheMtx_);
    auto devIt = cache_.find(device);
    if (devIt != cache_.end()) {
      std::shared_lock lock(devIt->second.entryMtx);
      auto pIt = devIt->second.dynamicPipelines.find(info);
      if (pIt != devIt->second.dynamicPipelines.end()) {
        return pIt->second;
      }
    }
  }

  // 2. Build outside any lock (can be slow)
  auto pipeline = buildDynamic(info, device);
  if (!pipeline) {
    return std::unexpected(pipeline.error());
  }

  // 3. Write lock to insert
  DeviceEntry *entry;
  {
    std::unique_lock wlock(cacheMtx_);
    auto devIt = cache_.find(device);
    if (devIt != cache_.end()) {
      entry = &devIt->second;

    } else {
      entry = &cache_[device];
    }
  }

  std::unique_lock lock(entry->entryMtx);
  auto [it, inserted] = entry->dynamicPipelines.emplace(info.tag, *pipeline);
  return it->second;
}

std::expected<std::shared_ptr<vk::raii::Pipeline>, PipelineError>
Manager::getOrCreate(const StaticPipelineInfo &info,
                     const std::shared_ptr<vk::raii::Device> &device) {
  // 1. Optimistic read
  {
    std::shared_lock rlock(cacheMtx_);
    auto devIt = cache_.find(device);
    if (devIt != cache_.end()) {
      std::shared_lock lock(devIt->second.entryMtx);
      auto pIt = devIt->second.staticPipelines.find(info);
      if (pIt != devIt->second.staticPipelines.end()) {
        return pIt->second;
      }
    }
  }

  // 2. Build outside any lock (can be slow)
  auto pipeline = buildStatic(info, device);
  if (!pipeline) {
    return std::unexpected(pipeline.error());
  }

  // 3. Write lock to insert
  DeviceEntry *entry;
  {
    std::unique_lock wlock(cacheMtx_);
    auto devIt = cache_.find(device);
    if (devIt != cache_.end()) {
      entry = &devIt->second;

    } else {
      entry = &cache_[device];
    }
  }

  std::unique_lock lock(entry->entryMtx);
  auto [it, inserted] = entry->staticPipelines.emplace(info.tag, *pipeline);
  return it->second;
}

void Manager::invalidateShader(const shaders::Shader *tag) {
  std::shared_lock lock(cacheMtx_);

  std::ranges::for_each(cache_, [&tag](auto &pair) {
    DeviceEntry &cache = pair.second;

    std::erase_if(cache.dynamicPipelines, [&tag](const auto &pair) {
      return pair.first.tag.shaderTag == tag;
    });
    std::erase_if(cache.staticPipelines, [&tag](const auto &pair) {
      return pair.first.tag.shaderTag == tag;
    });
  });
}

void Manager::invalidateDevice(
    const std::shared_ptr<vk::raii::Device> &device) {
  std::unique_lock lock(cacheMtx_);

  std::erase_if(cache_,
                [&device](const auto &pair) { return pair.first == device; });
}

} // namespace graphics::vulkan::pipelines
