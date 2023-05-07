#include "common/data_format.hpp"
#include "common/error_util.hpp"
#include "common/global_module_defs.hpp"
#include "common/math_util.hpp"
#include "common/pose_prediction.hpp"
#include "common/switchboard.hpp"
#include "common/threadloop.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <future>
#include <iostream>
#include <thread>

using namespace ILLIXR;

class vkdemo : public threadloop {
public:
    vkdemo(std::string name_, phonebook* pb_)
        : threadloop{name_, pb_}
        , sb{pb->lookup_impl<switchboard>()}
        , pp{pb->lookup_impl<pose_prediction>()}
        , _m_clock{pb->lookup_impl<RelativeClock>()}
        , _m_vsync{sb->get_reader<switchboard::event_wrapper<time_point>>("vsync_estimate")} { }

    void initialize() {

    }

private:
    const std::shared_ptr<switchboard>                                sb;
    const std::shared_ptr<pose_prediction>                            pp;
    const std::shared_ptr<const RelativeClock>                        _m_clock;
    const switchboard::reader<switchboard::event_wrapper<time_point>> _m_vsync;
};

class vkdemo_plugin : public plugin {
public:
    vkdemo_plugin(const std::string& name, phonebook* pb)
        : plugin{name, pb},
        vkd{std::make_shared<vkdemo>(pb)} {
        pb->register_impl<vkdemo>(std::static_pointer_cast<vkdemo>(vkd));
    }

    virtual void start() override {
        vkd->initialize();
    }

private:
    std::shared_ptr<vkdemo> vkd;
};

PLUGIN_MAIN(vkdemo_plugin)
