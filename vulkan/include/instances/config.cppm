module;

#include "FrozenStarCrystal-graphics_export.h"

export module graphics.vulkan.instances:config;

import std.compat;
import signals;
import vulkan;

export namespace graphics::vulkan::instances {

class FROZENSTARCRYSTAL_GRAPHICS_API Config {
  private:
    bool needUpdate = false;
    bool immediate = true;
    mutable std::mutex mtx;

    std::vector<std::string> instanceExtensions;
    std::vector<std::string> deviceExtensions;
    std::vector<std::string> instanceLayers;

    std::vector<std::string> optionalInstanceExtensions;
    std::vector<std::string> optionalDeviceExtensions;
    std::vector<std::string> optionalInstanceLayers;

    Config() { resetToDefaults(); }
    ~Config() = default;

  public:
    signals::Signals<void()> vulkanChanged;

    static constexpr const char *applicationName = APP_NAME;
    static constexpr uint32_t applicationVersion = vk::makeVersion(
        APP_VERSION_MAJOR, APP_VERSION_MINOR, APP_VERSION_PATCH);

    static constexpr const char *engineName = "FrozenStarCrystal";
    static constexpr uint32_t engineVersion = vk::makeVersion(0, 1, 0);
    static constexpr uint32_t minApiVersion = vk::ApiVersion14;

    static Config &instance() {
        static Config inst;
        return inst;
    }

    Config(const Config &) = delete;
    Config &operator=(const Config &) = delete;
    Config(Config &&) = delete;
    Config &operator=(Config &&) = delete;
    constexpr auto operator<=>(const Config &) const noexcept = delete;

    void resetToDefaults();

    bool needsUpdate() const {
        std::lock_guard lock(mtx);
        return needUpdate;
    }

    void resetUpdate() {
        std::lock_guard lock(mtx);
        needUpdate = false;
    }

    void setImmediate(bool value) {
        std::lock_guard lock(mtx);
        immediate = value;
    }

    bool addInstanceExtension(const std::string &extension);
    bool addDeviceExtension(const std::string &extension);
    bool addInstanceLayer(const std::string &layer);

    bool removeInstanceExtension(const std::string &extension);
    bool removeDeviceExtension(const std::string &extension);
    bool removeInstanceLayer(const std::string &layer);

    bool addOptionalInstanceExtension(const std::string &extension);
    bool addOptionalDeviceExtension(const std::string &extension);
    bool addOptionalInstanceLayer(const std::string &layer);

    bool removeOptionalInstanceExtension(const std::string &extension);
    bool removeOptionalDeviceExtension(const std::string &extension);
    bool removeOptionalInstanceLayer(const std::string &layer);

    const std::vector<std::string> &getInstanceExtensions() const {
        return instanceExtensions;
    }
    const std::vector<std::string> &getDeviceExtensions() const {
        return deviceExtensions;
    }
    const std::vector<std::string> &getInstanceLayers() const {
        return instanceLayers;
    }

    const std::vector<std::string> &getOptionalInstanceExtensions() const {
        return optionalInstanceExtensions;
    }
    const std::vector<std::string> &getOptionalDeviceExtensions() const {
        return optionalDeviceExtensions;
    }
    const std::vector<std::string> &getOptionalInstanceLayers() const {
        return optionalInstanceLayers;
    }
};

} // namespace graphics::vulkan::instances
