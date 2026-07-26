module;

module graphics.vulkan.instances;

import vulkan;

namespace graphics::vulkan::instances {

namespace {

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

  std::cerr << std::format("[Vulkan {}] {}\n", severity,
                           pCallbackData->pMessage);

  return vk::False;
}

#ifdef ENGINE_DEBUG
std::unique_ptr<vk::raii::DebugUtilsMessengerEXT>
setupDebugMessenger(const vk::raii::Instance &instance) {

  vk::DebugUtilsMessengerCreateInfoEXT createInfo{
      {},
      vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
          vk::DebugUtilsMessageSeverityFlagBitsEXT::eError,
      vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
          vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
          vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance,
      debugCallback,
      nullptr};

  return std::make_unique<vk::raii::DebugUtilsMessengerEXT>(instance,
                                                            createInfo);
}
#endif

} // namespace

Instance::Instance() {
  vk::detail::DynamicLoader dl;
  auto instanceProc =
      dl.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
  if (instanceProc == nullptr) {
    throw std::runtime_error("[Instance] Failed to get vkGetInstanceProcAddr");
  }
  vk::detail::defaultDispatchLoaderDynamic.init(instanceProc);

  context_ = std::make_unique<vk::raii::Context>();

  const auto &layers = Config::instance().getInstanceLayers();

  auto unsupportedList = checkLayerSupport(layers);
  std::unordered_set<std::string> unsupported(unsupportedList.begin(),
                                              unsupportedList.end());
  if (!unsupported.empty()) {
    std::cerr << std::format(
        "[Instance] Skipping unsupported instance layers:\n");
    std::ranges::for_each(
        unsupportedList.begin(), unsupportedList.end(),
        [](const auto &layer) { std::cerr << std::format("  - {}\n", layer); });
    throw std::runtime_error("Necessary layers not supported\n");
  }

  const auto &optionalLayers = Config::instance().getOptionalInstanceLayers();

  unsupportedList = checkLayerSupport(optionalLayers);
  unsupported.insert(unsupportedList.begin(), unsupportedList.end());

  if (!unsupported.empty()) {
    std::cerr << std::format(
        "[Instance] Skipping unsupported optional instance layers:\n");
    std::ranges::for_each(
        unsupportedList.begin(), unsupportedList.end(),
        [](const auto &layer) { std::cerr << std::format("  - {}\n", layer); });
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
  const auto &exts = Config::instance().getInstanceExtensions();
  auto unsupportedExtensionsList = checkExtensionSupport(exts);
  std::unordered_set<std::string> unsupportedExt(
      unsupportedExtensionsList.begin(), unsupportedExtensionsList.end());
  if (!unsupportedExt.empty()) {
    std::cerr << std::format("[Instance] Unsupported extensions:\n");
    std::ranges::for_each(
        unsupportedExt.begin(), unsupportedExt.end(),
        [](const auto &ext) { std::cerr << std::format("  - {}\n", ext); });
    throw std::runtime_error("Necessary extensions not supported");
  }

  const auto &optionalExts = Config::instance().getOptionalInstanceExtensions();
  unsupportedExtensionsList = checkExtensionSupport(optionalExts);
  unsupportedExt.insert(unsupportedExtensionsList.begin(),
                        unsupportedExtensionsList.end());

  if (!unsupportedExt.empty()) {
    std::cerr << std::format("[Instance] Unsupported optional extensions:\n");
    std::ranges::for_each(
        unsupportedExt.begin(), unsupportedExt.end(),
        [](const auto &ext) { std::cerr << std::format("  - {}\n", ext); });
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

  instance_ = std::make_unique<vk::raii::Instance>(*context_, createInfo);
  vk::detail::defaultDispatchLoaderDynamic.init(**instance_, instanceProc);

#ifdef ENGINE_DEBUG
  try {
    debugMessenger_ = setupDebugMessenger(getRaiiInstance());
  } catch (const vk::SystemError &e) {
    std::cerr << std::format(
        "[Instance] Failed to create debug messenger: {}\n", e.what());
  }
#endif

  std::println("[Instance] Initialized successfully");
}

Instance::~Instance() {
#ifdef ENGINE_DEBUG
  debugMessenger_.reset();
#endif
  instance_.reset();
  context_.reset();

  std::cout << "[Instance] Shutdown complete";
}

std::vector<std::string> Instance::getAvailableExtensions() {
  // Use DynamicLoader + C API directly to avoid vk::raii::Context segfaults
  vk::detail::DynamicLoader dl;
  auto getInstanceProcAddr =
      dl.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
  if (getInstanceProcAddr == nullptr) {
    throw std::runtime_error("[Instance] vkGetInstanceProcAddr not available");
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
    throw std::runtime_error("[Instance] vkGetInstanceProcAddr not available");
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

} // namespace graphics::vulkan::instances
