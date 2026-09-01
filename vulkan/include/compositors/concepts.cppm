module;

export module graphics.vulkan.compositors:concepts;

import std.compat;
import vulkan;

import :structs;

import graphics.vulkan.devices;
import concurrency;

export namespace graphics::vulkan::compositors {

template <typename T>
concept RenderTargetPolicy = requires(T &t) {
  // begin a frame, returning FrameContext
  { t.beginFrame() } -> std::same_as<FrameContext>;
  // end a frame
  { t.endFrame(std::declval<FrameContext &>()) } -> std::same_as<void>;
  // get current extent
  { t.extent() } -> std::same_as<vk::Extent2D>;
  // get color format
  { t.colorFormat() } -> std::same_as<vk::Format>;
};

// for possible future implementations that need it
template <typename T>
concept RenderTargetPolicyOptionals = requires(T &t) {
  // this target have a swapchain - links to a window
  { t.swapchain() } -> std::same_as<std::shared_ptr<devices::Swapchain>>;
};

template <typename T>
concept Scene = requires(T &s, const FrameContext frame) {
  { s.beginRecord(frame) } -> std::same_as<void>;

  {
    s.record(frame)
  } -> std::same_as<concurrency::pool::coroutine::CoroutineTask<
      concurrency::pool::coroutine::policy::Suspend::Never, void>>;

  { s.endRecord(frame) } -> std::same_as<void>;

  { s.sceneContext() } -> std::same_as<SceneRenderContext>;
  {
    s.setSceneContext(std::declval<const SceneRenderContext &>())
  } -> std::same_as<void>;
};

} // namespace graphics::vulkan::compositors
