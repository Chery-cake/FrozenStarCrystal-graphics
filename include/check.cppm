module;

export module graphics:check;

import std.compat;

export namespace graphics {

// RenderContext is returned by beginFrame().
// Concept only requires the type to be default-constructible and have a bool
// `valid` field so the caller can check whether acquisition succeeded.
template <typename T>
concept RenderContextType = requires(T ctx) {
    { ctx.valid } -> std::convertible_to<bool>;
};

template <typename T>
concept ApiCheck = requires(T &api) {
    // Core lifecycle
    { api.initialize() } -> std::same_as<void>;
    { api.shutdown()   } -> std::same_as<void>;
    // Frame loop
    requires RenderContextType<decltype(api.beginFrame())>;
    { api.endFrame()   } -> std::same_as<void>;
    // Sync
    { api.waitIdle()   } -> std::same_as<void>;
};

} // namespace graphics
