#include "common/threadloop.hpp"
#include "common/plugin.hpp"
#include <mxre>
#include "common/switchboard.hpp"
#include "common/data_format.hpp"
#include "common/pose_prediction.hpp"
#include "common/math_util.hpp"
#include "common/phonebook.hpp"

class mxre_reader : public threadloop {
  public:
    mxre_reader(std::string name_, phonebook* pb_)
      : threadloop{name_, pb_}
      , sb{pb->lookup_impl<switchboard>()}
      , _m_pose{sb->publish<pose_type>("slow_pose")}
    	, _m_imu_integrator_input{sb->publish<imu_integrator_input>("imu_integrator_input")}
    {}

    virtual void _p_thread_setup() {
      illixrSink.setup("sink", MX_DTYPE_CVMAT);
    }

    virtual void _p_one_iteration() {
      illixrSink.recv(&recvFrame);

      const auto& cached_state = vio_output->W_State_Blkf_;
      const auto& w_pose_blkf_trans = cached_state.pose_.translation().transpose();
      const auto& w_pose_blkf_rot = cached_state.pose_.rotation().quaternion();
      const auto& w_vel_blkf = cached_state.velocity_.transpose();
      const auto& imu_bias_gyro = cached_state.imu_bias_.gyroscope().transpose();
      const auto& imu_bias_acc = cached_state.imu_bias_.accelerometer().transpose();

      // Get the pose returned from SLAM
      Eigen::Quaternionf quat = Eigen::Quaternionf{w_pose_blkf_rot(0), w_pose_blkf_rot(1), w_pose_blkf_rot(2), w_pose_blkf_rot(3)};
      Eigen::Quaterniond doub_quat = Eigen::Quaterniond{w_pose_blkf_rot(0), w_pose_blkf_rot(1), w_pose_blkf_rot(2), w_pose_blkf_rot(3)};
      Eigen::Vector3f pos  = w_pose_blkf_trans.cast<float>();

      assert(isfinite(quat.w()));
      assert(isfinite(quat.x()));
      assert(isfinite(quat.y()));
      assert(isfinite(quat.z()));
      assert(isfinite(pos[0]));
      assert(isfinite(pos[1]));
      assert(isfinite(pos[2]));
      
      _m_pose->put(new pose_type{
        .sensor_time = imu_cam_buffer->time,
        .position = pos,
        .orientation = quat,
      });

      _m_imu_integrator_input->put(new imu_integrator_input{
        .last_cam_integration_time = (double(imu_cam_buffer->dataset_time) / NANO_SEC),
        .t_offset = -0.05,

        .params = {
          .gyro_noise = kimera_pipeline_params.imu_params_.gyro_noise_,
          .acc_noise = kimera_pipeline_params.imu_params_.acc_noise_,
          .gyro_walk = kimera_pipeline_params.imu_params_.gyro_walk_,
          .acc_walk = kimera_pipeline_params.imu_params_.acc_walk_,
          .n_gravity = kimera_pipeline_params.imu_params_.n_gravity_,
          .imu_integration_sigma = kimera_pipeline_params.imu_params_.imu_integration_sigma_,
          .nominal_rate = kimera_pipeline_params.imu_params_.nominal_rate_,
        },

        .biasAcc =imu_bias_acc,
        .biasGyro = imu_bias_gyro,
        .position = w_pose_blkf_trans,
        .velocity = w_vel_blkf,
        .quat = doub_quat,
      });
    }

  private:
    const std::shared_ptr<switchboard> sb;
    std::unique_ptr<writer<pose_type>> _m_pose;
	  std::unique_ptr<writer<imu_integrator_input>> _m_imu_integrator_input;

    mxre::types::ILLIXRSink<cv::Mat> illixrSink;