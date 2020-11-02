#include "common/threadloop.hpp"
#include "common/plugin.hpp"
#include <mxre>
#include "common/switchboard.hpp"
#include "common/data_format.hpp"
#include "common/phonebook.hpp"

using namespace ILLIXR;

class mxre_reader : public threadloop {
  public:
    mxre_reader(std::string name_, phonebook* pb_)
      : threadloop{name_, pb_}
      , sb{pb->lookup_impl<switchboard>()}
      //, _m_mxre_frame{sb->publish<imu_cam_type>("mxre_frame")}
      , _m_mxre_frame{sb->publish<int>("mxre_frame")}
    {}

    virtual void _p_thread_setup() {
      // Init reader connection to MXRE here
      illixrSink.setup("sink");
    }

    virtual void _p_one_iteration() {
      // Poll MXRE for new frame, If there is a new frame, publish it to the plug
      illixrSink.recv(&recvData);
      _m_mxre_frame->put(&recvData);
    }

  private:
    const std::shared_ptr<switchboard> sb;
    //std::unique_ptr<writer<imu_cam_type>> _m_mxre_frame;
    std::unique_ptr<writer<int>> _m_mxre_frame;
    mxre::types::ILLIXRSink<int> illixrSink;
    int recvData;
};

PLUGIN_MAIN(mxre_reader)
