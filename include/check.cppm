module;

export module graphics:check;

import std.compat;

export namespace graphics {

template <typename T>
concept ApiCheck = requires(T &api) {
    // {api.initialize()} -> std::same_as<void>;
    std::not_equal_to<void>();
};

} // namespace graphics
