#pragma once

#include "illixr/plugin.hpp"

#include "illixr/data_format.hpp"
#include "illixr/error_util.hpp"
#include "illixr/global_module_defs.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/switchboard.hpp"

namespace ILLIXR {
class offload_data : public plugin {
public:
    [[maybe_unused]] offload_data(const std::string& name, phonebook* pb);
    void callback(const switchboard::ptr<const texture_pose>& datum);
    ~offload_data() override;
private:
    void write_metadata();
    void write_data_to_disk();

    const std::shared_ptr<switchboard>                switchboard_;
    std::vector<long>                                 _time_seq;
    std::vector<switchboard::ptr<const texture_pose>> _offload_data_container;

    int         percent_;
    int         img_idx_;
    bool        enable_offload_;
    bool        is_success_;
    std::string obj_dir_;

};
}