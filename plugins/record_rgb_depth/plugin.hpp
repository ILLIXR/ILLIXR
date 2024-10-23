#pragma once

#include "illixr/opencv_data_types.hpp"
#include "illixr/plugin.hpp"
#include "illixr/switchboard.hpp"

#include <boost/filesystem.hpp>
#include <fstream>

namespace ILLIXR {
class record_rgb_depth : public plugin {
public:
    [[maybe_unused]] record_rgb_depth(const std::string& name, phonebook* pb);
    void dump_data(const switchboard::ptr<const rgb_depth_type>& datum);
    ~record_rgb_depth() override;

private:
    static boost::filesystem::path get_record_data_path();

    std::ofstream                      rgb_wt_file_;
    std::ofstream                      depth_wt_file_;
    const std::shared_ptr<switchboard> switchboard_;

    const boost::filesystem::path record_data_;
    const boost::filesystem::path rgb_data_dir_;
    const boost::filesystem::path depth_data_dir_;
};

} // namespace ILLIXR