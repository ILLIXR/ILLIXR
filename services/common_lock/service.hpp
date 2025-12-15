#pragma once

#include "illixr/common_lock.hpp"
#include "illixr/plugin.hpp"
#include "illixr/switchboard.hpp"

namespace ILLIXR {
class common_lock_impl : public common_lock {
public:
    common_lock_impl(const phonebook* const pb);

    void get_lock();

    void release_lock();

    void wait_illixr();

    void wait_monado();

private:
    const std::shared_ptr<switchboard> switchboard_;
    bool illixr_monado_ = false;
};

class common_lock_plugin : public plugin {
public:
    [[maybe_unused]] common_lock_plugin(const std::string& name, phonebook* pb)
            : plugin{name, pb} {
        pb->register_impl<common_lock>(
                std::static_pointer_cast<common_lock>(std::make_shared<common_lock_impl>(pb)));
    }
};

}