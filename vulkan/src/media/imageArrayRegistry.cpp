module;

module graphics.vulkan.media;

import std.compat;
import vulkan;

namespace graphics::vulkan::media {

ImageArrayRegistry::ImageArrayRegistry(
    const std::shared_ptr<vk::raii::Device> &device, ChannelDesc &desc)
    : device_(device) {

  defineChannel(desc);

  // Build sorted channel list so bindings are in ascending order.
  std::vector<uint32_t> sortedBindings =
      channels_ | std::views::keys | std::ranges::to<std::vector<uint32_t>>();
  std::ranges::sort(sortedBindings, std::less{});

  std::vector<vk::DescriptorSetLayoutBinding> bindings;
  std::vector<vk::DescriptorBindingFlags> bindingFlags;
  bindings.reserve(sortedBindings.size());
  bindingFlags.reserve(sortedBindings.size());

  uint32_t totalSlots = 0;

  std::ranges::for_each(
      sortedBindings |
          std::views::transform(
              [&channels = channels_](uint32_t i) { return channels.at(i); }) |
          std::views::enumerate,
      [&bindings, &bindingFlags, &totalSlots](const auto &pair) {
        auto &&[i, state] = pair;
        vk::DescriptorSetLayoutBinding lb{};
        lb.binding = static_cast<uint32_t>(i);
        lb.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        lb.descriptorCount = state.desc.maxSlots;
        lb.stageFlags = vk::ShaderStageFlagBits::eAll;
        bindings.push_back(lb);

        bindingFlags.push_back(vk::DescriptorBindingFlagBits::ePartiallyBound |
                               vk::DescriptorBindingFlagBits::eUpdateAfterBind);

        totalSlots += state.desc.maxSlots;
      });

  vk::DescriptorSetLayoutBindingFlagsCreateInfo flagsInfo{};
  flagsInfo.setBindingFlags(bindingFlags);

  vk::DescriptorSetLayoutCreateInfo layoutInfo{};
  layoutInfo.flags =
      vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool;
  layoutInfo.setBindings(bindings);
  layoutInfo.setPNext(&flagsInfo);

  layout_ =
      std::make_unique<vk::raii::DescriptorSetLayout>(*device, layoutInfo);

  vk::DescriptorPoolSize poolSize{vk::DescriptorType::eCombinedImageSampler,
                                  totalSlots};
  vk::DescriptorPoolCreateInfo poolInfo{};
  poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind;
  poolInfo.maxSets = 1;
  poolInfo.setPoolSizes(poolSize);
  pool_ = std::make_unique<vk::raii::DescriptorPool>(*device, poolInfo);

  vk::DescriptorSetAllocateInfo allocInfo{};
  allocInfo.descriptorPool = **pool_;
  allocInfo.setSetLayouts(**layout_);
  auto sets = device->allocateDescriptorSets(allocInfo);
  set_ = std::make_unique<vk::raii::DescriptorSet>(std::move(sets[0]));
}

void ImageArrayRegistry::defineChannel(ChannelDesc desc) {
  std::lock_guard lock(mutex_);
  uint32_t binding = desc.binding;
  auto [it, inserted] = channels_.try_emplace(binding);
  if (inserted) {
    it->second.desc = std::move(desc);
    it->second.slots.resize(it->second.desc.maxSlots, vk::ImageView{});
  }
}

ImageHandle ImageArrayRegistry::registerImage(uint32_t binding,
                                              vk::ImageView view) {
  std::lock_guard lock(mutex_);

  auto it = channels_.find(binding);
  if (it == channels_.end()) {
    return {};
  }
  ChannelState &state = it->second;

  // Dedup: return existing handle if this view is already registered.
  auto viewIt = state.viewToSlot.find(view);
  if (viewIt != state.viewToSlot.end()) {
    return {.index = viewIt->second, .binding = binding};
  }

  // Allocate a slot.
  uint32_t slot;
  if (!state.freeList.empty()) {
    slot = state.freeList.back();
    state.freeList.pop_back();
  } else {
    if (state.nextSlot >= state.desc.maxSlots) {
      return {}; // channel is full
    }
    slot = state.nextSlot++;
  }

  state.slots[slot] = view;
  state.viewToSlot[view] = slot;
  state.dirtySlots.insert(slot);

  return {.index = slot, .binding = binding};
}

void ImageArrayRegistry::removeImage(ImageHandle handle) {
  if (!handle.valid()) {
    return;
  }
  std::lock_guard lock(mutex_);

  auto it = channels_.find(handle.binding);
  if (it == channels_.end()) {
    return;
  }
  ChannelState &state = it->second;
  if (handle.index >= state.desc.maxSlots) {
    return;
  }

  vk::ImageView view = state.slots[handle.index];
  if (view) {
    state.viewToSlot.erase(view);
  }
  state.slots[handle.index] = vk::ImageView{};
  state.freeList.push_back(handle.index);
  // The slot is dirty but null — commitDescriptors() will skip null views,
  // leaving the GPU entry stale (harmless under PARTIALLY_BOUND).
  state.dirtySlots.erase(handle.index);
}

void ImageArrayRegistry::commitDescriptors(vk::Sampler sampler) {
  std::lock_guard lock(mutex_);

  // We keep the image-info structs alive for the duration of the call.
  std::vector<vk::DescriptorImageInfo> imageInfos;
  std::vector<vk::WriteDescriptorSet> writes =
      channels_ |
      std::views::transform([&sampler, &imageInfos,
                             &set = set_](const auto &pair) {
        auto &&[binding, state] = pair;
        return std::views::transform(
            std::views::filter(
                state.dirtySlots,
                [&state](uint32_t i) { return state.slots[i] != nullptr; }),
            [&state, &imageInfos, &sampler, &set, &binding](uint32_t slot) {
              vk::ImageView view = state.slots[slot];
              imageInfos.emplace_back(sampler, view,
                                      vk::ImageLayout::eShaderReadOnlyOptimal);

              vk::WriteDescriptorSet write{};
              write.dstSet = **set;
              write.dstBinding = binding;
              write.dstArrayElement = slot;
              write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
              write.descriptorCount = 1;
              write.pImageInfo = &imageInfos.back();
              return write;
            });
      }) |
      std::views::join | std::ranges::to<std::vector<vk::WriteDescriptorSet>>();

  std::ranges::for_each(channels_, [](auto &pair) {
    auto &&[_, state] = pair;
    state.dirtySlots.clear();
  });

  if (!writes.empty()) {
    device_->updateDescriptorSets(writes, {});
  }
}

vk::DescriptorSet ImageArrayRegistry::bindlessSet() const {
  return set_ ? **set_ : vk::DescriptorSet{};
}

vk::DescriptorSetLayout ImageArrayRegistry::bindlessLayout() const {
  return layout_ ? **layout_ : vk::DescriptorSetLayout{};
}

} // namespace graphics::vulkan::media
