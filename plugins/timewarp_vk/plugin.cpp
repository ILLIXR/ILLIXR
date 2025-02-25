#include "plugin.hpp"

[[maybe_unused]] timewarp_vk_plugin::timewarp_vk_plugin(const std::string& name, phonebook* pb)
    : threadloop{name, pb}
    , timewarp_{std::make_shared<timewarp_vk>(phonebook_)} {
    pb->register_impl<vulkan::timewarp>(std::static_pointer_cast<vulkan::timewarp>(timewarp_));
}

void timewarp_vk_plugin::_p_one_iteration() {
    auto fps = timewarp_->num_record_calls_.exchange(0) / 2; // two eyes
    auto ups = timewarp_->num_update_uniforms_calls_.exchange(0);

    // std::cout << "timewarp_vk: cb records: " << fps << ", uniform updates: " << ups << std::endl;
}

threadloop::skip_option timewarp_vk_plugin::_p_should_skip() {
    // Get the current time in milliseconds
    auto now =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

    // Only print every 1 second
    if (now - last_print_ < 1000) {
        return skip_option::skip_and_yield;
    } else {
        last_print_ = now;
        return skip_option::run;
    }
}

PLUGIN_MAIN(timewarp_vk_plugin)
