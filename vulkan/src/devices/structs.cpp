module;

module graphics.vulkan.devices;

import std.compat;

namespace graphics::vulkan::devices {

void FenceWaiter::loop() {
  fenceWaiter = std::jthread([&mtx = mtx,
                              &sub = submissions](const std::stop_token &stok) {
    while (!stok.stop_requested()) {
      // Gather all fences we need to wait on
      std::unordered_map<std::shared_ptr<Device>, std::vector<vk::Fence>>
          perDeviceFences;
      {
        std::unique_lock lock(mtx);
        if (sub.empty()) {
          lock.unlock();
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
          continue;
        }

        perDeviceFences =
            sub | std::views::transform([](const auto &pair) {
              auto &&[device, vec] = pair;
              return std::pair{
                  device,
                  vec | std::views::transform([](const CoroutineSubmission &s) {
                    return *s.fence;
                  }) | std::ranges::to<std::vector<vk::Fence>>()};
            }) |
            std::ranges::to<std::unordered_map<std::shared_ptr<Device>,
                                               std::vector<vk::Fence>>>();
      }

      // Wait for any of them (or all, we can wait for all with a
      // timeout)
      constexpr auto fenceWaitTimeout = 1'000'000; // 1 ms

      std::erase_if(perDeviceFences, [](const auto &pair) {
        auto &&[device, vec] = pair;
        if (vec.empty()) {
          return true;
        }
        auto result = device->getRaiiDevice().waitForFences(
            vec, false, fenceWaitTimeout); // wait up to 1ms
        return (result != vk::Result::eSuccess &&
                result != vk::Result::eTimeout);
      });

      {
        std::unique_lock lock(mtx);
        std::erase_if(sub, [&perDeviceFences](const auto &pair) {
          return perDeviceFences.find(pair.first) == perDeviceFences.end();
        });
      }

      std::unique_lock lock(mtx);
      std::ranges::for_each(sub, [](auto &pair) {
        auto &&[device, vec] = pair;
        std::erase_if(vec, [&device](CoroutineSubmission &s) {
          if (s.fence != nullptr && device->getDevice().getFenceStatus(
                                        *s.fence) == vk::Result::eSuccess) {
            device->submit([h = s.continuation] { h.resume(); });
            return true;
          }
          return false;
        });
      });
    }
    // on stop: signal any remaining promises with an exception?
  });
}

} // namespace graphics::vulkan::devices
