module;

#include "FrozenStarCrystal-graphics_export.h"

export module graphics.vulkan.compositors:structs;

import std.compat;
import vulkan;

import graphics.vulkan.shaders;
import graphics.vulkan.pipelines;
import graphics.vulkan.media;

export namespace graphics::vulkan::compositors {

struct FROZENSTARCRYSTAL_GRAPHICS_API FrameContext {
  vk::CommandBuffer cmd;
  vk::Image image;
  vk::ImageView view;
  vk::Extent2D extent;
  vk::Format format;
  uint32_t imageIndex = 0;
  std::shared_ptr<vk::raii::ImageView> ownedView;
  bool valid = false;
};

struct FROZENSTARCRYSTAL_GRAPHICS_API SceneRenderContext {
  vk::Extent2D viewportExtent;
  vk::Format colorFormat;
  // TODO Add camera pointers, etc.
  // maybe change from managers to the used tags
  std::shared_ptr<shaders::Manager> shadersManager;
  std::shared_ptr<pipelines::Manager> pipelineManager;
  std::shared_ptr<media::ImageArrayRegistry> bindlessImages;
  std::shared_ptr<media::BufferArrayRegistry> bindlessBuffers;
};

struct SceneId {
  uint64_t value;
  explicit SceneId(uint64_t v = 0) : value(v) {}

  [[nodiscard]] SceneId next() const { return SceneId{value + 1}; }

  constexpr auto operator<=>(const SceneId &) const noexcept = default;

  struct Hash {
    std::size_t operator()(const SceneId &id) const noexcept {
      return std::hash<uint64_t>{}(id.value);
    }
  };
};

} // namespace graphics::vulkan::compositors
