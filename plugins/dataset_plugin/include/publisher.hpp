#pragma once

#include "illixr/common/data_format.hpp"
#include "illixr/common/phonebook.hpp"
#include "illixr/common/relative_clock.hpp"
#include "illixr/common/threadloop.hpp"
#include "dataset_loader.hpp"

#include <chrono> // for std::chrono::nanoseconds
#include <memory> // for std::shared_ptr
#include <thread>  // for std::this_thread::sleep_for
#include <utility> // for std::move

using namespace ILLIXR;

class Publisher : public threadloop {
public:
    Publisher(std::string name, phonebook* pb)
        : threadloop{name, pb}
        , sb{pb->lookup_impl<switchboard>()}
        , data_emitter{sb->get_writer<ground_truth_type>("image")
                     , sb->get_writer<ground_truth_type>("imu")
                     , sb->get_writer<ground_truth_type>("pose")
                     , sb->get_writer<ground_truth_type>("ground truth")
          }
        , m_rtc{pb->lookup_impl<RelativeClock>()} { }
        // : threadloop{name, pb}
        // , sb{pb->lookup_impl<switchboard>()}
        // , m_dataset_loader{std::shared_ptr<DatasetLoader>(DatasetLoader::getInstance())}
        
        // , m_img_data{m_dataset_loader->getImageData()}
        // , m_img_iterator{m_img_data.cbegin()}
        // , m_img_publisher{sb->get_writer<ground_truth_type>("image")}

        // , m_imu_data{m_dataset_loader->getIMUData()}
        // , m_imu_iterator{m_imu_data.cbegin()}
        // , m_imu_publisher{sb->get_writer<ground_truth_type>("imu")}

        // , m_pose_data{m_dataset_loader->getPoseData()}
        // , m_pose_iterator{m_pose_data.cbegin()}
        // , m_pose_publisher{sb->get_writer<ground_truth_type>("pose")}

        // , m_ground_truth_data{m_dataset_loader->getGroundTruthData()}
        // , m_ground_truth_iterator{m_ground_truth_data.cbegin()}
        // , m_ground_truth_publisher{sb->get_writer<ground_truth_type>("ground truth")}
        
        // // , dataset_first_time{m_data_iterator->first}
        // , m_rtc{pb->lookup_impl<RelativeClock>()} { }

    virtual skip_option _p_should_skip() override {
        if (!data_emitter.empty()) {
            std::chrono::nanoseconds sleep_time = data_emitter.sleep_for();
            std::this_thread::sleep_for(sleep_time);

            return skip_option::run;
        } else {
            return skip_option::stop;
        }
    }

    virtual void _p_one_iteration() override {
        std::chrono::nanoseconds time_since_start = m_rtc->now().time_since_epoch();
        data_emitter.emit(m_rtc.now());
    }

private:
    const std::shared_ptr<switchboard>                                       sb;
    // const std::shared_ptr<DatasetLoader>                                     m_dataset_loader;
    
    // Image stuff
    // switchboard::writer<image_type>                                          m_image_publisher;
    // const std::multimap<std::chrono::nanoseconds, ImageData>                 m_img_data;
    // std::multimap<std::chrono::nanoseconds, ImageData>::const_iterator       m_img_iterator;

    // IMU stuff
    // switchboard::writer<imu_type>                                            m_imu_publisher;
    // const std::multimap<std::chrono::nanoseconds, IMUData>                   m_imu_data;
    // std::multimap<std::chrono::nanoseconds, IMUData>::const_iterator         m_imu_iterator;

    // Pose stuff
    // switchboard::writer<pose_type>                                           m_pose_publisher;
    // const std::multimap<std::chrono::nanoseconds, PoseData>                  m_pose_data;
    // std::multimap<std::chrono::nanoseconds, PoseData>::const_iterator        m_pose_iterator;

    // Ground Truth stuff
    // switchboard::writer<ground_truth_type>                                   m_ground_truth_publisher;
    // const std::multimap<std::chrono::nanoseconds, GroundTruthData>           m_ground_truth_data;
    // std::multimap<std::chrono::nanoseconds, GroundTruthData>::const_iterator m_ground_truth_iterator;

    DataEmitter data_emitter;
    // std::chrono::nanoseconds                                                 dataset_first_time;
    std::shared_ptr<RelativeClock>                                           m_rtc;

    // const std::chrono::nanoseconds                                           error_cushion{250};
}