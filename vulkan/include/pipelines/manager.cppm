module;

#include "FrozenStarCrystal-graphics_export.h"

export module graphics.vulkan.pipelines:manager;

import std.compat;
import vulkan;
import resource;

import graphics.vulkan.devices;
import graphics.vulkan.shaders;
import :structs;

export namespace graphics::vulkan::pipelines {
using DynamicCache =
    std::unordered_map<DynamicPipelineInfo, std::shared_ptr<vk::raii::Pipeline>>;
using StaticCache =
    std::unordered_map<StaticPipelineInfo, std::shared_ptr<vk::raii::Pipeline>>;

struct FROZENSTARCRYSTAL_GRAPHICS_API DeviceEntry {
  DynamicCache dynamicPipelines;
  StaticCache staticPipelines;
};
} // namespace graphics::vulkan::pipelines

export namespace graphics::vulkan::pipelines {

class FROZENSTARCRYSTAL_GRAPHICS_API Manager {
private:
  std::shared_ptr<shaders::Manager> shaderManager_;

  std::unordered_map<std::shared_ptr<vk::raii::Device>, DeviceEntry> cache_;
  mutable std::shared_mutex cacheMtx_;

  // Internal builders
  std::expected<std::shared_ptr<vk::raii::Pipeline>, PipelineError>
  buildDynamic(const DynamicPipelineInfo &info,
               const std::shared_ptr<vk::raii::Device> &device);

  std::expected<std::shared_ptr<vk::raii::Pipeline>, PipelineError>
  buildStatic(const StaticPipelineInfo &info,
              const std::shared_ptr<vk::raii::Device> &device);

public:
  Manager(const std::shared_ptr<shaders::Manager> &shaderManager);
  ~Manager();

  Manager(const Manager &) = delete;
  Manager &operator=(const Manager &) = delete;
  Manager(Manager &&) = delete;
  Manager &operator=(Manager &&) = delete;

  // Get or create a dynamic-rendering pipeline
  [[nodiscard]] std::expected<std::shared_ptr<vk::raii::Pipeline>,
                              PipelineError>
  getOrCreate(const DynamicPipelineInfo &info,
              const std::shared_ptr<vk::raii::Device> &device);

  // Get or create a classic render-pass pipeline
  [[nodiscard]] std::expected<std::shared_ptr<vk::raii::Pipeline>,
                              PipelineError>
  getOrCreate(const StaticPipelineInfo &info,
              const std::shared_ptr<vk::raii::Device> &device);

  // Invalidate all pipelines that use a given shader (call after hot-reload)
  void invalidateShader(const shaders::Shader *tag);

  // Drop all pipelines for a device (call before device destruction)
  void invalidateDevice(const std::shared_ptr<vk::raii::Device> &device);
};

} // namespace graphics::vulkan::pipelines
