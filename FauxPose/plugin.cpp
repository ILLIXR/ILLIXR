/*****************************************************************************/
/* FauxPose/plugin.cpp                                                       */
/*                                                                           */
/* Created: 03/03/2022                                                       */
/* Last Edited: 08/06/2022                                                   */
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
/*   * (This version uploaded to ILLIXR github)                              */
/*                                                                           */

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
#ifndef NDEBUG
		std::cout << "[fauxpose] Starting Service\n";
#endif

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
#ifndef NDEBUG
		std::cout << "[fauxpose] Ending Service\n";
#endif
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

	// ********************************************************************
	virtual pose_type correct_pose([[maybe_unused]] const pose_type pose) const override {
		pose_type simulated_pose;
#ifndef NDEBUG
		std::cout << "[fauxpose] Returning (zero) pose\n";
#endif
		return simulated_pose;
	}

	// ********************************************************************
	virtual Eigen::Quaternionf get_offset() override {
		return offset;
	}

	// ********************************************************************
	virtual void set_offset(const Eigen::Quaternionf& raw_o_times_offset) override{
		std::unique_lock lock {offset_mutex};
		Eigen::Quaternionf raw_o = raw_o_times_offset * offset.inverse();
		offset = raw_o.inverse();
	}

	// ********************************************************************
	virtual fast_pose_type get_fast_pose() const override  {
		// MHuzai:  In actual pose prediction, the semantics are that
		//  we return the pose for next vsync, not now. I think we
		//  should do the same here, unless your intent is different
		//  with faux_pose.
		return get_fast_pose(std::chrono::system_clock::now());
	}

	// ********************************************************************
	// get_fast_pose(): returns a "fast_pose_type" with the algorithmically
	//   determined location values.  (Presently moving in a circle, but
	//   always facing "front".)
	//
	// NOTE: time_type == std::chrono::system_clock::time_point
	virtual fast_pose_type get_fast_pose(time_type time) const override {
		pose_type simulated_pose;	/* The algorithmically calculated 6-DOF pose */
		double	sim_time;		/* sim_time is used to regulate a consistent movement */

		RAC_ERRNO_MSG("[fauxpose] at start of _p_one_iteration");

		// Calculate simulation time from start of execution
		std::chrono::nanoseconds elapsed_time;
		elapsed_time = time - sim_start_time;
		sim_time = elapsed_time.count() * 0.000000001;

		// Calculate new pose values
		//   Pose values are calculated from the passage of time to maintain consistency */
		simulated_pose.position[0] = amplitude * sin(sim_time * period);	// X
		simulated_pose.position[1] = 1.5;					// Y
		simulated_pose.position[2] = amplitude * cos(sim_time * period);	// Z
		simulated_pose.orientation = Eigen::Quaternionf(1.0, 0.0, 0.0, 0.0);	// (W,X,Y,Z) Facing forward

		// Return the new pose
#ifndef NDEBUG
		std::cout << "[fauxpose] Returning pose\n";
#endif
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

	time_type sim_start_time;	/* Store the initial time to calculate a known runtime */
	double period;			/* The period of the circular movment (in seconds) */
	double amplitude;		/* The amplitude of the circular movment (in meters) */
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
#ifndef NDEBUG
		printf("[fauxpose] Starting Plugin\n");
#endif
	}

	// ********************************************************************
	virtual ~faux_pose() override {
#ifndef NDEBUG
		std::cout << "[fauxpose] Ending Plugin\n";
#endif
	}
};

// This line makes the plugin importable by Spindle
PLUGIN_MAIN(faux_pose);

