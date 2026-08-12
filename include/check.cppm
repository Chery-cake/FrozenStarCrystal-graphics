module;

export module graphics:check;

import std.compat;

#if defined(GRAPHICS_BACKEND_VULKAN)
import graphics.vulkan;

#else
#error "No graphics backend selected"
#endif

export namespace graphics {

template <typename T>
concept ApiCheck = requires(T &api) {
  // {api.initialize()} -> std::same_as<void>;
  std::not_equal_to<void>();

  // Frame loop TODO improve check
  /*  { api.beginFrame() } -> std::same_as<vulkan::WindowFrame>;
    { api.endFrame() } -> std::same_as<void>;*/
  // Sync
  { api.waitIdle() } -> std::same_as<void>;
};

} // namespace graphics
