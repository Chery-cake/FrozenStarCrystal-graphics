module;

export module graphics;

import :check;

#if defined(GRAPHICS_BACKEND_VULKAN)
export import graphics.vulkan;
export import :vulkan.backend;
export import :vulkan.backend_impl;

#else
#error "No graphics backend selected"
#endif

export namespace graphics {

#if defined(GRAPHICS_BACKEND_VULKAN)
using GraphicsBackend = Backend;
#else
#error "No graphics backend selected"
#endif

static_assert(
    ApiCheck<GraphicsBackend>,
    "The selected backend doesn't support all the minimun requirements");

} // namespace graphics
