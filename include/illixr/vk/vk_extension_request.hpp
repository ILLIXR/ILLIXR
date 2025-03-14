#pragma once

#include <string>
#include <vector>

namespace ILLIXR::vulkan {
class vk_extension_request {
public:
    virtual std::vector<const char*> get_required_instance_extensions() = 0;
    virtual std::vector<const char*> get_required_devices_extensions()  = 0;
};
} // namespace ILLIXR::vulkan
