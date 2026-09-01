module;

export module graphics;

import :check;

// TODO
// check if theres a need for this integration when the system will be able to
// use multiple apis at once
//
// allow the system to use multiple apis at once

#if defined(GRAPHICS_BACKEND_VULKAN)
export import graphics.vulkan;

#else
#error "No graphics backend selected"
#endif

export namespace graphics {

#if defined(GRAPHICS_BACKEND_VULKAN)
#else
#error "No graphics backend selected"
#endif

static_assert(
    ApiCheck<int>,
    "The selected backend doesn't support all the minimun requirements");
} // namespace graphics
