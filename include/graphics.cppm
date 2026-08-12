module;

export module graphics;

import :check;

#if defined(GRAPHICS_BACKEND_VULKAN)
export import graphics.vulkan;

#else
#error "No graphics backend selected"
#endif

export namespace graphics {

#if defined(GRAPHICS_BACKEND_VULKAN)
using namespace vulkan;
using GraphicsApi = Api;
#else
#error "No graphics backend selected"
#endif

static_assert(
    ApiCheck<GraphicsApi>,
    "The selected backend doesn't support all the minimun requirements");
} // namespace graphics
