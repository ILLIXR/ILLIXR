#pragma once

#include "illixr/threadloop.hpp"
#include "timewarp_vk.hpp"

namespace ILLIXR {

class timewarp_vk_plugin : public threadloop {
public:
    [[maybe_unused]] timewarp_vk_plugin(const std::string& name, phonebook* pb);
    void        _p_one_iteration() override;
    skip_option _p_should_skip() override;

private:
    std::shared_ptr<timewarp_vk> timewarp_;

    int64_t last_print_ = 0;
};

} // namespace ILLIXR
