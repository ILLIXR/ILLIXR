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

static constexpr int   EYE_TEXTURE_WIDTH   = 1024;  // NOTE: texture size cannot be larger than
static constexpr int   EYE_TEXTURE_HEIGHT  = 1024;  // the rendering window size in non-FBO mode

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

void GLAPIENTRY
MessageCallback( GLenum source,
                 GLenum type,
                 GLuint id,
                 GLenum severity,
                 GLsizei length,
                 const GLchar* message,
                 const void* userParam )
{
  fprintf( stderr, "GL CALLBACK: %s type = 0x%x, severity = 0x%x, message = %s\n",
           ( type == GL_DEBUG_TYPE_ERROR ? "** GL ERROR **" : "" ),
            type, severity, message );
}

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

int createSharedEyebuffer(rendered_frame* sharedTexture){

	// Create the shared eye texture handle.
	glGenTextures(1, &(sharedTexture->texture_handle));
    glBindTexture(GL_TEXTURE_2D_ARRAY, sharedTexture->texture_handle);

	// Set the texture parameters for the texture that the FBO will be
    // mapped into.
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGB8, EYE_TEXTURE_WIDTH, EYE_TEXTURE_HEIGHT, 2, 0, GL_RGB, GL_UNSIGNED_BYTE, 0);

	glBindTexture(GL_TEXTURE_2D_ARRAY, 0); // unbind texture, will rebind later

	if(glGetError()){
        return 0;
    } else {
		return 1;
	}
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
		return -1;
	}

	glEnable              ( GL_DEBUG_OUTPUT );
	glDebugMessageCallback( MessageCallback, 0 );

	// Creat our fake eye texture (again, usually would come from Monado's swapchain)
	rendered_frame eye_texture;

	if(!createSharedEyebuffer(&eye_texture)){
		std::cout << "Failed to create shared eye texture resources, quitting" << std::endl;
		glfwDestroyWindow(headless_window);
		glfwTerminate();
		return -1;
	}

	glfwMakeContextCurrent(NULL);

	// this would be set by a config file
	dynamic_lib slam_lib = dynamic_lib::create(std::string_view{argv[1]});
	dynamic_lib cam_lib = dynamic_lib::create(std::string_view{argv[2]});
	dynamic_lib imu_lib = dynamic_lib::create(std::string_view{argv[3]});
	dynamic_lib slam2_lib = dynamic_lib::create(std::string_view{argv[4]});
	dynamic_lib timewarp_gl_lib = dynamic_lib::create(std::string_view{argv[5]});
	dynamic_lib gldemo_lib = dynamic_lib::create(std::string_view{argv[6]});

	/* dynamiclaly load the .so-lib provider and call its factory. I
		use pointers for the polymorphism. */
	std::shared_ptr<abstract_slam> slam = create_from_dynamic_factory(abstract_slam, slam_lib);
	std::shared_ptr<abstract_slam> slam2 = create_from_dynamic_factory(abstract_slam, slam2_lib);
	std::shared_ptr<abstract_cam> cam = create_from_dynamic_factory(abstract_cam, cam_lib);
	std::shared_ptr<abstract_imu> imu = create_from_dynamic_factory(abstract_imu, imu_lib);
	std::shared_ptr<abstract_timewarp> tw = create_from_dynamic_factory(abstract_timewarp, timewarp_gl_lib);
	std::shared_ptr<abstract_gldemo> demo = create_from_dynamic_factory(abstract_gldemo, gldemo_lib);

	// Init the timewarp component, will grab
	// eye buffers at 60fps
	tw->init(eye_texture, headless_window, NULL);

	// Init the demo GL app, it will spin up a thread
	// that draws things into the shared eye buffers for us!
	demo->init(eye_texture, headless_window);

	auto slow_pose = std::make_unique<slow_pose_producer>(slam, cam, imu);

	std::async(std::launch::async, [&](){
		std::default_random_engine generator;
		std::uniform_int_distribution<int> distribution{200, 600};

		std::cout << "Model an XR app by calling for a pose sporadically."
				  << std::endl;

		for (int i = 0; i < 40; ++i) {
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

	return 0;
}
