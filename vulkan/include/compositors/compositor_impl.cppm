module;

export module graphics.vulkan.compositors:compositor_impl;

import std.compat;
import vulkan;

import :structs;
import :concepts;
import :compositor;

import graphics.vulkan.devices;
import concurrency;

export namespace graphics::vulkan::compositors {

template <RenderTargetPolicy Target>
Compositor<Target>::Compositor(Target &&target,
                               const std::shared_ptr<devices::Device> &device)
    : device_(device), target_(std::forward<Target>(target)) {}

template <RenderTargetPolicy Target>
template <Scene S>
SceneId Compositor<Target>::addScene(S &&scene) {
  std::unique_lock lock(mtx_);
  SceneId id = nextSceneId_;
  nextSceneId_ = nextSceneId_.next();
  sceneEntries_.emplace_back(
      id,
      [scene = std::forward<S>(scene)](const FrameContext &frame) mutable
          -> concurrency::pool::coroutine::CoroutineTask<
              concurrency::pool::coroutine::policy::Suspend::Never, void> {
        scene.beginRecord(frame);
        co_await scene.record(frame);
        scene.endRecord(frame);
      });
  return id;
}

template <RenderTargetPolicy Target>
template <Scene S>
SceneId Compositor<Target>::addScene(S &&scene, Position pos) {
  std::unique_lock lock(mtx_);
  SceneId id = nextSceneId_;
  nextSceneId_ = nextSceneId_.next();

  auto factory = [scene =
                      std::forward<S>(scene)](const FrameContext &frame) mutable
      -> concurrency::pool::coroutine::CoroutineTask<
          concurrency::pool::coroutine::policy::Suspend::Never, void> {
    scene.beginRecord(frame);
    co_await scene.record(frame);
    scene.endRecord(frame);
  };

  switch (pos) {
  case Position::START: {
    sceneEntries_.insert(sceneEntries_.begin(),
                         SceneEntry{id, std::move(factory)});
    break;
  }
  case Position::END:
  default: {
    sceneEntries_.emplace_back(id, std::move(factory));
    break;
  }
  }
  return id;
}

template <RenderTargetPolicy Target>
bool Compositor<Target>::removeScene(const SceneId &id) {
  std::unique_lock lock(mtx_);
  auto it = std::ranges::find_if(
      sceneEntries_, [id](const auto &entry) { return entry.id == id; });
  if (it != sceneEntries_.end()) {
    sceneEntries_.erase(it);
    return true;
  }
  return false;
}

template <RenderTargetPolicy Target>
bool Compositor<Target>::removeScenes(const std::span<const SceneId> &ids) {
  if (ids.empty()) {
    return false;
  }

  std::unique_lock lock(mtx_);

  std::unordered_set<SceneId, SceneId::Hash> idsToRemove(ids.begin(),
                                                         ids.end());

  auto count =
      std::erase_if(sceneEntries_, [&idsToRemove](const SceneEntry &entry) {
        return idsToRemove.contains(entry.id);
      });

  return count > 0;
}

template <RenderTargetPolicy Target>
concurrency::pool::coroutine::CoroutineTask<
    concurrency::pool::coroutine::policy::Suspend::Never, bool>
Compositor<Target>::render() {
  // Run the whole frame on a single GPU worker thread.
  co_await device_
      ->schedule<concurrency::pool::coroutine::policy::Queue::Enqueue>();

  FrameContext frame = target_.beginFrame();
  if (!frame.valid) {
    co_return false;
  }

  for (auto &&[id, factory] : sceneEntries_) {
    co_await factory(frame);
  }

  target_.endFrame(frame);
  co_return true;
}

} // namespace graphics::vulkan::compositors
