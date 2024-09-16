#pragma once

#include "illixr/data_format.hpp"
#include "illixr/opencv_data_types.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/plugin.hpp"
#include "illixr/switchboard.hpp"

#include <boost/filesystem.hpp>
#include <fstream>

namespace ILLIXR {
class record_imu_cam : public plugin {
public:
    [[maybe_unused]] record_imu_cam(const std::string& name, phonebook* pb);
    void dump_data(const switchboard::ptr<const imu_type>& datum);
    ~record_imu_cam() override;

private:
    static boost::filesystem::path get_record_data_path();

    std::ofstream                      imu_wt_file_;
    std::ofstream                      cam0_wt_file_;
    std::ofstream                      cam1_wt_file_;
    const std::shared_ptr<switchboard> switchboard_;

    switchboard::buffered_reader<cam_type> cam_;

    const boost::filesystem::path record_data_;
    const boost::filesystem::path cam0_data_dir_;
    const boost::filesystem::path cam1_data_dir_;
};

}