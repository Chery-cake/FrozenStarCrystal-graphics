// VMA Implementation - this file provides the VMA function implementations
// Must be in a separate shared library and linked by all modules that use VMA.
//
// Uses vulkan.hpp dynamic dispatch loader for Vulkan function loading.

// Include vulkan.hpp first with dynamic dispatch enabled
// #include <vulkan/vulkan.hpp>

// Provide VMA implementation
/*
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wnullability-completeness"
#pragma clang diagnostic ignored "-Wnullability-extension"

#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"
#include <vk_mem_alloc.hpp>
#include <vk_mem_alloc_raii.hpp>

#include <vk_mem_alloc_imported.hpp>
#include <vk_mem_alloc_static_assertions.hpp>

#pragma clang diagnostic pop
*/

import vk_mem_alloc;
