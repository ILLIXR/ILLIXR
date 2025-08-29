#pragma once

#include "illixr/data_format/mesh.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/plugin.hpp"
#include "illixr/relative_clock.hpp"
#include "illixr/switchboard.hpp"


namespace ILLIXR {

class mesh_compression : public plugin {
public:
    [[maybe_unused]] mesh_compression(const std::string& name_, phonebook* pb_);

    ~mesh_compression() override;

    void process_mesh(switchboard::ptr<const data_format::mesh_type> datum);

private:
    uint mesh_count_;

    // ILLIXR related variables
    const std::shared_ptr<switchboard>          switchboard_;
    std::shared_ptr<switchboard::writer<data_format::mesh_type>> compressed_mesh_;
    std::vector<std::thread> compress_thread_;
};

} // namespace ILLIXR
