
import graphics;
import std.compat;
import vulkan;

int main() {

    auto inst = std::make_shared<graphics::vulkan::Instance>();
    inst->initialize();

    graphics::vulkan::DeviceConfig conf{nullptr, false, 0};

    graphics::vulkan::DeviceManager d_mam;
    d_mam.initialize(inst, conf);

    return 0;
}
