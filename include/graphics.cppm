module;

export module graphics;

import :check;
import :vulkan_backend;

#if defined(GRAPHICS_BACKEND_VULKAN)
export import graphics.vulkan;
#else
#error "No graphics backend selected"
#endif

export namespace graphics {

#if defined(GRAPHICS_BACKEND_VULKAN)
using GraphicsBackend = VulkanBackend;
#else
#error "No graphics backend selected"
#endif

static_assert(
    ApiCheck<GraphicsBackend>,
    "The selected backend doesn't support all the minimun requirements");

} // namespace graphics
