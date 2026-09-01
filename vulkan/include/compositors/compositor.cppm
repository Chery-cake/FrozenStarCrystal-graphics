module;

#include "FrozenStarCrystal-graphics_export.h"

export module graphics.vulkan.compositors:compositor;

import std.compat;
import vulkan;

import :structs;
import :concepts;

import graphics.vulkan.devices;
import concurrency;

export namespace graphics::vulkan::compositors {

using SceneTaskFactory =
    std::move_only_function<concurrency::pool::coroutine::CoroutineTask<
        concurrency::pool::coroutine::policy::Suspend::Never, void>(
        const FrameContext &)>;

template <RenderTargetPolicy Target>
class FROZENSTARCRYSTAL_GRAPHICS_API Compositor {
private:
  std::shared_ptr<devices::Device> device_;

  Target target_;
  std::vector<SceneTaskFactory> sceneFactories_;

public:
  explicit Compositor(Target &&target,
                      const std::shared_ptr<devices::Device> &device);

  // Add a scene. The scene must satisfy the Scene concept.
  // We type-erase it into a callable.
  template <Scene S> void addScene(S &&scene);

  // Remove a scene? Type-erasure makes this hard; typical approach is to
  // rebuild the vector or use an ID system. For now, provide clearScenes().
  void clearSceneFactories() { sceneFactories_.clear(); }

  // Render all scenes in vector order.
  concurrency::pool::coroutine::CoroutineTask<
      concurrency::pool::coroutine::policy::Suspend::Never, bool>
  render();
  // Access to target for resize etc.
  Target &target() { return target_; }
  const Target &target() const { return target_; }
};

} // namespace graphics::vulkan::compositors
