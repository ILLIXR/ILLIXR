#include "common/plugin.hpp"
#include <mxre>
#include "common/switchboard.hpp"
#include "common/data_format.hpp"
#include "common/phonebook.hpp"

using namespace ILLIXR;

class mxre_writer : public plugin {
public:
	mxre_writer(std::string name_, phonebook* pb_)
		: plugin{name_, pb_}
		, sb{pb->lookup_impl<switchboard>()}
	{
    illixrSource.setup("source", MX_DTYPE_CVMAT);
	}

	virtual void start() override {
		plugin::start();
		sb->schedule<imu_cam_type>(id, "imu_cam", [&](const imu_cam_type *datum) {
			this->feed_mxre(datum);
		});
  }

  void feed_mxre(const imu_cam_type *datum) {
    // Not every imu_cam_type will have a cam frame (some might only have IMU values)
    if (!datum->img0.has_value() && !datum->img1.has_value()) {
      return;
    }

    debug_print("send data");
    cv::Mat img0{*datum->img0.value()};
    illixrSource.send(&img0);
	}

private:
	const std::shared_ptr<switchboard> sb;
  mxre::types::ILLIXRSource<cv::Mat> illixrSource;
};

PLUGIN_MAIN(mxre_writer)
