#pragma once

#include "illixr/plugin.hpp"
#include "illixr/vk/display_provider.hpp"
#include "illixr/vk/vulkan_utils.hpp"
#include "illixr/switchboard.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace ILLIXR {

class frame_logger : public plugin {
public:
    frame_logger(std::string name, phonebook* pb);
    virtual ~frame_logger() override;

    void start() override;
    void stop() override;

private:
    void save_frame(const VkImage& image, uint32_t width, uint32_t height, uint32_t frame_number);
    void create_output_directory();
    void handle_frame(const switchboard::event_wrapper<time_point>& event);

    const std::shared_ptr<vulkan::display_provider> display_provider_;
    std::shared_ptr<switchboard> switchboard_;
    switchboard::reader<switchboard::event_wrapper<time_point>> frame_reader_;
    std::filesystem::path output_directory_;
    uint32_t frame_counter_{0};
    bool is_running_{false};
};

} // namespace ILLIXR 