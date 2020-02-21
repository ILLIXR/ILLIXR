#include <chrono>
#include <future>
#include <iostream>
#include <thread>
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include "common/common.hh"

using namespace ILLIXR;

class timewarp_gl : public abstract_timewarp {

static GLFWwindow* initWindow(int width, int height, GLFWwindow* shared, bool visible)
{
	GLFWwindow* win;
	if(visible)
		glfwWindowHint(GLFW_VISIBLE, GL_TRUE);
	else 
		glfwWindowHint(GLFW_VISIBLE, GL_FALSE);
    win = glfwCreateWindow(width, height, "ILLIXR", 0, shared);
	return win;	
}


public:
	timewarp_gl() {
		window = initWindow(500, 500, 0, true);
		glfwMakeContextCurrent(window);
	}

private:
	GLFWwindow* window;
	rendered_frame frame;
	std::thread _m_thread;
	std::atomic<bool> _m_terminate {false};

public:
	/* compatibility interface */
	
	virtual void init(rendered_frame frame_handle) override {
		this->frame = frame_handle;
	}
};

ILLIXR_make_dynamic_factory(abstract_timewarp, timewarp_gl)
