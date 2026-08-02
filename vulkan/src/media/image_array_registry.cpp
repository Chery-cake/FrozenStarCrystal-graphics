module;

#include <stdexcept>

module graphics.vulkan.media;

import std.compat;
import vulkan;

namespace graphics::vulkan::media {

// ============================================================
// ImageArrayRegistry implementation
// ============================================================

void ImageArrayRegistry::defineChannel(ChannelDesc desc) {
    std::lock_guard lock(mutex_);
    uint32_t binding = desc.binding;
    auto [it, inserted] = channels_.try_emplace(binding);
    if (inserted) {
        it->second.desc = std::move(desc);
        it->second.slots.resize(it->second.desc.maxSlots, vk::ImageView{});
    }
}

void ImageArrayRegistry::init(const vk::raii::Device &device) {
    std::lock_guard lock(mutex_);

    if (initialized_) {
        throw std::logic_error(
            "ImageArrayRegistry::init() called more than once");
    }
    if (channels_.empty()) {
        throw std::logic_error(
            "ImageArrayRegistry::init() called with no channels defined");
    }

    // Build sorted channel list so bindings are in ascending order.
    std::vector<uint32_t> sortedBindings;
    sortedBindings.reserve(channels_.size());
    for (auto &[b, _] : channels_) {
        sortedBindings.push_back(b);
    }
    std::sort(sortedBindings.begin(), sortedBindings.end());

    std::vector<vk::DescriptorSetLayoutBinding> bindings;
    std::vector<vk::DescriptorBindingFlags>     bindingFlags;
    bindings.reserve(sortedBindings.size());
    bindingFlags.reserve(sortedBindings.size());

    uint32_t totalSlots = 0;
    for (uint32_t b : sortedBindings) {
        auto &state = channels_.at(b);
        vk::DescriptorSetLayoutBinding lb{};
        lb.binding         = b;
        lb.descriptorType  = vk::DescriptorType::eCombinedImageSampler;
        lb.descriptorCount = state.desc.maxSlots;
        lb.stageFlags      = vk::ShaderStageFlagBits::eAll;
        bindings.push_back(lb);

        bindingFlags.push_back(
            vk::DescriptorBindingFlagBits::ePartiallyBound |
            vk::DescriptorBindingFlagBits::eUpdateAfterBind);

        totalSlots += state.desc.maxSlots;
    }

    vk::DescriptorSetLayoutBindingFlagsCreateInfo flagsInfo{};
    flagsInfo.setBindingFlags(bindingFlags);

    vk::DescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.flags =
        vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool;
    layoutInfo.setBindings(bindings);
    layoutInfo.setPNext(&flagsInfo);

    layout_ = std::make_unique<vk::raii::DescriptorSetLayout>(
        device, layoutInfo);

    vk::DescriptorPoolSize poolSize{vk::DescriptorType::eCombinedImageSampler,
                                    totalSlots};
    vk::DescriptorPoolCreateInfo poolInfo{};
    poolInfo.flags   = vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind;
    poolInfo.maxSets = 1;
    poolInfo.setPoolSizes(poolSize);
    pool_ = std::make_unique<vk::raii::DescriptorPool>(device, poolInfo);

    vk::DescriptorSetAllocateInfo allocInfo{};
    allocInfo.descriptorPool = **pool_;
    allocInfo.setSetLayouts(**layout_);
    auto sets = device.allocateDescriptorSets(allocInfo);
    set_ = std::make_unique<vk::raii::DescriptorSet>(std::move(sets[0]));

    initialized_ = true;
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
        return {viewIt->second, binding};
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

    state.slots[slot]      = view;
    state.viewToSlot[view] = slot;
    state.dirtySlots.insert(slot);

    return {slot, binding};
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

void ImageArrayRegistry::commitDescriptors(const vk::raii::Device &device,
                                           vk::Sampler sampler) {
    std::lock_guard lock(mutex_);
    if (!initialized_) {
        return;
    }

    // We keep the image-info structs alive for the duration of the call.
    std::vector<vk::DescriptorImageInfo> imageInfos;
    std::vector<vk::WriteDescriptorSet>  writes;

    for (auto &[binding, state] : channels_) {
        for (uint32_t slot : state.dirtySlots) {
            vk::ImageView view = state.slots[slot];
            if (!view) {
                continue; // null slots are left stale (PARTIALLY_BOUND)
            }

            vk::DescriptorImageInfo imgInfo{};
            imgInfo.sampler     = sampler;
            imgInfo.imageView   = view;
            imgInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            imageInfos.push_back(imgInfo);

            vk::WriteDescriptorSet write{};
            write.dstSet          = **set_;
            write.dstBinding      = binding;
            write.dstArrayElement = slot;
            write.descriptorType  = vk::DescriptorType::eCombinedImageSampler;
            write.descriptorCount = 1;
            write.pImageInfo      = &imageInfos.back();
            writes.push_back(write);
        }
        state.dirtySlots.clear();
    }

    if (!writes.empty()) {
        device.updateDescriptorSets(writes, {});
    }
}

vk::DescriptorSet ImageArrayRegistry::bindlessSet() const {
    return set_ ? **set_ : vk::DescriptorSet{};
}

vk::DescriptorSetLayout ImageArrayRegistry::bindlessLayout() const {
    return layout_ ? **layout_ : vk::DescriptorSetLayout{};
}

// ============================================================
// BufferArrayRegistry implementation
// ============================================================

void BufferArrayRegistry::defineChannel(ChannelDesc desc) {
    std::lock_guard lock(mutex_);
    uint32_t binding = desc.binding;
    auto [it, inserted] = channels_.try_emplace(binding);
    if (inserted) {
        it->second.desc = std::move(desc);
        it->second.slots.resize(it->second.desc.maxSlots);
    }
}

void BufferArrayRegistry::init(const vk::raii::Device &device) {
    std::lock_guard lock(mutex_);

    if (initialized_) {
        throw std::logic_error(
            "BufferArrayRegistry::init() called more than once");
    }
    if (channels_.empty()) {
        throw std::logic_error(
            "BufferArrayRegistry::init() called with no channels defined");
    }

    std::vector<uint32_t> sortedBindings;
    sortedBindings.reserve(channels_.size());
    for (auto &[b, _] : channels_) {
        sortedBindings.push_back(b);
    }
    std::sort(sortedBindings.begin(), sortedBindings.end());

    std::vector<vk::DescriptorSetLayoutBinding> bindings;
    std::vector<vk::DescriptorBindingFlags>     bindingFlags;
    bindings.reserve(sortedBindings.size());
    bindingFlags.reserve(sortedBindings.size());

    uint32_t totalSlots = 0;
    for (uint32_t b : sortedBindings) {
        auto &state = channels_.at(b);
        vk::DescriptorSetLayoutBinding lb{};
        lb.binding         = b;
        lb.descriptorType  = vk::DescriptorType::eStorageBuffer;
        lb.descriptorCount = state.desc.maxSlots;
        lb.stageFlags      = vk::ShaderStageFlagBits::eAll;
        bindings.push_back(lb);

        bindingFlags.push_back(
            vk::DescriptorBindingFlagBits::ePartiallyBound |
            vk::DescriptorBindingFlagBits::eUpdateAfterBind);

        totalSlots += state.desc.maxSlots;
    }

    vk::DescriptorSetLayoutBindingFlagsCreateInfo flagsInfo{};
    flagsInfo.setBindingFlags(bindingFlags);

    vk::DescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.flags =
        vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool;
    layoutInfo.setBindings(bindings);
    layoutInfo.setPNext(&flagsInfo);

    layout_ = std::make_unique<vk::raii::DescriptorSetLayout>(
        device, layoutInfo);

    vk::DescriptorPoolSize poolSize{vk::DescriptorType::eStorageBuffer,
                                    totalSlots};
    vk::DescriptorPoolCreateInfo poolInfo{};
    poolInfo.flags   = vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind;
    poolInfo.maxSets = 1;
    poolInfo.setPoolSizes(poolSize);
    pool_ = std::make_unique<vk::raii::DescriptorPool>(device, poolInfo);

    vk::DescriptorSetAllocateInfo allocInfo{};
    allocInfo.descriptorPool = **pool_;
    allocInfo.setSetLayouts(**layout_);
    auto sets = device.allocateDescriptorSets(allocInfo);
    set_ = std::make_unique<vk::raii::DescriptorSet>(std::move(sets[0]));

    initialized_ = true;
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
        state.slots[existing] = {buffer, offset, range};
        state.dirtySlots.insert(existing);
        return {existing, binding};
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

    state.slots[slot]          = {buffer, offset, range};
    state.bufferToSlot[buffer] = slot;
    state.dirtySlots.insert(slot);

    return {slot, binding};
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
    if (!initialized_) {
        return;
    }

    std::vector<vk::DescriptorBufferInfo> bufferInfos;
    std::vector<vk::WriteDescriptorSet>   writes;

    for (auto &[binding, state] : channels_) {
        for (uint32_t slot : state.dirtySlots) {
            const BufferEntry &entry = state.slots[slot];
            if (!entry.buffer) {
                continue;
            }

            vk::DescriptorBufferInfo bufInfo{};
            bufInfo.buffer = entry.buffer;
            bufInfo.offset = entry.offset;
            bufInfo.range  = entry.range ? entry.range : vk::WholeSize;
            bufferInfos.push_back(bufInfo);

            vk::WriteDescriptorSet write{};
            write.dstSet          = **set_;
            write.dstBinding      = binding;
            write.dstArrayElement = slot;
            write.descriptorType  = vk::DescriptorType::eStorageBuffer;
            write.descriptorCount = 1;
            write.pBufferInfo     = &bufferInfos.back();
            writes.push_back(write);
        }
        state.dirtySlots.clear();
    }

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
