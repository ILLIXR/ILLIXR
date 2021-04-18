#include "common/threadloop.hpp"
#include "common/plugin.hpp"
#include <mxre>
#include "common/switchboard.hpp"
#include "common/data_format.hpp"
#include "common/pose_prediction.hpp"
#include "common/math_util.hpp"
#include "common/phonebook.hpp"
#include <iostream>

class mxre_reader : public threadloop {
  public:
    mxre_reader(std::string name_, phonebook* pb_)
      : threadloop{name_, pb_}
      , sb{pb->lookup_impl<switchboard>()}
      , _m_pose{sb->publish<pose_type>("slow_pose")}
    	, _m_imu_integrator_input{sb->publish<imu_integrator_input>("imu_integrator_input")}
    {}

    virtual void _p_thread_setup() {
      illixrSink.setup("192.17.102.20");//this machine ip address
    }
       
    virtual void _p_one_iteration() {
      //TODO: mitigate memory leak
      mxre::kimera_type::kimera_output recvKimeraOutput;
      illixrSink.recv_kimera_output(&recvKimeraOutput);

      // Get the pose returned from SLAM
      Eigen::Quaternionf quat = Eigen::Quaternionf{recvKimeraOutput.quat[0], recvKimeraOutput.quat[1], recvKimeraOutput.quat[2], recvKimeraOutput.quat[3]};
      Eigen::Quaterniond doub_quat = Eigen::Quaterniond{recvKimeraOutput.quat[0], recvKimeraOutput.quat[1], recvKimeraOutput.quat[2], recvKimeraOutput.quat[3]};
      Eigen::Vector3f pos = Eigen::Vector3f(recvKimeraOutput.pose_type_position[0], recvKimeraOutput.pose_type_position[1], recvKimeraOutput.pose_type_position[2]);
      
      _m_pose->put(new pose_type{
        .sensor_time = recvKimeraOutput.sensor_time,
        .position = pos,
        .orientation = quat,
      });

      _m_imu_integrator_input->put(new imu_integrator_input{
        .last_cam_integration_time = recvKimeraOutput.last_cam_integration_time,
        .t_offset = -0.05,

        .params = {
          .gyro_noise = recvKimeraOutput.imu_params_gyro_noise,
          .acc_noise = recvKimeraOutput.imu_params_acc_noise,
          .gyro_walk = recvKimeraOutput.imu_params_gyro_walk,
          .acc_walk = recvKimeraOutput.imu_params_acc_walk,
          .n_gravity = Eigen::Matrix<double,3,1>{recvKimeraOutput.imu_params_n_gravity[0], recvKimeraOutput.imu_params_n_gravity[1], recvKimeraOutput.imu_params_n_gravity[2]},
          .imu_integration_sigma = recvKimeraOutput.imu_params_imu_integration_sigma,
          .nominal_rate = recvKimeraOutput.imu_params_nominal_rate,
        },

        .biasAcc = Eigen::Vector3d{recvKimeraOutput.biasAcc[0], recvKimeraOutput.biasAcc[1], recvKimeraOutput.biasAcc[2]},
        .biasGyro = Eigen::Vector3d{recvKimeraOutput.biasGyro[0], recvKimeraOutput.biasGyro[1], recvKimeraOutput.biasGyro[2]},
        .position = Eigen::Matrix<double,3,1>{recvKimeraOutput.position[0], recvKimeraOutput.position[1], recvKimeraOutput.position[2]},
        .velocity = Eigen::Matrix<double,3,1>{recvKimeraOutput.velocity[0], recvKimeraOutput.velocity[1], recvKimeraOutput.velocity[2]},
        .quat = Eigen::Quaterniond{recvKimeraOutput.orientation[0], recvKimeraOutput.orientation[1], recvKimeraOutput.orientation[2], recvKimeraOutput.orientation[3]},
      });

      auto curr_time = std::chrono::time_point_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now());
      auto diff = curr_time - std::chrono::time_point_cast<std::chrono::nanoseconds>(recvKimeraOutput.sensor_time);
      std::cerr << "Latency: " << std::chrono::duration_cast<std::chrono::nanoseconds>(diff).count() / 1000000.0 << std::endl << std::endl;
    }

  private:
    const std::shared_ptr<switchboard> sb;
    std::unique_ptr<writer<pose_type>> _m_pose;
	  std::unique_ptr<writer<imu_integrator_input>> _m_imu_integrator_input;

    mxre::kernels::ILLIXRZMQSink<mxre::kimera_type::kimera_output> illixrSink;
};

PLUGIN_MAIN(mxre_reader)