#pragma once

#include "illixr/phonebook.hpp"

namespace ILLIXR {

class graphics_backend : public phonebook::service {
public:
    // Vulkan object creation

    /// Destruction: delete any remaining Vulkan objects.
    virtual ~graphics_backend() override = default;
};

} // namespace ILLIXR