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
    : target_(std::forward<Target>(target)), device_(device) {}

template <RenderTargetPolicy Target>
template <Scene S>
void Compositor<Target>::addScene(S &&scene) {
  sceneFactories_.emplace_back(
      [scene = std::forward<S>(scene)](const FrameContext &frame) mutable
          -> concurrency::pool::coroutine::CoroutineTask<
              concurrency::pool::coroutine::policy::Suspend::Never, void> {
        scene.beginRecord(frame);
        co_await scene.record(frame);
        scene.endRecord(frame);
      });
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

  for (auto &factory : sceneFactories_) {
    co_await factory(frame);
  }

  target_.endFrame(frame);
  co_return true;
}

} // namespace graphics::vulkan::compositors
