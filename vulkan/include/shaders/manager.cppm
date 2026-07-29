module;

#include "FrozenStarCrystal-graphics_export.h"

export module graphics.vulkan.shaders:manager;

import std.compat;
import vulkan;
import resource;

import :structs;
import :compiler;

namespace graphics::vulkan::shaders {

using shaderModuleRegistry =
    resource::Registry<Shader, ShaderModule,
                       resource::SharedPtrPolicy<Shader, ShaderModule>>;
using deviceModuleRegistry = resource::Registry<
    Shader, vk::raii::ShaderModule,
    resource::SharedPtrPolicy<Shader, vk::raii::ShaderModule>>;

} // namespace graphics::vulkan::shaders

export namespace graphics::vulkan::shaders {

class FROZENSTARCRYSTAL_GRAPHICS_API Manager {
public:
private:
  std::unique_ptr<Compiler> compiler_;

  shaderModuleRegistry shaderModuleRegistry_;

  std::unordered_map<std::shared_ptr<vk::raii::Device>,
                     std::unique_ptr<deviceModuleRegistry>>
      deviceRegistries_;
  mutable std::shared_mutex deviceMtx_;

  deviceModuleRegistry &
  getDeviceRegistry(const std::shared_ptr<vk::raii::Device> &device);

public:
  Manager();
  ~Manager();

  Manager(const Manager &) = delete;
  Manager &operator=(const Manager &) = delete;
  Manager(Manager &&) = delete;
  Manager &operator=(Manager &&) = delete;

  // -- per‑device operations --
  // Load (compile if needed) and return the Vulkan module for a device.
  [[nodiscard]] std::expected<std::shared_ptr<vk::raii::ShaderModule>,
                              ShaderError>
  loadShader(const Shader *tag,
             const std::shared_ptr<vk::raii::Device> &device);

  // Retrieve an already‑loaded module.
  [[nodiscard]] std::shared_ptr<vk::raii::ShaderModule>
  getModule(const Shader *tag, const std::shared_ptr<vk::raii::Device> &device);

  // -- lifecycle --
  void
  reloadShader(const Shader *tag); // recompile & recreate all device modules
  void unloadShader(const Shader *tag,
                    const std::shared_ptr<vk::raii::Device> &device) {
    std::shared_lock lock(deviceMtx_);
    getDeviceRegistry(device).remove(tag);
  }
  void unloadShaderAllDevice(const Shader *tag) {
    std::shared_lock lock(deviceMtx_);

    std::ranges::for_each(deviceRegistries_, [&tag](auto &pair) {
      auto &[device, registry] = pair;
      registry->remove(tag);
    });
  }
};

} // namespace graphics::vulkan::shaders
