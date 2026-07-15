module;

#include "FrozenStarCrystal-graphics_export.h"

export module graphics.vulkan.instances:instance;

import std.compat;
import vulkan;

export namespace graphics::vulkan::instances {

class FROZENSTARCRYSTAL_GRAPHICS_API Instance {
  private:
    std::unique_ptr<vk::raii::Context> context_;
    std::shared_ptr<vk::raii::Instance> instance_;

#ifdef ENGINE_DEBUG
    std::unique_ptr<vk::raii::DebugUtilsMessengerEXT> debugMessenger_;
#endif

  public:
    Instance();
    ~Instance();

    Instance(const Instance &) = delete;
    Instance &operator=(const Instance &) = delete;
    Instance(Instance &&) = delete;
    Instance &operator=(Instance &&) = delete;

    [[nodiscard]] std::shared_ptr<vk::raii::Instance> getInstancePtr() {
        return instance_;
    }
    [[nodiscard]] vk::Instance getInstance() {
        return instance_ ? **instance_ : vk::Instance{};
    }
    [[nodiscard]] const vk::raii::Instance &getRaiiInstance() {
        return *instance_;
    }

    [[nodiscard]] const vk::raii::Context &getRaiiContext() {
        return *context_;
    }

    static std::vector<std::string> getAvailableExtensions();
    static std::vector<std::string> getAvailableLayers();

    static std::vector<std::string>
    checkExtensionSupport(const std::vector<std::string> &extensions);
    static std::vector<std::string>
    checkLayerSupport(const std::vector<std::string> &layers);
};

} // namespace graphics::vulkan::instances
