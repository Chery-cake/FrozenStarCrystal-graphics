// vulkan.cpp
// This file provides the storage for the Vulkan-Hpp dynamic dispatch loader.
// Must be compiled into a single shared library that all other modules link
// against.
//
// VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE provides the global
// vk::DispatchLoaderDynamic instance that all Vulkan-Hpp calls use.
// Uses vk::detail::DynamicLoader for loading Vulkan library and getting
// function pointers.

/*
 *
 * #include <vulkan/vulkan.hpp>
 * VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE
 *
 */

import vulkan;

#ifdef __APPLE__
// Point the Vulkan loader at MoltenVK's ICD manifest.
// The path is relative to the executable — set once before DynamicLoader.
// This is the only Apple-specific code needed anywhere in the engine.
struct VKICDEnvSetter {
    VKICDEnvSetter() {
        // Only set if not already overridden (e.g. by a dev with the Vulkan
        // SDK)
        if (!std::getenv("VK_ICD_FILENAMES")) {
            setenv("VK_ICD_FILENAMES", "vulkan/icd.d/MoltenVK_icd.json", 0);
        }
    }
};
static VKICDEnvSetter s_vk_icd_env_setter;
#endif
