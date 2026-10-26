#pragma once

#include "illixr/plugin.hpp"
#include "illixr/switchboard.hpp"

#include "illixr/data_format/openxr_data.hpp"
#include "illixr/data_format/openxr_payloads.h"

namespace ILLIXR {

class test_oxr : public plugin {
public:
    test_oxr(const std::string& name, phonebook* pb);
    
    void start() override;
private:
    void process_message(const ixr_foo_data& datum);
    
    const std::shared_ptr<switchboard>    switchboard_;
    
    std::optional<switchboard::writer<ixr_event<ixr_foo_data>>> writer_;
};
}
