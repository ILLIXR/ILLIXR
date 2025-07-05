#pragma once

#include "illixr/plugin.hpp"
#include "illixr/threadloop.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/vk/display_provider.hpp"
#include "illixr/vk/vulkan_utils.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/data_format/frame.hpp"
#include <filesystem>
#include <string>
#include <vector>
#include <stack>
#include "illixr/vk/third_party/vk_mem_alloc.h"

namespace ILLIXR {

class frame_logger : public threadloop {
public:
    frame_logger(std::string name, phonebook* pb);

    void _p_thread_setup() override;
    void _p_one_iteration() override;
    void start() override;
    void stop() override;
    void initialize();

private:
    void save_frame(const VkImage& image, uint32_t width, uint32_t height, uint32_t frame_number, bool left, VkFence fence, const std::string& output_directory, int index);
    std::shared_ptr<vulkan::display_provider> display_provider_;
    std::shared_ptr<switchboard> switchboard_;
    const phonebook* phonebook_;
    switchboard::buffered_reader<data_format::frame_to_be_saved> frame_reader_;
    bool is_running_{false};

    std::stack<std::function<void()>> deletion_queue_;
    VmaAllocator                      vma_allocator_{};
    VkCommandPool                     command_pool_{};

    std::shared_ptr<spdlog::logger>   log_;
    int countdown = 40; // Countdown to wait for the app to be ready
};

} // namespace ILLIXR 