module;

#include "FrozenStarCrystal-graphics_export.h"

export module graphics.vulkan.shaders:structs;

import std.compat;
import vulkan;
import files;

export namespace graphics::vulkan::shaders {

struct FROZENSTARCRYSTAL_GRAPHICS_API EntryPoint {
  std::string entry;
  vk::ShaderStageFlagBits type;

  constexpr auto operator<=>(const EntryPoint &) const noexcept = default;
};

struct FROZENSTARCRYSTAL_GRAPHICS_API Shader {
  std::vector<EntryPoint> entryPoints;
  std::string sourcePath;

  constexpr auto operator<=>(const Shader &) const noexcept = default;
};

} // namespace graphics::vulkan::shaders

export namespace std {
template <> struct hash<graphics::vulkan::shaders::Shader> {

  static constexpr size_t golden_ratio = 0x9e3779b9;

  static constexpr size_t buffer_size = 4096;
  static constexpr size_t mix_left = 6;
  static constexpr size_t mix_right = 2;

  static std::size_t computeFileHash(const std::string &path) {
    auto stamp = files::file_stamp::read(files::path{path});
    if (!stamp) {
      return std::hash<std::string>{}(path);
    }

    auto timeCount = stamp->mtime.time_since_epoch().count();
    auto sizeVal = static_cast<std::size_t>(stamp->size);

    std::size_t hash = std::hash<std::string>{}(path);
    hash ^= std::hash<decltype(timeCount)>{}(timeCount) + golden_ratio +
            (hash << mix_left) + (hash >> mix_right);
    hash ^= std::hash<std::size_t>{}(sizeVal) + golden_ratio +
            (hash << mix_left) + (hash >> mix_right);
    return hash;
  }

  std::size_t
  operator()(const graphics::vulkan::shaders::Shader &s) const noexcept {

    std::size_t seed = computeFileHash(s.sourcePath);

    auto eps = s.entryPoints;
    std::ranges::sort(eps, [](const auto &a, const auto &b) {
      if (a.entry != b.entry) {
        return a.entry < b.entry;
      }
      return static_cast<vk::ShaderStageFlags::MaskType>(a.type) <
             static_cast<vk::ShaderStageFlags::MaskType>(b.type);
    });

    std::ranges::for_each(eps, [&seed](const auto &ep) {
      auto h = std::hash<std::string>{}(ep.entry) ^
               (std::hash<vk::ShaderStageFlags>{}(ep.type) << 1);
      seed ^= h + golden_ratio + (seed << mix_left) + (seed >> mix_right);
    });

    return seed;
  }
};
} // namespace std

export namespace graphics::vulkan::shaders {

struct FROZENSTARCRYSTAL_GRAPHICS_API ShaderError {
  enum class Code : uint8_t {
    fileNotFound,
    wrongSizeMultiplier,
    noFileSize,
    creationFailed,
  } code;
  std::string message;
};

// TODO
// use the files reloadable_file for reloading/recompiling the shaders
struct FROZENSTARCRYSTAL_GRAPHICS_API ShaderModule {
  std::vector<EntryPoint> entryPoints;
  std::string sourcePath;

  files::reloadable_file<files::blob> compiledShaders;
  files::file_stamp sourceStamp;

  ShaderModule(const Shader &shader,
               const std::function<std::string(Shader)> &getShaderBinary)
      : entryPoints(shader.entryPoints), sourcePath(shader.sourcePath),
        compiledShaders(
            files::reloadable_file<files::blob>::with_default_loader(
                files::path{getShaderBinary(shader)})),
        sourceStamp(files::file_stamp::read(files::path{shader.sourcePath})
                        .value_or(files::file_stamp{})) {}

  [[nodiscard]] bool sourceChanged() const {
    auto now = files::file_stamp::read(files::path{sourcePath});
    return !now || !(*now == sourceStamp);
  }

  void markRecompiled(std::string newCompiledPath) {
    compiledShaders = files::reloadable_file<files::blob>::with_default_loader(
        files::path{std::move(newCompiledPath)});
    sourceStamp = files::file_stamp::read(files::path{sourcePath})
                      .value_or(files::file_stamp{});
  }

  std::expected<void, ShaderError> refreshBinary() {
    auto r = compiledShaders.refresh();
    if (!r) {
      return std::unexpected{
          ShaderError{.code = ShaderError::Code::fileNotFound,
                      .message = r.error().what()}};
    }
    return {};
  }

  std::expected<std::shared_ptr<vk::raii::ShaderModule>, ShaderError>
  createModule(vk::raii::Device &device) {
    if (auto r = refreshBinary(); !r) {
      return std::unexpected{r.error()};
    }
    const auto &binary = compiledShaders.value();

    if (binary.empty() || binary.size() % sizeof(uint32_t) != 0) {
      return std::unexpected{ShaderError{
          .code = ShaderError::Code::wrongSizeMultiplier,
          .message = "Compiled shader size not a multiple of 4 bytes"}};
    }

    vk::ShaderModuleCreateInfo info{};
    info.codeSize = binary.size();
    info.pCode = reinterpret_cast<const uint32_t *>(binary.data());

    try {
      return std::make_shared<vk::raii::ShaderModule>(device, info);
    } catch (const std::exception &e) {
      return std::unexpected{ShaderError{
          .code = ShaderError::Code::creationFailed,
          .message = std::string("vk::raii::ShaderModule creating failed ") +
                     e.what() + '\n'}};
    }
  }
};

} // namespace graphics::vulkan::shaders
