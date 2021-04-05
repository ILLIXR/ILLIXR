#include "common/threadloop.hpp"
#include "common/plugin.hpp"
#include <mxre>
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
      illixrSource.setup("source", MX_DTYPE_CVMAT);
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

      if (currentBlock == NULL) {
        currentBlock = new mxre::kimera_type::imu_cam_type;
      }

      currentBlock->imu_readings.push_back(mxre::kimera_type::imu_cam_type{
        datum->time,
        datum->angular_v,
        datum->linear_a,
        datum->dataset_time,
      });

      if (!datum->img0.has_value() && !datum->img1.has_value()) {
			  return;
		  }

      currentBlock->time = datum->time;
      currentBlock->imu_count = currentBlock->imu_readings.size();
      currentBlock->img0 = datum->img0;
      currentBlock->img1 = datum->img1;
      currentBlock->dataset_time = datum->dataset_time;

      illixrSource.send(&currentBlock);
      // Release currentBlock
      currentBlock = NULL;
    }


  private:
    const std::shared_ptr<switchboard> sb;
    std::unique_ptr<writer<pose_type>> _m_pose;
	  std::unique_ptr<writer<imu_integrator_input>> _m_imu_integrator_input;

    mxre::kimera_type::imu_cam_type *currentBlock;
	  mxre::types::ILLIXRSource<cv::Mat> illixrSource;

    double previous_timestamp = 0.0;
