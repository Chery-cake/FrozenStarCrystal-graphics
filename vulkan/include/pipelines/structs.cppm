module;

#include "FrozenStarCrystal-graphics_export.h"

export module graphics.vulkan.pipelines:structs;

import std.compat;
import vulkan;
import graphics.vulkan.shaders;

export namespace graphics::vulkan::pipelines {

enum class PipelineKind : uint8_t { Dynamic, Static };

template <PipelineKind Kind> struct FROZENSTARCRYSTAL_GRAPHICS_API PipelineTag {
  static constexpr PipelineKind kind = Kind;

  const shaders::Shader *shaderTag = nullptr;
  vk::PipelineLayout layout = {};
  vk::PipelineCreateFlags flags;

  constexpr auto operator<=>(const PipelineTag &) const noexcept = default;
};

using DynamicPipeline = PipelineTag<PipelineKind::Dynamic>;
using StaticPipeline = PipelineTag<PipelineKind::Static>;

// Dynamic pipeline (dynamic rendering, no render pass)

struct FROZENSTARCRYSTAL_GRAPHICS_API DynamicPipelineInfo {
  DynamicPipeline tag;

  // Topology
  vk::PrimitiveTopology topology = vk::PrimitiveTopology::eTriangleList;
  vk::Bool32 primitiveRestart = vk::False;

  // Rasterizer
  vk::PolygonMode polygonMode = vk::PolygonMode::eFill;
  vk::CullModeFlags cullMode = vk::CullModeFlagBits::eBack;
  vk::FrontFace frontFace = vk::FrontFace::eCounterClockwise;
  vk::Bool32 depthClamp = vk::False;
  vk::Bool32 depthBias = vk::False;
  float lineWidth = 1.0F;

  // Depth / stencil
  vk::Bool32 depthTest = vk::True;
  vk::Bool32 depthWrite = vk::True;
  vk::CompareOp depthCompareOp = vk::CompareOp::eLess;
  vk::Bool32 stencilTest = vk::False;

  // Multisample
  vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1;

  // Dynamic rendering attachment formats
  vk::Format depthFormat = vk::Format::eUndefined;
  vk::Format stencilFormat = vk::Format::eUndefined;
  std::vector<vk::Format> colorFormats;
  uint32_t colorCount = 0;

  // Vertex input
  std::vector<vk::VertexInputBindingDescription> vertexBindings;
  std::vector<vk::VertexInputAttributeDescription> vertexAttributes;

  // Per-attachment blend state
  std::vector<vk::PipelineColorBlendAttachmentState> colorBlendAttachments;

  // Extra dynamic states beyond Manager defaults
  std::vector<vk::DynamicState> extraDynamicStates;
};

//  Static pipeline (classic render pass)

struct FROZENSTARCRYSTAL_GRAPHICS_API StaticPipelineInfo {
  StaticPipeline tag;

  // Vertex input
  std::vector<vk::VertexInputBindingDescription> vertexBindings;
  std::vector<vk::VertexInputAttributeDescription> vertexAttributes;

  vk::PrimitiveTopology topology = vk::PrimitiveTopology::eTriangleList;
  vk::Bool32 primitiveRestart = vk::False;

  vk::PipelineRasterizationStateCreateInfo rasterizer = {};
  vk::PipelineDepthStencilStateCreateInfo depthStencil = {};
  vk::PipelineMultisampleStateCreateInfo multisample = {};

  std::vector<vk::PipelineColorBlendAttachmentState> colorBlendAttachments;
  vk::PipelineColorBlendStateCreateInfo colorBlend = {};

  std::vector<vk::DynamicState> dynamicStates;

  vk::RenderPass renderPass = {};
  uint32_t subpass = 0;
};

struct FROZENSTARCRYSTAL_GRAPHICS_API PipelineError {
  enum class Code : uint8_t {
    shaderLoadFailed,
    layoutInvalid,
    creationFailed,
    notFound,
  } code;
  std::string message;
};

} // namespace graphics::vulkan::pipelines

export namespace std {

template <graphics::vulkan::pipelines::PipelineKind Kind>
struct hash<graphics::vulkan::pipelines::PipelineTag<Kind>> {
  static constexpr size_t golden = 0x9e3779b9;
  static constexpr size_t ml = 6, mr = 2;

  size_t operator()(
      const graphics::vulkan::pipelines::PipelineTag<Kind> &k) const noexcept {
    size_t seed = std::hash<const void *>{}(k.shaderTag);
    auto mix = [&](size_t h) {
      seed ^= h + golden + (seed << ml) + (seed >> mr);
    };
    mix(std::hash<uint64_t>{}(reinterpret_cast<uint64_t>(
        static_cast<vk::PipelineLayout::CType>(k.layout))));
    mix(std::hash<uint32_t>{}(static_cast<uint32_t>(
        static_cast<vk::PipelineCreateFlags::MaskType>(k.flags))));
    mix(std::hash<uint8_t>{}(static_cast<uint8_t>(Kind)));
    return seed;
  }
};

} // namespace std
