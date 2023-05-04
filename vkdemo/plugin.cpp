#include "common/data_format.hpp"
#include "common/error_util.hpp"
#include "common/global_module_defs.hpp"
#include "common/math_util.hpp"
#include "common/pose_prediction.hpp"
#include "common/switchboard.hpp"
#include "common/threadloop.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <future>
#include <iostream>
#include <thread>

using namespace ILLIXR;

// Wake up 1 ms after vsync instead of exactly at vsync to account for scheduling uncertainty
static constexpr std::chrono::milliseconds VSYNC_SAFETY_DELAY{1};

class vkdemo : public threadloop {
public:
    // Public constructor, create_component passes Switchboard handles ("plugs")
    // to this constructor. In turn, the constructor fills in the private
    // references to the switchboard plugs, so the component can read the
    // data whenever it needs to.

    vkdemo(std::string name_, phonebook* pb_)
        : threadloop{name_, pb_}
        , sb{pb->lookup_impl<switchboard>()}
        , pp{pb->lookup_impl<pose_prediction>()}
        , _m_clock{pb->lookup_impl<RelativeClock>()}
        , _m_vsync{sb->get_reader<switchboard::event_wrapper<time_point>>("vsync_estimate")} { }

    // Essentially, a crude equivalent of XRWaitFrame.
    void wait_vsync() {
        switchboard::ptr<const switchboard::event_wrapper<time_point>> next_vsync = _m_vsync.get_ro_nullable();
        time_point                                                     now        = _m_clock->now();
        time_point                                                     wait_time;

        if (next_vsync == nullptr) {
            // If no vsync data available, just sleep for roughly a vsync period.
            // We'll get synced back up later.
            std::this_thread::sleep_for(display_params::period);
            return;
        }

#ifndef NDEBUG
        if (log_count > LOG_PERIOD) {
            double vsync_in = duration2double<std::milli>(**next_vsync - now);
            std::cout << "\033[1;32m[GL DEMO APP]\033[0m First vsync is in " << vsync_in << "ms" << std::endl;
        }
#endif

        bool hasRenderedThisInterval = (now - lastTime) < display_params::period;

        // If less than one frame interval has passed since we last rendered...
        if (hasRenderedThisInterval) {
            // We'll wait until the next vsync, plus a small delay time.
            // Delay time helps with some inaccuracies in scheduling.
            wait_time = **next_vsync + VSYNC_SAFETY_DELAY;

            // If our sleep target is in the past, bump it forward
            // by a vsync period, so it's always in the future.
            while (wait_time < now) {
                wait_time += display_params::period;
            }

#ifndef NDEBUG
            if (log_count > LOG_PERIOD) {
                double wait_in = duration2double<std::milli>(wait_time - now);
                std::cout << "\033[1;32m[GL DEMO APP]\033[0m Waiting until next vsync, in " << wait_in << "ms" << std::endl;
            }
#endif
            // Perform the sleep.
            // TODO: Consider using Monado-style sleeping, where we nanosleep for
            // most of the wait, and then spin-wait for the rest?
            std::this_thread::sleep_for(wait_time - now);
        } else {
#ifndef NDEBUG
            if (log_count > LOG_PERIOD) {
                std::cout << "\033[1;32m[GL DEMO APP]\033[0m We haven't rendered yet, rendering immediately." << std::endl;
            }
#endif
        }
    }

    void _p_thread_setup() override {
        lastTime = _m_clock->now();
    }

    void _p_one_iteration() override {
        // Essentially, XRWaitFrame.
        wait_vsync();

#ifndef NDEBUG
        const double frame_duration_s = duration2double(_m_clock->now() - lastTime);
        const double fps              = 1.0 / frame_duration_s;

        if (log_count > LOG_PERIOD) {
            std::cout << "\033[1;32m[GL DEMO APP]\033[0m Submitting frame to buffer, frametime: " << frame_duration_s << ", FPS: " << fps << std::endl;
        }
#endif
        lastTime = _m_clock->now();

#ifndef NDEBUG
        if (log_count > LOG_PERIOD) {
            log_count = 0;
        } else {
            log_count++;
        }
#endif
    }

#ifndef NDEBUG
    size_t log_count  = 0;
    size_t LOG_PERIOD = 20;
#endif

private:
    const std::shared_ptr<switchboard>                                sb;
    const std::shared_ptr<pose_prediction>                            pp;
    const std::shared_ptr<const RelativeClock>                        _m_clock;
    const switchboard::reader<switchboard::event_wrapper<time_point>> _m_vsync;

    Eigen::Matrix4f basicProjection;

    time_point lastTime;

public:
    // We override start() to control our own lifecycle
    virtual void start() override {
        // Load/initialize the demo scene
        char* obj_dir = std::getenv("ILLIXR_DEMO_DATA");
        if (obj_dir == nullptr) {
            ILLIXR::abort("Please define ILLIXR_DEMO_DATA.");
        }

//        demoscene = ObjScene(std::string(obj_dir), "scene.obj");

        // Construct perspective projection matrix
        math_util::projection_fov(&basicProjection, display_params::fov_x / 2.0f, display_params::fov_x / 2.0f,
                                  display_params::fov_y / 2.0f, display_params::fov_y / 2.0f, rendering_params::near_z,
                                  rendering_params::far_z);

        // Effectively, last vsync was at zero.
        // Try to run gldemo right away.
//        threadloop::start();
    }
};

PLUGIN_MAIN(vkdemo)
