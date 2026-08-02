module;

module graphics.vulkan.instances;

import std.compat;
import signals;
import vulkan;

namespace graphics::vulkan::instances {

void Config::resetToDefaults() {
  std::lock_guard lock(mtx);

  instanceExtensions.clear();
  deviceExtensions.clear();
  instanceLayers.clear();
  optionalInstanceExtensions.clear();
  optionalDeviceExtensions.clear();
  optionalInstanceLayers.clear();

  // Initialize with sensible defaults
#ifdef ENGINE_DEBUG
  // Add validation layer in debug builds
  instanceLayers.emplace_back("VK_LAYER_KHRONOS_validation");
#endif

  // Common instance extensions
  instanceExtensions.emplace_back(vk::KHRSurfaceExtensionName);
#ifdef _WIN32
  instanceExtensions.emplace_back(vk::KHRWin32SurfaceExtensionName);
#elif defined(__linux__)
  instanceExtensions.emplace_back(vk::KHRXcbSurfaceExtensionName);
  instanceExtensions.emplace_back(vk::KHRWaylandSurfaceExtensionName);
#elif defined(__APPLE__)
  instanceExtensions.emplace_back(vk::EXTMetalSurfaceExtensionName);
#endif

  // Common device extensions
  deviceExtensions.emplace_back(vk::KHRSwapchainExtensionName);
}

inline bool add(std::vector<std::string> &vec, std::mutex &mtx,
                const std::string &toAdd) {
  bool changed = false;

  {
    std::lock_guard lock(mtx);
    auto it = std::ranges::find(vec, toAdd);

    if (it == vec.end()) {
      vec.push_back(toAdd);
      changed = true;
    }
  }

  return changed;
}

inline bool remove(std::vector<std::string> &vec, std::mutex &mtx,
                   const std::string &toRemove) {
  bool removed = false;

  {
    std::lock_guard lock(mtx);
    auto it = std::ranges::find(vec, toRemove);

    if (it != vec.end()) {
      vec.erase(it);
      removed = true;
    }
  }

  return removed;
}

bool Config::addInstanceExtension(const std::string &extension) {
  bool added = add(instanceExtensions, mtx, extension);

  if (added) {
    if (immediate) {
      vulkanChanged.emit();

    } else {
      needUpdate = true;
    }
  }
  return added;
}

bool Config::addDeviceExtension(const std::string &extension) {
  bool added = add(deviceExtensions, mtx, extension);

  if (added) {
    if (immediate) {
      vulkanChanged.emit();

    } else {
      needUpdate = true;
    }
  }
  return added;
}

bool Config::addInstanceLayer(const std::string &layer) {
  bool added = add(instanceLayers, mtx, layer);

  if (added) {
    if (immediate) {
      vulkanChanged.emit();

    } else {
      needUpdate = true;
    }
  }
  return added;
}

bool Config::removeInstanceExtension(const std::string &extension) {
  bool removed = remove(instanceExtensions, mtx, extension);

  if (removed) {
    if (immediate) {
      vulkanChanged.emit();

    } else {
      needUpdate = true;
    }
  }
  return removed;
}

bool Config::removeDeviceExtension(const std::string &extension) {
  bool removed = remove(deviceExtensions, mtx, extension);

  if (removed) {
    if (immediate) {
      vulkanChanged.emit();

    } else {
      needUpdate = true;
    }
  }
  return removed;
}

bool Config::removeInstanceLayer(const std::string &layer) {
  bool removed = remove(instanceLayers, mtx, layer);

  if (removed) {
    if (immediate) {
      vulkanChanged.emit();

    } else {
      needUpdate = true;
    }
  }
  return removed;
}

bool Config::addOptionalInstanceExtension(const std::string &extension) {
  bool added = add(optionalInstanceExtensions, mtx, extension);

  if (added) {
    if (immediate) {
      vulkanChanged.emit();

    } else {
      needUpdate = true;
    }
  }
  return added;
}

bool Config::addOptionalDeviceExtension(const std::string &extension) {
  bool added = add(optionalDeviceExtensions, mtx, extension);

  if (added) {
    if (immediate) {
      vulkanChanged.emit();

    } else {
      needUpdate = true;
    }
  }
  return added;
}

bool Config::addOptionalInstanceLayer(const std::string &layer) {
  bool added = add(optionalInstanceLayers, mtx, layer);

  if (added) {
    if (immediate) {
      vulkanChanged.emit();

    } else {
      needUpdate = true;
    }
  }
  return added;
}

bool Config::removeOptionalInstanceExtension(const std::string &extension) {
  bool removed = remove(optionalInstanceExtensions, mtx, extension);

  if (removed) {
    if (immediate) {
      vulkanChanged.emit();

    } else {
      needUpdate = true;
    }
  }
  return removed;
}

bool Config::removeOptionalDeviceExtension(const std::string &extension) {
  bool removed = remove(optionalDeviceExtensions, mtx, extension);

  if (removed) {
    if (immediate) {
      vulkanChanged.emit();

    } else {
      needUpdate = true;
    }
  }
  return removed;
}

bool Config::removeOptionalInstanceLayer(const std::string &layer) {
  bool removed = remove(optionalInstanceLayers, mtx, layer);

  if (removed) {
    if (immediate) {
      vulkanChanged.emit();

    } else {
      needUpdate = true;
    }
  }
  return removed;
}

} // namespace graphics::vulkan::instances
