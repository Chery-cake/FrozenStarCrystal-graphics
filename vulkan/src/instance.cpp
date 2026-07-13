module;

#include <cstdio>

module graphics.vulkan;

import vulkan;

namespace graphics::vulkan {

static vk::Bool32
debugCallback(vk::DebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
              vk::DebugUtilsMessageTypeFlagsEXT messageType,
              const vk::DebugUtilsMessengerCallbackDataEXT *pCallbackData,
              void *pUserData) {
    (void)pUserData;
    (void)messageType;

    const char *severity = "UNKNOWN";
    if (messageSeverity & vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose) {
        severity = "VERBOSE";
    } else if (messageSeverity &
               vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo) {
        severity = "INFO";
    } else if (messageSeverity &
               vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning) {
        severity = "WARNING";
    } else if (messageSeverity &
               vk::DebugUtilsMessageSeverityFlagBitsEXT::eError) {
        severity = "ERROR";
    }

    std::println(stderr, "[Vulkan {}] {}", severity, pCallbackData->pMessage);

    return vk::False;
}

void Instance::shutdown() {
    if (!initialized_) {
        return;
    }

    debugMessenger_.reset();
    instance_.reset();
    context_.reset();

    initialized_ = false;

    std::println("[Instance] Shutdown complete");
}

bool Instance::initialize() {
    if (initialized_) {
        std::println("[Instance] Already initialized");
        return false;
    }

    vk::detail::DynamicLoader dl;
    auto instanceProc =
        dl.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
    if (instanceProc == nullptr) {
        std::println(stderr, "[Instance] Failed to get vkGetInstanceProcAddr");
        return false;
    }
    vk::detail::defaultDispatchLoaderDynamic.init(instanceProc);

    try {
        context_ = std::make_unique<vk::raii::Context>();
    } catch (const vk::SystemError &e) {
        std::println("[Instance] Failed to create context: {}", e.what());
        return false;
    }

    auto &layers = Config::instance().getInstanceLayers();

    auto unsupportedList = checkLayerSupport(layers);
    std::unordered_set<std::string> unsupported(unsupportedList.begin(),
                                                unsupportedList.end());
    if (!unsupported.empty()) {
        std::println(stderr,
                     "[Instance] Skipping unsupported instance layers:");
        std::ranges::for_each(
            unsupportedList.begin(), unsupportedList.end(),
            [](const auto &layer) { std::println(stderr, "  - {}", layer); });
        return false;
    }

    auto &optionalLayers = Config::instance().getOptionalInstanceLayers();

    unsupportedList = checkLayerSupport(optionalLayers);
    unsupported.insert(unsupportedList.begin(), unsupportedList.end());

    if (!unsupported.empty()) {
        std::println(stderr, "[Instance] Skipping unsupported "
                             "optional instance layers:");
        std::ranges::for_each(
            unsupportedList.begin(), unsupportedList.end(),
            [](const auto &layer) { std::println(stderr, "  - {}", layer); });
    }

    // Add supported instance layers
    std::vector<const char *> enabledLayers;

    std::ranges::copy(layers |
                          std::views::transform([](const std::string &layer) {
                              return layer.c_str();
                          }),
                      std::back_inserter(enabledLayers));

    std::ranges::copy(
        optionalLayers | std::views::filter([&unsupported](const auto &layer) {
            return unsupported.find(layer) == unsupported.end();
        }) | std::views::transform([](const std::string &layer) {
            return layer.c_str();
        }),
        std::back_inserter(enabledLayers));

    // Check extension support
    auto &exts = Config::instance().getInstanceExtensions();
    auto unsupportedExtensionsList = checkExtensionSupport(exts);
    std::unordered_set<std::string> unsupportedExt(
        unsupportedExtensionsList.begin(), unsupportedExtensionsList.end());
    if (!unsupportedExt.empty()) {
        std::println(stderr, "[Instance] Unsupported extensions:");
        std::ranges::for_each(
            unsupportedExt.begin(), unsupportedExt.end(),
            [](const auto &ext) { std::println(stderr, "  - {}", ext); });
        return false;
    }

    auto &optionalExts = Config::instance().getOptionalInstanceExtensions();
    unsupportedExtensionsList = checkExtensionSupport(optionalExts);
    unsupportedExt.insert(unsupportedExtensionsList.begin(),
                          unsupportedExtensionsList.end());

    if (!unsupportedExt.empty()) {
        std::println(stderr, "[Instance] Unsupported optional extensions:");
        std::ranges::for_each(
            unsupportedExt.begin(), unsupportedExt.end(),
            [](const auto &ext) { std::println(stderr, "  - {}", ext); });
    }

    // Prepare extension list
    std::vector<const char *> enabledExtensions;

    std::ranges::copy(exts | std::views::transform([](const std::string &ext) {
                          return ext.c_str();
                      }),
                      std::back_inserter(enabledExtensions));

    std::ranges::copy(
        optionalExts | std::views::filter([&unsupportedExt](const auto &ext) {
            return unsupportedExt.find(ext) == unsupportedExt.end();
        }) | std::views::transform([](const std::string &ext) {
            return ext.c_str();
        }),
        std::back_inserter(enabledExtensions));

// Add debug extension if layers are enabled (for debug messenger)
#ifdef ENGINE_DEBUG
    enabledExtensions.push_back(vk::EXTDebugUtilsExtensionName);
#endif

    vk::ApplicationInfo appInfo{Config::applicationName,
                                Config::applicationVersion, Config::engineName,
                                Config::engineVersion, Config::minApiVersion};

    vk::InstanceCreateInfo createInfo{
        {}, &appInfo, enabledLayers, enabledExtensions};

#ifdef ENGINE_DEBUG
    vk::DebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
    debugCreateInfo.messageSeverity =
        vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
        vk::DebugUtilsMessageSeverityFlagBitsEXT::eError;
    debugCreateInfo.messageType =
        vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
        vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
        vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance;
    debugCreateInfo.pfnUserCallback = debugCallback;

    createInfo.pNext = &debugCreateInfo;
#endif

    try {
        instance_ = std::make_unique<vk::raii::Instance>(*context_, createInfo);
    } catch (const vk::SystemError &e) {
        std::println(stderr, "[Instance] Failed to create instance: {}",
                     e.what());
        return false;
    }

    vk::detail::defaultDispatchLoaderDynamic.init(**instance_, instanceProc);

    // Setup debug messenger
#ifdef ENGINE_DEBUG
    if (!setupDebugMessenger()) {
        std::println(stderr, "[Instance] Failed to setup debug messenger");
        // Continue anyway, not critical
    }
#endif

    initialized_ = true;
    std::println("[Instance] Initialized successfully");
    return true;
}

std::vector<std::string> Instance::getAvailableExtensions() {
    // Use DynamicLoader + C API directly to avoid vk::raii::Context segfaults
    vk::detail::DynamicLoader dl;
    auto getInstanceProcAddr =
        dl.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
    if (getInstanceProcAddr == nullptr) {
        throw std::runtime_error(
            "[Instance] vkGetInstanceProcAddr not available");
    }

    vk::detail::DispatchLoaderDynamic dld{getInstanceProcAddr};
    auto exts = vk::enumerateInstanceExtensionProperties(nullptr, dld);

    return exts | std::views::transform([](const vk::ExtensionProperties &ext) {
               return std::string(ext.extensionName);
           }) |
           std::ranges::to<std::vector<std::string>>();
}

std::vector<std::string> Instance::getAvailableLayers() {
    // Use DynamicLoader + C API directly to avoid vk::raii::Context segfaults
    vk::detail::DynamicLoader dl;
    auto getInstanceProcAddr =
        dl.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
    if (getInstanceProcAddr == nullptr) {
        throw std::runtime_error(
            "[Instance] vkGetInstanceProcAddr not available");
    }

    vk::detail::DispatchLoaderDynamic dld{getInstanceProcAddr};
    auto layers = vk::enumerateInstanceLayerProperties(dld);

    return layers | std::views::transform([](const vk::LayerProperties &layer) {
               return std::string(layer.layerName);
           }) |
           std::ranges::to<std::vector<std::string>>();
}

std::vector<std::string>
Instance::checkExtensionSupport(const std::vector<std::string> &extensions) {
    auto available = getAvailableExtensions();
    std::unordered_set<std::string> available_set{available.begin(),
                                                  available.end()};

    std::vector<std::string> unsupported;

    std::ranges::copy_if(extensions, std::back_inserter(unsupported),
                         [&available_set](const auto &ext) {
                             return !available_set.contains(ext);
                         });

    return unsupported;
}

std::vector<std::string>
Instance::checkLayerSupport(const std::vector<std::string> &layers) {
    auto available = getAvailableLayers();
    std::unordered_set<std::string> available_set{available.begin(),
                                                  available.end()};

    std::vector<std::string> unsupported;

    std::ranges::copy_if(layers, std::back_inserter(unsupported),
                         [&available_set](const auto &layer) {
                             return !available_set.contains(layer);
                         });

    return unsupported;
}

bool Instance::setupDebugMessenger() {
    if (!instance_) {
        return false;
    }

    vk::DebugUtilsMessengerCreateInfoEXT createInfo{
        {},
        vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
            vk::DebugUtilsMessageSeverityFlagBitsEXT::eError,
        vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
            vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
            vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance,
        debugCallback,
        nullptr};

    try {
        debugMessenger_ = std::make_unique<vk::raii::DebugUtilsMessengerEXT>(
            *instance_, createInfo);
        return true;
    } catch (const vk::SystemError &e) {
        std::println(stderr, "[Instance] Failed to create debug messenger: {}",
                     e.what());
        return false;
    }
}

} // namespace graphics::vulkan
