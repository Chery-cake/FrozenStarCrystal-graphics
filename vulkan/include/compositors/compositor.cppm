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
public:
  struct SceneEntry {
    SceneId id;
    std::shared_ptr<SceneTaskFactory> factory;
  };

  enum class Position : uint8_t {
    START,
    END,
  };

private:
  std::shared_ptr<devices::Device> device_;

  Target target_;
  std::vector<SceneEntry> sceneEntries_;

  SceneId nextSceneId_{1};
  mutable std::shared_mutex mtx_;

public:
  explicit Compositor(Target &&target,
                      const std::shared_ptr<devices::Device> &device);

  // Add a scene. The scene must satisfy the Scene concept.
  // We type-erase it into a callable.
  template <Scene S> SceneId addScene(S &&scene);
  template <Scene S> SceneId addScene(S &&scene, Position pos);

  // Remove a scene? Type-erasure makes this hard; typical approach is to
  // rebuild the vector or use an ID system. For now, provide clearScenes().
  void clearSceneFactories() {
    std::unique_lock lock(mtx_);
    sceneEntries_.clear();
  }

  bool removeScene(const SceneId &id);
  bool removeScenes(const std::span<const SceneId> &ids);

  // Render all scenes in vector order.
  concurrency::pool::coroutine::CoroutineTask<
      concurrency::pool::coroutine::policy::Suspend::Never, bool>
  render();
  // Access to target for resize etc.
  Target &target() { return target_; }
  const Target &target() const { return target_; }
};

} // namespace graphics::vulkan::compositors
