module;

export module graphics.vulkan.devices;

export import :structs;
export import :device;
export import :swapchain;
export import :manager;
export import :transfers;
export import :semaphores;

// TODO
// change all fences and semaphores to timeline semaphores
// have binary semaphores as backup when not supported
//
// make all calls made through a device to go inside the thread pool, to not
// lock the main thread
// check if all run inside the thread pool
//
// change submit/dispach commands to be fully async, so the CPU doesn't need to
// keep waiting for it, and can call for it's completion later
// check if all were changed
