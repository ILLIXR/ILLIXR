#include "plugin.hpp"

#include <spdlog/spdlog.h>

using namespace ILLIXR;

test_oxr::test_oxr(const std::string& name, phonebook* pb)
    : plugin{name, pb}
    , switchboard_{phonebook_->lookup_impl<switchboard>()} {
    switchboard_->schedule<ixr_event<ixr_foo_data>>(id_, "oxr_input", [this](const switchboard::ptr<const ixr_event<ixr_foo_data>>& datum, size_t) {
        process_message(datum->data); 
    });
}
 
void test_oxr::start() {
    writer_.emplace(switchboard_->get_writer<ixr_event<ixr_foo_data>>("oxr_output"));
    plugin::start();
}
void test_oxr::process_message(const ixr_foo_data& datum) {
    spdlog::get("illixr")->debug("[test_oxr] Rx: {:.3f}, {:.3f}, {:.3f}, {:.3f}  id: {}", datum.values[0], datum.values[1],
                                 datum.values[2], datum.values[3], datum.count);
    ixr_foo_data new_data{};
    new_data.values[0] = 2.f * datum.values[0];
    new_data.values[1] = 2.f * datum.values[1];
    new_data.values[2] = 4.f * datum.values[2];
    new_data.values[3] = 0.5f * datum.values[3];
    new_data.count = datum.count;
    spdlog::get("illixr")->debug("[test_oxr] Tx: {:.3f}, {:.3f}, {:.3f}, {:.3f}  id: {}", new_data.values[0], new_data.values[1],
                                 new_data.values[2], new_data.values[3], new_data.count);
    
    writer_->put(std::make_shared<ixr_event<ixr_foo_data>>(new_data, now_monotonic_ns()));
}

PLUGIN_MAIN(test_oxr)