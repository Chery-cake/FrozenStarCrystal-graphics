module;

module graphics.vulkan.shaders;

import std.compat;
import vulkan;

namespace graphics::vulkan::shaders {

Manager::Manager() : compiler_(std::make_unique<Compiler>()) {}

Manager::~Manager() {

  std::unique_lock lock(deviceMtx_);

  deviceRegistries_.clear();
  shaderModuleRegistry_.clear();

  compiler_.reset();

  std::cout << "[Manager] Cleared \n";
}

deviceModuleRegistry &
Manager::getDeviceRegistry(const std::shared_ptr<vk::raii::Device> &device) {
  std::shared_lock lock(deviceMtx_);
  auto it = deviceRegistries_.find(device);
  if (it != deviceRegistries_.end()) {
    return *it->second;
  }
  lock.unlock();

  std::unique_lock writeLock(deviceMtx_);
  auto &&[newIt, inserted] = deviceRegistries_.emplace(
      device, std::make_unique<deviceModuleRegistry>());
  return *newIt->second;
}

std::expected<std::shared_ptr<vk::raii::ShaderModule>, ShaderError>
Manager::loadShader(const Shader *tag,
                    const std::shared_ptr<vk::raii::Device> &device) {
  {
    std::shared_lock lock(deviceMtx_);

    // 1. Ensure the shader is compiled (global cache)
    if (!shaderModuleRegistry_.contains(tag)) {
      lock.unlock();

      auto result = shaderModuleRegistry_.emplace(
          tag, [&compiler = compiler_](const Shader &s) {
            return compiler->getBinary(s);
          });

      if (!result) {
        return std::unexpected<ShaderError>(
            {.code = ShaderError::Code::creationFailed,
             .message = "Failed to emplace compiled shader"});
      }
    }
  }

  auto compiledMod = shaderModuleRegistry_.getStored(tag);

  // 2. Check device cache
  auto &devReg = getDeviceRegistry(device);
  auto existing = devReg.getStored(tag);
  if (existing != nullptr) {
    return std::shared_ptr<vk::raii::ShaderModule>(existing);
  }

  // 3. Create the device‑specific Vulkan module
  auto vkModResult = compiledMod->createModule(*device);
  if (!vkModResult) {
    return std::unexpected<ShaderError>(vkModResult.error());
  }

  // 4. Insert into device registry
  std::shared_ptr<vk::raii::ShaderModule> sharedMod = std::move(*vkModResult);
  bool added = devReg.add(tag, sharedMod);
  if (!added) {
    // race condition: another thread just added it; just return the one
    // from the registry
    return devReg.getStored(tag);
  }
  return sharedMod;
}

std::shared_ptr<vk::raii::ShaderModule>
Manager::getModule(const Shader *tag,
                   const std::shared_ptr<vk::raii::Device> &device) {
  std::shared_lock lock(deviceMtx_);
  auto &devReg = getDeviceRegistry(device);
  return devReg.getStored(tag);
}

void Manager::reloadShader(const Shader *tag) {
  // 1. Force re‑compilation
  shaderModuleRegistry_.remove(tag); // fires signals if needed
  shaderModuleRegistry_.emplace(tag, [&compiler = compiler_](const Shader &s) {
    return compiler->getBinary(
        s); // this will recompile because old binary is gone
  });
  auto compiledMod = shaderModuleRegistry_.getStored(tag);

  // 2. Recreate modules for every device that has this shader
  std::shared_lock lock(deviceMtx_);
  std::ranges::for_each(deviceRegistries_, [&tag, &compiledMod](auto &pair) {
    auto &&[device, registry] = pair;

    registry->remove(tag);

    auto newMod = compiledMod->createModule(*device);
    if (newMod) {
      registry->add(tag, std::move(*newMod));
    }
  });
}

} // namespace graphics::vulkan::shaders
