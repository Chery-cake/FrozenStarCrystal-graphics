module;

#include "FrozenStarCrystal-graphics_export.h"
#include <slang-com-ptr.h>
#include <slang.h>

export module graphics.vulkan.shaders:compiler;

import std.compat;

import :structs;

export namespace graphics::vulkan::shaders {

class FROZENSTARCRYSTAL_GRAPHICS_API Compiler {
public:
  struct CompilerError {
    enum class Code : uint8_t {
      fileNotFound,
      sessionNotCreated,
      moduleFailedToLoad,
      entryNotFound,
      failedToComposeShader,
      failedToLinkShaders,
      failedToGetSPIRV,
      invalidCodeSize,
      unexpectedError,
    } code;
    std::string message;
  };

private:
  static Slang::ComPtr<slang::IGlobalSession> &globalSession();

  std::vector<std::filesystem::path> includePaths_;

  mutable std::recursive_mutex mtx_;

public:
  Compiler();
  ~Compiler() = default;

  Compiler(const Compiler &) = delete;
  Compiler &operator=(const Compiler &) = delete;
  Compiler(Compiler &&) = delete;
  Compiler &operator=(Compiler &&) = delete;

  std::expected<std::string, CompilerError> compile(const Shader &shader);
  std::string getBinary(const Shader &shader);
  bool askRecompile(const Shader &shader);

  void addPath(const std::filesystem::path &path) {
    std::unique_lock lock(mtx_);
    includePaths_.push_back(path);
  }
  void clearPaths(); // all but the base path "assets/shaders"
  std::vector<std::filesystem::path> getPaths() {
    std::unique_lock lock(mtx_);
    return includePaths_;
  }
};

} // namespace graphics::vulkan::shaders
