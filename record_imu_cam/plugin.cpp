#include "common/phonebook.hpp"
#include "common/plugin.hpp"
#include "common/switchboard.hpp"
#include "common/data_format.hpp"

#include <fstream>
#include <opencv2/core/mat.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

using namespace ILLIXR;

class record_imu_cam : public plugin {
public:
	record_imu_cam(std::string name_, phonebook* pb_)
		: plugin{name_, pb_}
		, sb{pb->lookup_impl<switchboard>()}
	{
		sb->schedule<imu_cam_type>(id, "imu_cam", [&](const imu_cam_type *datum){
			this->dump_data(datum);
		});

		std::string record_data = get_record_data_path();
		//check folder exist, if exist delete it'
		system(("rm  -r " + record_data).c_str());

		std::string imu_file = record_data + "imu0/" + "data.csv";
		system(("mkdir -p " + record_data + "imu0/").c_str());
		imu_wt_file.open(imu_file, std::ofstream::out);

		std::string cam0_file = record_data + "cam0/" + "data.csv";
		system(("mkdir -p " + record_data + "cam0/data").c_str());
		cam0_wt_file.open(cam0_file, std::ofstream::out);

		std::string cam1_file = record_data + "cam1/" + "data.csv";
		system(("mkdir -p " + record_data + "cam1/data").c_str());
		cam1_wt_file.open(cam1_file, std::ofstream::out);
	}

	void dump_data(const imu_cam_type *datum) {
		std::string record_data = get_record_data_path();
		ullong timestamp  = datum->dataset_time;
		Eigen::Vector3f angular_v = datum->angular_v;
		Eigen::Vector3f linear_a = datum->linear_a;

		// write imu0
		imu_wt_file << datum->dataset_time
	       			<< "," << angular_v[0]
				<< "," << angular_v[1]
	       			<< "," << angular_v[2]
	       			<< "," << linear_a[0]
				<< "," << linear_a[1]
	       			<< "," << linear_a[2]
				<< std::endl;

		// write cam0
		std::optional<cv::Mat*>  cam0_data = datum->img0;
		std::string cam0_img = record_data + "cam0/data/" + std::to_string(timestamp) + ".png";
		if (cam0_data!=std::nullopt) {
			cam0_wt_file << timestamp << "," << timestamp <<".png"<< std::endl;
			cv::imwrite(cam0_img, *(cam0_data.value()));
		}

		// write cam1 
		std::optional<cv::Mat*>  cam1_data = datum->img1;
                std::string cam1_img = record_data + "cam1/data/" + std::to_string(timestamp) + ".png";
		if (cam1_data!=std::nullopt) {
			cam1_wt_file << timestamp << "," << timestamp <<".png"<< std::endl;
			cv::imwrite(cam1_img, *(cam1_data.value()));
		}

	}

	virtual ~record_imu_cam() override { 
		imu_wt_file.close();
		cam0_wt_file.close();
		cam1_wt_file.close();
	}

private:
	std::ofstream imu_wt_file;
	std::ofstream cam0_wt_file;
	std::ofstream cam1_wt_file;
	const std::shared_ptr<switchboard> sb;

	std::string get_record_data_path() {
		std::string illixr_data(std::getenv("ILLIXR_DATA"));
		// set path for data recording here
		return illixr_data + "/../data_record/";
	}

};
// This line makes the plugin importable by Spindle
PLUGIN_MAIN(record_imu_cam);
