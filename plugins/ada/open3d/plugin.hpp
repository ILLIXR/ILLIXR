#pragma once

#include "illixr/data_format/mesh.hpp"
#include "illixr/data_format/scene_reconstruction.hpp"
#include "illixr/plugin.hpp"
#include "illixr/switchboard.hpp"

#include <fstream>
#include <memory>
#include <mutex>

namespace ILLIXR {
namespace ada_open3d {
    class reconstructor;
}

class open3d_reconstruction : public plugin {
public:
    open3d_reconstruction(const std::string& name, phonebook* pb);
    ~open3d_reconstruction() override;

private:
    void process_frame(const switchboard::ptr<const data_format::scene_recon_type>& datum);
    void extract(unsigned scene_id);

    const std::shared_ptr<switchboard>          switchboard_;
    switchboard::writer<data_format::mesh_type> mesh_writer_;
    switchboard::writer<data_format::vb_type>   block_writer_;
    std::unique_ptr<ada_open3d::reconstructor>  state_;
    unsigned                                    frame_ = 0;
    unsigned                                    interval_;
    unsigned                                    workers_;
    std::mutex                                  mutex_;
    std::ofstream                               latency_;
};
} // namespace ILLIXR
