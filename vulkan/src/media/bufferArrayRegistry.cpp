module;

module graphics.vulkan.media;

import std.compat;
import vulkan;

namespace graphics::vulkan::media {

BufferArrayRegistry::BufferArrayRegistry(
    const std::shared_ptr<vk::raii::Device> &device, ChannelDesc &desc)
    : device_(device) {

  defineChannel(desc);

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
        lb.binding = i;
        lb.descriptorType = vk::DescriptorType::eStorageBuffer;
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

  vk::DescriptorPoolSize poolSize{vk::DescriptorType::eStorageBuffer,
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

void BufferArrayRegistry::defineChannel(ChannelDesc desc) {
  std::lock_guard lock(mutex_);
  uint32_t binding = desc.binding;
  auto [it, inserted] = channels_.try_emplace(binding);
  if (inserted) {
    it->second.desc = std::move(desc);
    it->second.slots.resize(it->second.desc.maxSlots);
  }
}

BufferHandle BufferArrayRegistry::registerBuffer(uint32_t binding,
                                                 vk::Buffer buffer,
                                                 vk::DeviceSize offset,
                                                 vk::DeviceSize range) {
  std::lock_guard lock(mutex_);

  auto it = channels_.find(binding);
  if (it == channels_.end()) {
    return {};
  }
  ChannelState &state = it->second;

  // Dedup by buffer handle (offset/range changes are not deduped).
  auto bufIt = state.bufferToSlot.find(buffer);
  if (bufIt != state.bufferToSlot.end()) {
    uint32_t existing = bufIt->second;
    // Update offset/range in case they changed, and mark dirty.
    state.slots[existing] = {
        .buffer = buffer, .offset = offset, .range = range};
    state.dirtySlots.insert(existing);
    return {.index = existing, .binding = binding};
  }

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

  state.slots[slot] = {.buffer = buffer, .offset = offset, .range = range};
  state.bufferToSlot[buffer] = slot;
  state.dirtySlots.insert(slot);

  return {.index = slot, .binding = binding};
}

void BufferArrayRegistry::removeBuffer(BufferHandle handle) {
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

  vk::Buffer buf = state.slots[handle.index].buffer;
  if (buf) {
    state.bufferToSlot.erase(buf);
  }
  state.slots[handle.index] = {};
  state.freeList.push_back(handle.index);
  state.dirtySlots.erase(handle.index);
}

void BufferArrayRegistry::commitDescriptors(const vk::raii::Device &device) {
  std::lock_guard lock(mutex_);

  std::vector<vk::DescriptorBufferInfo> bufferInfos;
  std::vector<vk::WriteDescriptorSet> writes =
      channels_ |
      std::views::transform([&set = set_, &bufferInfos](const auto &pair) {
        auto &&[binding, state] = pair;
        return std::views::transform(
            std::views::filter(state.dirtySlots,
                               [&state](uint32_t i) {
                                 return state.slots[i].buffer != nullptr;
                               }),
            [&state, &set, &binding, &bufferInfos](uint32_t slot) {
              const BufferEntry &entry = state.slots[slot];

              vk::DescriptorBufferInfo bufInfo{};
              bufInfo.buffer = entry.buffer;
              bufInfo.offset = entry.offset;
              bufInfo.range = entry.range ? entry.range : vk::WholeSize;
              bufferInfos.push_back(bufInfo);

              vk::WriteDescriptorSet write{};
              write.dstSet = **set;
              write.dstBinding = binding;
              write.dstArrayElement = slot;
              write.descriptorType = vk::DescriptorType::eStorageBuffer;
              write.descriptorCount = 1;
              write.pBufferInfo = &bufferInfos.back();
              return write;
            });
      }) |
      std::views::join | std::ranges::to<std::vector<vk::WriteDescriptorSet>>();

  std::ranges::for_each(channels_, [](auto &pair) {
    auto &&[_, state] = pair;
    state.dirtySlots.clear();
  });

  if (!writes.empty()) {
    device.updateDescriptorSets(writes, {});
  }
}

vk::DescriptorSet BufferArrayRegistry::bindlessSet() const {
  return set_ ? **set_ : vk::DescriptorSet{};
}

vk::DescriptorSetLayout BufferArrayRegistry::bindlessLayout() const {
  return layout_ ? **layout_ : vk::DescriptorSetLayout{};
}

} // namespace graphics::vulkan::media
