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

struct FROZENSTARCRYSTAL_GRAPHICS_API InputAssemblyState {
  vk::PrimitiveTopology topology = vk::PrimitiveTopology::eTriangleList;
  vk::Bool32 primitiveRestart = vk::False;

  constexpr auto
  operator<=>(const InputAssemblyState &) const noexcept = default;
};

struct FROZENSTARCRYSTAL_GRAPHICS_API RasterizationState {
  vk::PolygonMode polygonMode = vk::PolygonMode::eFill;
  vk::CullModeFlags cullMode = vk::CullModeFlagBits::eBack;
  vk::FrontFace frontFace = vk::FrontFace::eCounterClockwise;
  vk::Bool32 depthClamp = vk::False;
  vk::Bool32 depthBias = vk::False;
  float lineWidth = 1.0F;

  constexpr auto
  operator<=>(const RasterizationState &) const noexcept = default;
};

struct FROZENSTARCRYSTAL_GRAPHICS_API DepthStencilState {
  vk::Bool32 depthTest = vk::True;
  vk::Bool32 depthWrite = vk::True;
  vk::CompareOp depthCompare = vk::CompareOp::eLess;
  vk::Bool32 stencilTest = vk::False;

  constexpr auto
  operator<=>(const DepthStencilState &) const noexcept = default;
};

struct FROZENSTARCRYSTAL_GRAPHICS_API MultisampleState {
  vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1;

  constexpr auto operator<=>(const MultisampleState &) const noexcept = default;
};

struct FROZENSTARCRYSTAL_GRAPHICS_API AttachmentFormats {
  std::vector<vk::Format> color;
  vk::Format depth = vk::Format::eUndefined;
  vk::Format stencil = vk::Format::eUndefined;

  constexpr bool operator==(const AttachmentFormats &) const noexcept = default;
};

// Dynamic pipeline (dynamic rendering, no render pass)

struct FROZENSTARCRYSTAL_GRAPHICS_API DynamicPipelineInfo {
  DynamicPipeline tag;

  InputAssemblyState inputAssembly{};
  RasterizationState rasterization{};
  DepthStencilState depthStencil{};
  MultisampleState multisample{};
  AttachmentFormats attachments{};

  // Vertex input
  std::vector<vk::VertexInputBindingDescription> vertexBindings;
  std::vector<vk::VertexInputAttributeDescription> vertexAttributes;

  // Per-attachment blend state
  std::vector<vk::PipelineColorBlendAttachmentState> colorBlendAttachments;

  // Extra dynamic states beyond Manager defaults
  std::vector<vk::DynamicState> extraDynamicStates;

  constexpr bool
  operator==(const DynamicPipelineInfo &) const noexcept = default;
};

//  Static pipeline (classic render pass)

struct FROZENSTARCRYSTAL_GRAPHICS_API StaticPipelineInfo {
  StaticPipeline tag;

  // Vertex input
  std::vector<vk::VertexInputBindingDescription> vertexBindings;
  std::vector<vk::VertexInputAttributeDescription> vertexAttributes;

  InputAssemblyState inputAssembly{};
  RasterizationState rasterization{};
  DepthStencilState depthStencil{};
  MultisampleState multisample{};
  AttachmentFormats attachments{};

  std::vector<vk::PipelineColorBlendAttachmentState> colorBlendAttachments;
  vk::PipelineColorBlendStateCreateInfo colorBlend = {};

  std::vector<vk::DynamicState> dynamicStates;

  vk::RenderPass renderPass = {};
  uint32_t subpass = 0;

  constexpr bool
  operator==(const StaticPipelineInfo &) const noexcept = default;
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

template <> struct hash<graphics::vulkan::pipelines::DynamicPipelineInfo> {
  static constexpr size_t golden = 0x9e3779b9;
  static constexpr size_t ml = 6, mr = 2;

  static void mix(size_t &seed, size_t h) noexcept {
    seed ^= h + golden + (seed << ml) + (seed >> mr);
  }

  size_t operator()(const graphics::vulkan::pipelines::DynamicPipelineInfo &k)
      const noexcept {
    size_t seed =
        std::hash<graphics::vulkan::pipelines::DynamicPipeline>{}(k.tag);

    mix(seed,
        std::hash<uint32_t>{}(static_cast<uint32_t>(k.inputAssembly.topology)));
    mix(seed, std::hash<uint32_t>{}(k.inputAssembly.primitiveRestart));

    mix(seed, std::hash<uint32_t>{}(
                  static_cast<uint32_t>(k.rasterization.polygonMode)));
    mix(seed, std::hash<uint32_t>{}(static_cast<uint32_t>(
                  static_cast<vk::CullModeFlags::MaskType>(
                      k.rasterization.cullMode))));
    mix(seed, std::hash<uint32_t>{}(
                  static_cast<uint32_t>(k.rasterization.frontFace)));
    mix(seed, std::hash<uint32_t>{}(k.rasterization.depthClamp));
    mix(seed, std::hash<uint32_t>{}(k.rasterization.depthBias));
    mix(seed, std::hash<float>{}(k.rasterization.lineWidth));

    mix(seed, std::hash<uint32_t>{}(k.depthStencil.depthTest));
    mix(seed, std::hash<uint32_t>{}(k.depthStencil.depthWrite));
    mix(seed, std::hash<uint32_t>{}(
                  static_cast<uint32_t>(k.depthStencil.depthCompare)));
    mix(seed, std::hash<uint32_t>{}(k.depthStencil.stencilTest));

    mix(seed,
        std::hash<uint32_t>{}(static_cast<uint32_t>(k.multisample.samples)));

    for (const auto format : k.attachments.color) {
      mix(seed, std::hash<uint32_t>{}(static_cast<uint32_t>(format)));
    }
    mix(seed,
        std::hash<uint32_t>{}(static_cast<uint32_t>(k.attachments.depth)));
    mix(seed,
        std::hash<uint32_t>{}(static_cast<uint32_t>(k.attachments.stencil)));

    for (const auto &blend : k.colorBlendAttachments) {
      mix(seed, std::hash<uint32_t>{}(blend.blendEnable));
      mix(seed, std::hash<uint32_t>{}(
                    static_cast<uint32_t>(blend.srcColorBlendFactor)));
      mix(seed, std::hash<uint32_t>{}(
                    static_cast<uint32_t>(blend.dstColorBlendFactor)));
      mix(seed,
          std::hash<uint32_t>{}(static_cast<uint32_t>(blend.colorBlendOp)));
      mix(seed, std::hash<uint32_t>{}(static_cast<uint32_t>(
                    static_cast<vk::ColorComponentFlags::MaskType>(
                        blend.colorWriteMask))));
    }

    return seed;
  }
};

template <> struct hash<graphics::vulkan::pipelines::StaticPipelineInfo> {
  static constexpr size_t ml = 6, mr = 2;

  size_t operator()(
      const graphics::vulkan::pipelines::StaticPipelineInfo &k) const noexcept {
    graphics::vulkan::pipelines::DynamicPipelineInfo dynamicEquivalent{};
    dynamicEquivalent.tag.shaderTag = k.tag.shaderTag;
    dynamicEquivalent.tag.layout = k.tag.layout;
    dynamicEquivalent.tag.flags = k.tag.flags;
    dynamicEquivalent.inputAssembly = k.inputAssembly;
    dynamicEquivalent.rasterization = k.rasterization;
    dynamicEquivalent.depthStencil = k.depthStencil;
    dynamicEquivalent.multisample = k.multisample;
    dynamicEquivalent.attachments = k.attachments;
    dynamicEquivalent.colorBlendAttachments = k.colorBlendAttachments;

    size_t seed = std::hash<graphics::vulkan::pipelines::DynamicPipelineInfo>{}(
        dynamicEquivalent);
    constexpr size_t golden = 0x9e3779b9;
    seed ^= std::hash<uint64_t>{}(reinterpret_cast<uint64_t>(
                static_cast<vk::RenderPass::CType>(k.renderPass))) +
            golden + (seed << ml) + (seed >> mr);
    seed ^=
        std::hash<uint32_t>{}(k.subpass) + golden + (seed << ml) + (seed >> mr);
    return seed;
  }
};

} // namespace std
