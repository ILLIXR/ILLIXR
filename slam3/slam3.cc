#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include "common/common.hh"
#include <iostream>
#include <unistd.h>

using namespace ILLIXR;

/* This code gets slam values from the object in slam3.py. This is
   just a proof-of-concept that we can call out to Python in this
   way. In production, we might use Boost.Python or SWIG. */

class slam3 : public abstract_slam {
public:
	slam3() {
		Py_Initialize();

		module_name = PyUnicode_FromString("slam3");
		assert(module_name);

		module = PyImport_Import(module_name);
		assert(module);

		slam_object = PyObject_GetAttrString(module, "slam3");
		assert(slam_object);

		method_name = PyUnicode_FromString("produce_nonbl");
	}
private:
	PyObject *module_name, *module, *slam_object, *method_name;
	pose real_pose;
public:
	/* compatibility interface */
	virtual void feed_cam_frame_nonbl(cam_frame&) override { }
	virtual void feed_accel_nonbl(accel&) override { }
	virtual ~slam3() override {
		Py_DECREF(module_name);
		Py_DECREF(module);
		Py_DECREF(slam_object);
		Py_DECREF(method_name);
		Py_FinalizeEx();
	}
	virtual pose& produce_nonbl() override {
		PyObject* pose = PyObject_CallMethodObjArgs(slam_object, method_name, nullptr);
		assert(PyFloat_Check(pose));
		real_pose.data[0] = PyFloat_AsDouble(pose);
		Py_DECREF(pose);
		return real_pose;
	}
};

ILLIXR_make_dynamic_factory(abstract_slam, slam3)
