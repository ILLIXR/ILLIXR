#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <memory>
#include <random>
#include <chrono>
#include <future>
#include <thread>
#include "common/common.hh"
#include "concurrent_utils.hh"
#include "dynamic_lib.hh"

using namespace ILLIXR;

class slow_pose_producer {
public:
	slow_pose_producer(
		std::shared_ptr<abstract_slam> slam,
		std::shared_ptr<abstract_cam> cam,
		std::shared_ptr<abstract_imu> imu)
		: _m_slam{slam}
		, _m_cam{cam}
		, _m_imu{imu}
	{ }

	void main_loop() {
		_m_slam->feed_cam_frame_nonbl(_m_cam->produce_blocking());
		_m_slam->feed_accel_nonbl(_m_imu->produce_nonbl());
	}
	const pose& produce() {
		return _m_slam->produce_nonbl();
	}
private:
	std::shared_ptr<abstract_slam> _m_slam;
	std::shared_ptr<abstract_cam> _m_cam;
	std::shared_ptr<abstract_imu> _m_imu;
};

#define create_from_dynamic_factory(abstract_type, lib) \
	std::shared_ptr<abstract_type>{ \
		lib.get<abstract_type*(*)()>("make_" #abstract_type)() \
	};



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

int main(int argc, char** argv) {


	if(!glfwInit()){
		printf("Failed to initialize glfw\n");		
		return 0;
	}

	// Create a hidden window, so we can create some eye texture resources.
	// This is just imitating the glTexture created by Monado, etc
	GLFWwindow* headless_window = initWindow(256,256,0,false);

	glfwMakeContextCurrent(headless_window);
	
	// Init and verify GLEW
	if(glewInit()){
		printf("Failed to init GLEW\n");
		glfwDestroyWindow(headless_window);
		glfwTerminate();		
		return 0;
	}

	// Creat our fake eye texture (again, usually would come from Monado's swapchain)
	rendered_frame eye_texture;
	glGenTextures(1, &eye_texture.texture_handle);
    glBindTexture(GL_TEXTURE_2D_ARRAY, eye_texture.texture_handle);
	// Set the texture parameters for the texture that the FBO will be
    // mapped into.
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGB8, 256, 256, 2, 0, GL_RGB, GL_UNSIGNED_BYTE, 0);
	glBindTexture(GL_TEXTURE_2D_ARRAY, 0); // unbind texture, will rebind later

	// Create a framebuffer to draw some things to the eye texture
	GLuint eyetexture_fb;
	glGenFramebuffers(1, &eyetexture_fb);
	glViewport(0, 0, 256, 256);

	GLuint render_depth;
	glGenRenderbuffers(1, &render_depth);
    glBindRenderbuffer(GL_RENDERBUFFER, render_depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, 256, 256);
    //glRenderbufferStorageMultisample(GL_RENDERBUFFER, fboSampleCount, GL_DEPTH_COMPONENT, TEXTURE_WIDTH, TEXTURE_HEIGHT);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);

	// Bind eyebuffer texture
	glBindTexture(GL_TEXTURE_2D_ARRAY, eye_texture.texture_handle);
	glFramebufferTextureLayer(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, eye_texture.texture_handle, 0, 0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);

	// attach a renderbuffer to depth attachment point
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, render_depth);

	// Bind FBO to draw to eye texture
	glBindFramebuffer(GL_FRAMEBUFFER, eyetexture_fb);
	glClearColor(0.0f, 1.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);


	// this would be set by a config file
	dynamic_lib slam_lib = dynamic_lib::create(std::string{argv[1]});
	dynamic_lib cam_lib = dynamic_lib::create(std::string{argv[2]});
	dynamic_lib imu_lib = dynamic_lib::create(std::string{argv[3]});
	dynamic_lib slam2_lib = dynamic_lib::create(std::string{argv[4]});
	dynamic_lib timewarp_gl_lib = dynamic_lib::create(std::string{argv[5]});

	/* I enter a lexical scope after creating the dynamic_libs,
	   because I need to be sure that they are destroyed AFTER the
	   objects which come from them. */
	{
		/* dynamiclaly load the .so-lib provider and call its factory. I
		   use pointers for the polymorphism. */
		std::shared_ptr<abstract_slam> slam = create_from_dynamic_factory(abstract_slam, slam_lib);
		std::shared_ptr<abstract_cam> cam = create_from_dynamic_factory(abstract_cam, cam_lib);
		std::shared_ptr<abstract_imu> imu = create_from_dynamic_factory(abstract_imu, imu_lib);
		std::shared_ptr<abstract_timewarp> tw = create_from_dynamic_factory(abstract_timewarp, timewarp_gl_lib);
		tw->init(eye_texture, headless_window);

		auto slow_pose = std::make_unique<slow_pose_producer>(slam, cam, imu);

		std::async(std::launch::async, [&](){
			std::default_random_engine generator;
			std::uniform_int_distribution<int> distribution{200, 600};

			std::cout << "Model an XR app by calling for a pose sporadically."
					  << std::endl;

			for (int i = 0; i < 4; ++i) {
				int delay = distribution(generator);
				std::this_thread::sleep_for(std::chrono::milliseconds(delay));
				const pose& cur_pose = slow_pose->produce();
				std::cout << "pose = "
						  << cur_pose.data[0] << ", "
						  << cur_pose.data[1] << ", "
						  << cur_pose.data[2] << std::endl;
			}

			std::cout << "Hot swap slam1 for slam2 (should see negative drift now)."
					  << std::endl;

			std::shared_ptr<abstract_slam> slam2 = create_from_dynamic_factory(abstract_slam, slam2_lib);
			// auto new_slow_pose = std::make_unique<slow_pose_producer>(slam2, cam, imu);
			// slow_pose.swap(new_slow_pose);
			slow_pose.reset(new slow_pose_producer{slam2, cam, imu});

			for (int i = 0; i < 4; ++i) {
				int delay = distribution(generator);
				std::this_thread::sleep_for(std::chrono::milliseconds(delay));
				const pose& cur_pose = slow_pose->produce();
				std::cout << "pose = "
						  << cur_pose.data[0] << ", "
						  << cur_pose.data[1] << ", "
						  << cur_pose.data[2] << std::endl;
			}
		});
	}

	return 0;
}
