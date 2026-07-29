module;

#include "FrozenStarCrystal-graphics_export.h"

export module graphics.vulkan.shaders:structs;

import std.compat;
import vulkan;

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

  inline static const size_t golden_ratio = 0x9e3779b9;

  inline static const size_t buffer_size = 4096;
  inline static const size_t mix_left = 6;
  inline static const size_t mix_right = 2;

  static std::size_t computeFileHash(const std::string &path) {
    try {
      // Get modification time and file size
      std::filesystem::file_time_type ftime =
          std::filesystem::last_write_time(path);
      auto fileSize = std::filesystem::file_size(path);

      // Convert to numeric representations
      auto timeCount = ftime.time_since_epoch().count();
      auto sizeVal = static_cast<std::size_t>(fileSize);

      // Combine path, modification time, and size
      std::size_t hash = std::hash<std::string>{}(path);
      hash ^= std::hash<decltype(timeCount)>{}(timeCount) + golden_ratio +
              (hash << mix_left) + (hash >> mix_right);
      hash ^= sizeVal + golden_ratio + (hash << mix_left) + (hash >> mix_right);

      return hash;
    } catch (...) {
      // Fallback if file doesn't exist or permissions fail
      return std::hash<std::string>{}(path);
    }
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

struct FROZENSTARCRYSTAL_GRAPHICS_API ShaderModule {
  std::vector<EntryPoint> entryPoints;
  std::string sourcePath;
  std::string compiledPath;
  size_t hash;

  ShaderModule(const Shader &shader,
               const std::function<std::string(Shader)> &getShaderBinary)
      : entryPoints(shader.entryPoints), sourcePath(shader.sourcePath),
        compiledPath(getShaderBinary(shader)),
        hash(std::hash<Shader>{}(shader)) {}

  std::expected<std::shared_ptr<vk::raii::ShaderModule>, ShaderError>
  createModule(vk::raii::Device &device) const {

    std::error_code ec;

    // 1. Check file existence
    if (!std::filesystem::exists(compiledPath, ec)) {
      return std::unexpected(
          ShaderError{.code = ShaderError::Code::fileNotFound,
                      .message = "Compiled shader not found: " + compiledPath});
    }

    // 2. Get size (replaces tellg/seekg)
    auto fileSize = std::filesystem::file_size(compiledPath, ec);
    if (ec) {
      return std::unexpected(
          ShaderError{.code = ShaderError::Code::fileNotFound,
                      .message = "Failed to query size of: " + compiledPath});
    }

    // 3. Validate size constraints
    if (fileSize % sizeof(uint32_t) != 0) {
      return std::unexpected(ShaderError{
          .code = ShaderError::Code::wrongSizeMultiplier,
          .message = "Compiled shader size not a multiple of 4 bytes"});
    }
    if (fileSize == 0) {
      return std::unexpected(
          ShaderError{.code = ShaderError::Code::noFileSize,
                      .message = "Compiled shader is empty"});
    }

    // 4. Read the binary data
    std::string binary;
    binary.resize(static_cast<std::size_t>(fileSize));
    std::ifstream file(compiledPath, std::ios::binary);
    if (!file.read(binary.data(), static_cast<std::streamsize>(fileSize))) {
      return std::unexpected(ShaderError{
          .code = ShaderError::Code::fileNotFound,
          .message = "Failed to read compiled shader: " + compiledPath});
    }
    file.close();

    const auto *code = reinterpret_cast<const uint32_t *>(binary.data());
    vk::ShaderModuleCreateInfo info{};
    info.codeSize = fileSize;
    info.pCode = code;

    try {
      auto module = std::make_shared<vk::raii::ShaderModule>(device, info);
      return module;
    } catch (const std::exception &e) {
      return std::unexpected<ShaderError>(
          {.code = ShaderError::Code::creationFailed,
           .message = std::string("vk::ShaderModule creation failed: ") +
                      e.what() + '\n'});
    }
  }

  bool checkHashForRecreation() {
    return hash != std::hash<Shader>{}(
                       {.entryPoints = entryPoints, .sourcePath = sourcePath});
  }

  void recompile(const std::function<bool(Shader)> &askRecompile) {
    if (askRecompile({.entryPoints = entryPoints, .sourcePath = sourcePath})) {
      hash = std::hash<Shader>{}(
          {.entryPoints = entryPoints, .sourcePath = sourcePath});
    }
  }
};

} // namespace graphics::vulkan::shaders
