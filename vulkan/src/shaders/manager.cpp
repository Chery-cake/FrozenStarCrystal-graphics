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
  auto existing = shaderModuleRegistry_.getStored(tag);
  if (!existing) {
    // Not loaded yet — compile fresh.
    shaderModuleRegistry_.emplace(tag,
                                  [&compiler = compiler_](const Shader &s) {
                                    return compiler->getBinary(s);
                                  });
    existing = shaderModuleRegistry_.getStored(tag);
    if (!existing) {
      return;
    }
  }

  if (compiler_->askRecompile(*tag)) {
    // Recompile succeeded → install fresh blob loader, reset source stamp.
    existing->markRecompiled(compiler_->getBinary(*tag));
  }

  // Recreate device-specific modules.
  std::unique_lock lock(deviceMtx_);
  for (auto &[device, registry] : deviceRegistries_) {
    registry->remove(tag);
    if (auto m = existing->createModule(*device)) {
      registry->add(tag, std::move(*m));
    }
  }
}

} // namespace graphics::vulkan::shaders
