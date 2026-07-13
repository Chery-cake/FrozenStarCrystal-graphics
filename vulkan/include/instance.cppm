module;

#include "FrozenStarCrystal-graphics_export.h"

export module graphics.vulkan:instance;

import std.compat;
import vulkan;

export namespace graphics::vulkan {

class FROZENSTARCRYSTAL_GRAPHICS_API Instance {
  private:
    bool setupDebugMessenger();

    std::unique_ptr<vk::raii::Context> context_;
    std::shared_ptr<vk::raii::Instance> instance_;
    std::unique_ptr<vk::raii::DebugUtilsMessengerEXT> debugMessenger_;

    bool initialized_ = false;

  public:
    Instance() = default;
    ~Instance() { shutdown(); };

    // Disable copy & move
    Instance(const Instance &) = delete;
    Instance &operator=(const Instance &) = delete;
    Instance(Instance &&) = delete;
    Instance &operator=(Instance &&) = delete;

    bool initialize(); // TODO change to optional or expected the return
    void shutdown();

    [[nodiscard]] bool isInitialized() const { return initialized_; }

    [[nodiscard]] vk::Instance getInstance() const {
        return instance_ ? **instance_ : vk::Instance{};
    }
    [[nodiscard]] const vk::raii::Instance &getRaiiInstance() const {
        return *instance_;
    }

    [[nodiscard]] const vk::raii::Context &getContext() const {
        return *context_;
    }

    static std::vector<std::string> getAvailableExtensions();
    static std::vector<std::string> getAvailableLayers();

    static std::vector<std::string>
    checkExtensionSupport(const std::vector<std::string> &extensions);
    static std::vector<std::string>
    checkLayerSupport(const std::vector<std::string> &layers);
};

}; // namespace graphics::vulkan
