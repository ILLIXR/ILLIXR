#include "common/plugin.hpp"

#include "common/data_format.hpp"
#include "common/phonebook.hpp"
#include "common/pose_prediction.hpp"
#include "common/relative_clock.hpp"
#include "common/threadloop.hpp"
#include "data_loading.hpp"

#include <shared_mutex>

using namespace ILLIXR;

#define ViconRoom1Easy      1403715273262142976
#define ViconRoom1Medium    1403715523912143104
#define ViconRoom1Difficult 1403715886544058112
#define ViconRoom2Easy      1413393212225760512
#define ViconRoom2Medium    1413393885975760384
#define ViconRoom2Hard      1413394881555760384
#define fast2               2111965

#define dataset_walking     1700613045229490665
#define dataset_static      1700611471945221229
#define dataset_bs          1700612128609292316

#define fast2               2111965
#define slow1               2096955
#define fast1               2100470

#define still3              2221782


class offline_cam : public threadloop {
public:
    offline_cam(std::string name_, phonebook* pb_)
        : threadloop{name_, pb_}
        , sb{pb->lookup_impl<switchboard>()}
        , _m_cam_publisher{sb->get_writer<cam_type>("cam")}
        , _m_sensor_data{load_data()}
        , _m_sensor_data_it{_m_sensor_data.cbegin()}
        // , dataset_first_time{_m_sensor_data.cbegin()->first}
        , dataset_first_time{1700613045229490665}
        , last_ts{0}
        , _m_rtc{pb->lookup_impl<RelativeClock>()}
        , next_row{_m_sensor_data.cbegin()} { }

    virtual skip_option _p_should_skip() override {
        if (true) {
            return skip_option::run;
        } else {
            return skip_option::stop;
        }
    }

    virtual void _p_one_iteration() override {
        assert(_m_sensor_data_it != _m_sensor_data.end());
        time_point real_now(std::chrono::duration<long, std::nano>{_m_sensor_data_it->first - dataset_first_time});
        // What if real_now is negative? Drop the images
        if (real_now.time_since_epoch().count() < 0) {
            ++_m_sensor_data_it;
            return;
        }
        const sensor_types& sensor_datum = _m_sensor_data_it->second;

        _m_cam_publisher.put(_m_cam_publisher.allocate<cam_type>(cam_type{
            real_now,
            sensor_datum.cam0.load(),
            sensor_datum.cam1.load()
        }));
        ++_m_sensor_data_it;
        std::this_thread::sleep_for(std::chrono::nanoseconds(_m_sensor_data_it->first - dataset_first_time -
                                                             _m_rtc->now().time_since_epoch().count() - 2));
    }

private:
    const std::shared_ptr<switchboard>             sb;
    switchboard::writer<cam_type>                  _m_cam_publisher;
    const std::map<ullong, sensor_types>           _m_sensor_data;
    std::map<ullong, sensor_types>::const_iterator _m_sensor_data_it;
    ullong                                         dataset_first_time;
    ullong                                         last_ts;
    std::shared_ptr<RelativeClock>                 _m_rtc;
    std::map<ullong, sensor_types>::const_iterator next_row;
};

PLUGIN_MAIN(offline_cam);
