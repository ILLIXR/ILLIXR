#include "common/plugin.hpp"
#include "common/switchboard.hpp"
#include "common/data_format.hpp"
#include "common/phonebook.hpp"

using namespace ILLIXR;

class mxre_reader : public plugin {
public:
	mxre_reader(std::string name_, phonebook* pb_)
		: plugin{name_, pb_}
		, sb{pb->lookup_impl<switchboard>()}
        , _m_mxre_frame{sb->publish<imu_cam_type>("mxre_frame")}

	{}

    void _p_thread_setup() override {
        // Init reader connection to MXRE here
	}

	void _p_one_iteration() override {
        // Poll MXRE for new frame, If there is a new frame, publish it to the plug
    {

private:
	const std::shared_ptr<switchboard> sb;
    std::unique_ptr<writer<imu_cam_type>> _m_mxre_frame;
};

PLUGIN_MAIN(mxre_reader)
