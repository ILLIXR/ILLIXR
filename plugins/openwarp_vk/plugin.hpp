#pragma once

#include "illixr/phonebook.hpp"
#include "illixr/threadloop.hpp"
#include "openwarp_vk.hpp"

namespace ILLIXR {

class openwarp_vk_plugin : public threadloop {
public:
    [[maybe_unused]] openwarp_vk_plugin(const std::string& name, phonebook* pb);
    void        _p_one_iteration() override;
    skip_option _p_should_skip() override;

private:
    std::shared_ptr<openwarp_vk> timewarp_;
    int64_t                      last_print_ = 0;
};

} // namespace ILLIXR
