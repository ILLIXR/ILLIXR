#include "common/threadloop.hpp"
#include "common/plugin.hpp"
#include <mxre>
#include <memory>
#include "common/switchboard.hpp"
#include "common/data_format.hpp"
#include "common/pose_prediction.hpp"
#include "common/math_util.hpp"
#include "common/phonebook.hpp"

class mxre_writer : public plugin {
  public:
    mxre_writer(std::string name_, phonebook* pb_)
      : plugin{name_, pb_}
      , sb{pb->lookup_impl<switchboard>()}
    {}


    virtual void start() override {
      plugin::start();
      illixrSource.setup("192.17.102.20");
      sb->schedule<imu_cam_type>(id, "imu_cam", [&](const imu_cam_type *datum) {
        this->send_imu_cam_data(datum);
      });
	  }


    void send_imu_cam_data(const imu_cam_type *datum) {
      if (datum == NULL) {
		  	assert(previous_timestamp == 0);
			  return;
		  }

      assert(datum->dataset_time > previous_timestamp);
      previous_timestamp = datum->dataset_time;

      imu_buffer.push_back(mxre::kimera_type::imu_type{
        .time = datum->time,
        .angular_v = datum->angular_v,
        .linear_a = datum->linear_a,
        .dataset_time = datum->dataset_time,
      });

      if (!datum->img0.has_value() && !datum->img1.has_value()) {
			  return;
		  }

      if (currentBlock == NULL) {
        currentBlock = new mxre::kimera_type::imu_cam_type{
          .time = datum->time,
          .img0 = new mxre::types::Frame(*datum->img0.value(),0,0),
          .img1 = new mxre::types::Frame(*datum->img1.value(),0,0),
          .imu_count = static_cast<unsigned int>(imu_buffer.size()),
          .imu_readings=std::shared_ptr<mxre::kimera_type::imu_type[]>(&imu_buffer[0]),
          .dataset_time = datum->dataset_time,
        };
      }

      illixrSource.send_cam_imu_type(currentBlock);
      free(currentBlock);
      imu_buffer.clear();
      currentBlock = NULL;
    }


  private:
    const std::shared_ptr<switchboard> sb;
    std::unique_ptr<writer<pose_type>> _m_pose;
	  std::unique_ptr<writer<imu_integrator_input>> _m_imu_integrator_input;

    std::vector<mxre::kimera_type::imu_type> imu_buffer;
    mxre::kimera_type::imu_cam_type *currentBlock = NULL;
	  mxre::kernels::ILLIXRZMQSource<mxre::kimera_type::imu_cam_type> illixrSource;

    double previous_timestamp = 0.0;
};

PLUGIN_MAIN(mxre_writer)