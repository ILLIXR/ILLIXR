#include <chrono>
#include <iomanip>
#include <thread>
#include <eigen3/Eigen/Dense>

#include "common/plugin.hpp"
#include "common/switchboard.hpp"
#include "common/data_format.hpp"
#include "common/plugin.hpp"

#include <gtsam/base/Matrix.h>
#include <gtsam/base/Vector.h>
#include <gtsam/navigation/AHRSFactor.h>
#include <gtsam/navigation/CombinedImuFactor.h>  // Used if IMU combined is off.
#include <gtsam/navigation/ImuBias.h>
#include <gtsam/navigation/ImuFactor.h>
#include <boost/smart_ptr/shared_ptr.hpp>
#include <boost/smart_ptr/make_shared.hpp>

using namespace ILLIXR;
// IMU sample time to live in seconds
constexpr duration IMU_TTL {std::chrono::seconds{5}};

using ImuBias = gtsam::imuBias::ConstantBias;
class gtsam_integrator : public plugin {
public:
    gtsam_integrator(std::string name_, phonebook* pb_)
        : plugin{name_, pb_}
        , sb{pb->lookup_impl<switchboard>()}
        , _m_clock{pb->lookup_impl<RelativeClock>()}
        , _m_imu_integrator_input{sb->get_reader<imu_integrator_input>("imu_integrator_input")}
        , _m_imu_raw{sb->get_writer<imu_raw_type>("imu_raw")}
    {
        sb->schedule<imu_type>(id, "imu", [&](switchboard::ptr<const imu_type> datum, size_t) {
            callback(datum);
        });
    }

    void callback(switchboard::ptr<const imu_type> datum) {
		_imu_vec.emplace_back(
            datum->time,
            datum->angular_v,
            datum->linear_a
        );

        clean_imu_vec(datum->time);
        propagate_imu_values(datum->time);

        RAC_ERRNO_MSG("gtsam_integrator");
    }

private:
    const std::shared_ptr<switchboard> sb;
    const std::shared_ptr<RelativeClock> _m_clock;

    // IMU Data, Sequence Flag, and State Vars Needed
    switchboard::reader<imu_integrator_input> _m_imu_integrator_input;

    // Write IMU Biases for PP
    switchboard::writer<imu_raw_type> _m_imu_raw;

    std::vector<imu_type> _imu_vec;

    [[maybe_unused]] time_point last_cam_time;
    duration last_imu_offset;

    /**
     * @brief Wrapper object protecting the lifetime of IMU integration inputs and biases
     */
    class PimObject
    {
    public:
        using imu_int_t = ILLIXR::imu_integrator_input;
        using imu_t     = imu_type;
        using bias_t    = ImuBias;
        using nav_t     = gtsam::NavState;
        using pim_t     = gtsam::PreintegratedCombinedMeasurements;
        using pim_ptr_t = gtsam::PreintegrationType*;

        PimObject(const imu_int_t& imu_int_input)
            : _imu_bias{imu_int_input.biasAcc, imu_int_input.biasGyro}
            , _pim{nullptr}
        {
            pim_t::Params _params{imu_int_input.params.n_gravity};
            _params.setGyroscopeCovariance(std::pow(imu_int_input.params.gyro_noise, 2.0) * Eigen::Matrix3d::Identity());
            _params.setAccelerometerCovariance(std::pow(imu_int_input.params.acc_noise, 2.0) * Eigen::Matrix3d::Identity());
            _params.setIntegrationCovariance(std::pow(imu_int_input.params.imu_integration_sigma, 2.0) * Eigen::Matrix3d::Identity());
            _params.setBiasAccCovariance(std::pow(imu_int_input.params.acc_walk, 2.0) * Eigen::Matrix3d::Identity());
            _params.setBiasOmegaCovariance(std::pow(imu_int_input.params.gyro_walk, 2.0) * Eigen::Matrix3d::Identity());

            _pim = new pim_t {
                boost::make_shared<pim_t::Params>(std::move(_params)),
                _imu_bias
            };
            resetIntegrationAndSetBias(imu_int_input);
        }

        ~PimObject()
        {
            assert(_pim != nullptr && "_pim should not be null");

            /// Note: Deliberately leak _pim => Removes SEGV read during destruction
            /// delete _pim;
        };

        void resetIntegrationAndSetBias(const imu_int_t& imu_int_input) noexcept
        {
            assert(_pim != nullptr && "_pim should not be null");

            _imu_bias = bias_t{imu_int_input.biasAcc, imu_int_input.biasGyro};
            _pim->resetIntegrationAndSetBias(_imu_bias);

            _navstate_lkf = nav_t {
                gtsam::Pose3{gtsam::Rot3{imu_int_input.quat}, imu_int_input.position},
                imu_int_input.velocity
            };
        }

        void integrateMeasurement(const imu_t& imu_input, const imu_t& imu_input_next) noexcept
        {
            assert(_pim != nullptr && "_pim shuold not be null");

            const gtsam::Vector3 measured_acc{imu_input.linear_a.cast<double>()};
            const gtsam::Vector3 measured_omega{imu_input.angular_v.cast<double>()};

			/// Delta T should be in seconds
			duration delta_t = imu_input_next.time - imu_input.time;

			_pim->integrateMeasurement(measured_acc, measured_omega, duration2double(delta_t));
        }

        bias_t biasHat() const noexcept
        {
            assert(_pim != nullptr && "_pim shuold not be null");
            return _pim->biasHat();
        }

        nav_t predict() const noexcept
        {
            assert(_pim != nullptr && "_pim should not be null");
            return _pim->predict(_navstate_lkf, _imu_bias);
        }

    private:
        bias_t _imu_bias;
        nav_t _navstate_lkf;
        pim_ptr_t _pim;
    };

    std::unique_ptr<PimObject> _pim_obj;


    // Remove IMU values older than 'IMU_TTL' from the imu buffer
	void clean_imu_vec(time_point timestamp) {
		auto imu_iterator = _imu_vec.begin();

		// Since the vector is ordered oldest to latest, keep deleting until you
		// hit a value less than 'IMU_TTL' seconds old
		while (imu_iterator != _imu_vec.end()) {
			if (timestamp - imu_iterator->time < IMU_TTL) {
				break;
			}

			imu_iterator = _imu_vec.erase(imu_iterator);
		}
	}

    // Timestamp we are propagating the biases to (new IMU reading time)
	void propagate_imu_values(time_point real_time) {
		auto input_values = _m_imu_integrator_input.get_ro_nullable();
		if (input_values == nullptr) {
			return;
		}

#ifndef NDEBUG
        if (input_values->last_cam_integration_time > last_cam_time) {
            std::cout << "New slow pose has arrived!\n";
            last_cam_time = input_values->last_cam_integration_time;
        }
#endif

        if (_pim_obj == nullptr) {
            /// We don't have a PimObject -> make and set given the current input
            _pim_obj = std::make_unique<PimObject>(*input_values);

            /// Setting 'last_imu_offset' here to stay consistent with previous integrator version.
            /// TODO: Should be set and tested at the end of this function to avoid staleness from VIO.
            last_imu_offset = input_values->t_offset;
        } else {
            /// We already have a PimObject -> set the values given the current input
            _pim_obj->resetIntegrationAndSetBias(*input_values);
        }

        assert(_pim_obj != nullptr && "_pim_obj should not be null");

		time_point time_begin = input_values->last_cam_integration_time + last_imu_offset;
		time_point time_end = input_values->t_offset + real_time;

        const std::vector<imu_type> prop_data = select_imu_readings(_imu_vec, time_begin, time_end);

        /// Need to integrate over a sliding window of 2 imu_type values.
        /// If the container of data is smaller than 2 elements, return early.
        if (prop_data.size() < 2) {
            return;
        }

        ImuBias prev_bias = _pim_obj->biasHat();
        ImuBias bias = _pim_obj->biasHat();

#ifndef NDEBUG
        std::cout << "Integrating over " << prop_data.size() << " IMU samples\n";
#endif

        for (std::size_t i = 0; i < prop_data.size() - 1; i++) {
            _pim_obj->integrateMeasurement(prop_data[i], prop_data[i+1]);

            prev_bias = bias;
            bias = _pim_obj->biasHat();
        }

        gtsam::NavState navstate_k = _pim_obj->predict();
        gtsam::Pose3 out_pose = navstate_k.pose();

#ifndef NDEBUG
        std::cout << "Base Position (x, y, z) = "
                  << input_values->position(0) << ", "
                  << input_values->position(1) << ", "
                  << input_values->position(2) << std::endl;

        std::cout << "New  Position (x, y, z) = "
                  << out_pose.x() << ", "
                  << out_pose.y() << ", "
                  << out_pose.z() << std::endl;
#endif

        _m_imu_raw.put(_m_imu_raw.allocate<imu_raw_type>(
            imu_raw_type {
                prev_bias.gyroscope(),
                prev_bias.accelerometer(),
                bias.gyroscope(),
                bias.accelerometer(),
                out_pose.translation(),             /// Position
                navstate_k.velocity(),              /// Velocity
                out_pose.rotation().toQuaternion(), /// Eigen Quat
				real_time
            }
        ));
    }

    // Select IMU readings based on timestamp similar to how OpenVINS selects IMU values to propagate
    std::vector<imu_type> select_imu_readings(
        const std::vector<imu_type>& imu_data,
        const time_point time_begin,
        const time_point time_end)
    {
        std::vector<imu_type> prop_data;
        if (imu_data.size() < 2) {
            return prop_data;
        }

        for (std::size_t i = 0; i < imu_data.size() - 1; i++) {

            // If time_begin comes inbetween two IMUs (A and B), interpolate A forward to time_begin
            if (imu_data[i + 1].time > time_begin && imu_data[i].time < time_begin) {
                imu_type data = interpolate_imu(imu_data[i], imu_data[i + 1], time_begin);
                prop_data.push_back(data);
                continue;
            }

            // IMU is within time_begin and time_end
            if (imu_data[i].time >= time_begin && imu_data[i + 1].time <= time_end) {
                prop_data.push_back(imu_data[i]);
                continue;
            }

            // IMU is past time_end
            if (imu_data[i + 1].time > time_end) {
                imu_type data = interpolate_imu(imu_data[i], imu_data[i + 1], time_end);
                prop_data.push_back(data);
                break;
            }
        }

        // Loop through and ensure we do not have an zero dt values
        // This would cause the noise covariance to be Infinity
        for (int i = 0; i < int(prop_data.size()) - 1; i++) {
			// I need prop_data.size() - 1 to be signed, because it might equal -1.
            if (std::chrono::abs(prop_data[i + 1].time - prop_data[i].time) < std::chrono::nanoseconds{1}) {
                prop_data.erase(prop_data.begin() + i);
                i--;
            }
        }

        return prop_data;
    }

    // For when an integration time ever falls inbetween two imu measurements (modeled after OpenVINS)
	static imu_type interpolate_imu(const imu_type& imu_1, const imu_type& imu_2, time_point timestamp) {
		double lambda = duration2double(timestamp - imu_1.time) / duration2double(imu_2.time - imu_1.time);
		return imu_type {
			timestamp,
			(1 - lambda) * imu_1.linear_a + lambda * imu_2.linear_a,
			(1 - lambda) * imu_1.angular_v + lambda * imu_2.angular_v
		};
	}
};

PLUGIN_MAIN(gtsam_integrator)
