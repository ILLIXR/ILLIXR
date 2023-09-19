//
// Created by hongyig3 on 9/19/23.
//

#ifndef ILLIXR_VK_EXTENSION_REQUEST_H
#define ILLIXR_VK_EXTENSION_REQUEST_H

#include <string>
#include <vector>

namespace ILLIXR::vulkan {
class vk_extension_request {
public:
    virtual std::vector<const char*> get_required_instance_extensions() = 0;
    virtual std::vector<const char*> get_required_devices_extensions() = 0;
};
}


#endif // ILLIXR_VK_EXTENSION_REQUEST_H
