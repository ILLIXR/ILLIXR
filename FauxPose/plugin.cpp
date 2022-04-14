/*****************************************************************************/
/* FauxPose/plugin.cpp                                                       */
/*                                                                           */
/* Created: 03/03/2022                                                       */
/* Last Edited: 03/24/2022                                                   */
/*                                                                           */
/* An IlliXR plugin that publishes position tracking data ("pose")           */
/*	 from a mathematical operation just to quickly produce some known    */
/*	 tracking path for the purpose of debugging other portions of IlliXR.*/
/*                                                                           */
/* TODO:                                                                     */
/*   * DONE: Need to implement as a "pose_prediction" "impl" (service?)      */
/*   * DONE: Fix so that "gldemo" (etc.) face forward.                       */
/*                                                                           */
/* NOTES:                                                                    */
/*   * get_fast_pose() method returns a "fast_pose_type"                     */
/*   * "fast_pose_type" is a "pose_type" plus computed & target timestamps   */
/*   * correct_pose() method returns a "pose_type"                           */
/*   * (This version posted to freevr.org/Downloads)                         */
/*                                                                           */

#include "common/phonebook.hpp"
#include "common/plugin.hpp"
#include "common/threadloop.hpp"
#include "common/data_format.hpp"
#include "common/pose_prediction.hpp"

using namespace ILLIXR;

/// Create a "pose_prediction" type service
class faux_pose_impl : public pose_prediction {
public:
	// ********************************************************************
	/* Constructor: Provide handles to faux_pose */
	faux_pose_impl(const phonebook* const pb)
		: sb{pb->lookup_impl<switchboard>()}
	{
		printf("[fauxpose] Starting Service\n");
		RAC_ERRNO_MSG("[fauxpose] in service constructor");

		// Store the initial time
		auto now = std::chrono::system_clock::now();
		sim_start_time = std::chrono::time_point_cast<std::chrono::nanoseconds>(now);

		// Set an initial faux-pose location
		//simulated_location = Eigen::Vector3f{0.0, 1.5, 0.0};
		period = 0.5;
		amplitude = 2.0;
	}

	// ********************************************************************
	virtual ~faux_pose_impl() {
		printf("[fauxpose] Ending Service\n");
		RAC_ERRNO_MSG("[fauxpose] in service destructor");
	}

	// ********************************************************************
	virtual pose_type get_true_pose() const override {
		throw std::logic_error{"Not Implemented"};
	}

	// ********************************************************************
	virtual bool fast_pose_reliable() const override {
		return true;
	}

	// ********************************************************************
	virtual bool true_pose_reliable() const override {
		return false;
	}

	virtual pose_type correct_pose([[maybe_unused]] const pose_type pose) const override {
		pose_type simulated_pose;
		printf("[fauxpose] Returning (zero) pose\n");
		return simulated_pose;
	}

	// ********************************************************************
	virtual Eigen::Quaternionf get_offset() override {
		return offset;
	}

	virtual void set_offset(const Eigen::Quaternionf& raw_o_times_offset) override{
		std::unique_lock lock {offset_mutex};
		Eigen::Quaternionf raw_o = raw_o_times_offset * offset.inverse();
		//std::cout << "pose_prediction: set_offset" << std::endl;
		offset = raw_o.inverse();
	}

	// ********************************************************************
	virtual fast_pose_type get_fast_pose() const override  {                                          
	//	const switchboard::ptr<const switchboard::event_wrapper<time_type>> estimated_vsync = _m_vsync_estimate.get_ro_nullable();
	//	if (estimated_vsync == nullptr) {                                                            
	//		std::cerr << "Vsync estimation not valid yet, returning fast_pose for now()" << std::endl;
			return get_fast_pose(std::chrono::system_clock::now());                                  
	//	} else {                                                                                     
	//		return get_fast_pose(**estimated_vsync);                                                 
	//	}                                                                                            
	}

	// ********************************************************************
	// get_fast_pose(): returns a "fast_pose_type" with the algorithmically
	//   determined location values.  (Presently moving in a circle, but
	//   always facing "front".)
	//
	// NOTE: time_type == std::chrono::system_clock::time_point
	virtual fast_pose_type get_fast_pose(time_type time) const override {
		pose_type simulated_pose;
		double	sim_time;

		RAC_ERRNO_MSG("[fauxpose] at start of _p_one_iteration");

		// Calculate simulation time from start of execution
		std::chrono::nanoseconds elapsed_time;
		//auto now = std::chrono::system_clock::now();
		//time_type current_time = std::chrono::time_point_cast<std::chrono::nanoseconds>(now);
		//elapsed_time = current_time - sim_start_time;
		elapsed_time = time - sim_start_time;
		sim_time = elapsed_time.count() * 0.000000001;

		//printf("[fauxpose] time = %ld\n", std::chrono::high_resolution_clock::now().time_since_epoch().count());
		printf("[fauxpose] elapsed time = %ld (%lf)\n", elapsed_time.count(), sim_time);
		//fprintf(stderr, "[fauxpose] ********* elapsed time = %ld (%lf)\n", elapsed_time.count(), sim_time);

		// Calculate new pose values
		//simulated_location = Eigen::Vector3f{0.1f, 0.0f, 0.0f};

		//fprintf(stderr, "FP: time.count is %ld\n", time.time_since_epoch());
		simulated_pose.position[0] = amplitude * sin(sim_time * period);	// X
		simulated_pose.position[1] = 1.5;					// Y
		simulated_pose.position[2] = amplitude * cos(sim_time * period);	// Z
		simulated_pose.orientation = Eigen::Quaternionf(1.0, 0.0, 0.0, 0.0);	// W,X,Y,Z (No rotation)

		// Return the new pose
		printf("[fauxpose] Returning pose\n");
		return fast_pose_type{
			.pose = simulated_pose,
			.predict_computed_time = std::chrono::system_clock::now(),
			.predict_target_time = time
		};

	}

// ---------------------------------------------------------------------
// ---------------------------------------------------------------------
// ---------------------------------------------------------------------
private:
	const std::shared_ptr<switchboard> sb;
	mutable Eigen::Quaternionf offset {Eigen::Quaternionf::Identity()};
	mutable std::shared_mutex offset_mutex;
	time_type sim_start_time;
	double period;
	double amplitude;
};

// ********************************************************************
// ********************************************************************
class faux_pose : public plugin {
public:
	// ********************************************************************
	/* Constructor: Provide handles to faux_pose */
	faux_pose(const std::string& name, phonebook* pb)
		: plugin{name, pb}
	{
		// "pose_prediction" is a class inheriting from "phonebook::service"
		//   It is described in "pose_prediction.hpp"
		pb->register_impl<pose_prediction>(
			std::static_pointer_cast<pose_prediction>(std::make_shared<faux_pose_impl>(pb))
		);
		printf("[fauxpose] Starting Plugin\n");
		RAC_ERRNO_MSG("[fauxpose] in constructor");

		//sim_start_time = std::chrono::time_point_cast<std::chrono::nanoseconds>(now);
	}

	// ********************************************************************
	virtual ~faux_pose() override {
		printf("[fauxpose] Ending Plugin\n");
		RAC_ERRNO_MSG("[fauxpose] in destructor");
	}
};

// This line makes the plugin importable by Spindle
PLUGIN_MAIN(faux_pose);

