#include <chrono>
#include <future>
#include <iostream>
#include <thread>
#include <cmath>
#include <array>
#include <GL/glew.h>
#include "common/threadloop.hpp"
#include "common/switchboard.hpp"
#include "common/data_format.hpp"
#include "common/extended_window.hpp"
#include "common/shader_util.hpp"
#include "common/math_util.hpp"
#include "common/pose_prediction.hpp"
#include "common/gl_util/obj.hpp"
#include "shaders/demo_shader.hpp"
#include "common/global_module_defs.hpp"
#include "common/error_util.hpp"

using namespace ILLIXR;

static constexpr int   EYE_TEXTURE_WIDTH   = ILLIXR::FB_WIDTH;
static constexpr int   EYE_TEXTURE_HEIGHT  = ILLIXR::FB_HEIGHT;

static constexpr std::chrono::nanoseconds vsync_period {std::chrono::nanoseconds{std::chrono::seconds{1}} / 60};
static constexpr std::chrono::nanoseconds VSYNC_DELAY_TIME {std::chrono::milliseconds{2}};

// Monado-style eyebuffers:
// These are two eye textures; however, each eye texture
// represnts a swapchain. eyeTextures[0] is a swapchain of
// left eyes, and eyeTextures[1] is a swapchain of right eyes


class gldemo : public threadloop {
public:
	// Public constructor, create_component passes Switchboard handles ("plugs")
	// to this constructor. In turn, the constructor fills in the private
	// references to the switchboard plugs, so the component can read the
	// data whenever it needs to.

	gldemo(std::string name_, phonebook* pb_)
		: threadloop{name_, pb_}
		, xwin{new xlib_gl_extended_window{1, 1, pb->lookup_impl<xlib_gl_extended_window>()->glc}}
		, sb{pb->lookup_impl<switchboard>()}
		//, xwin{pb->lookup_impl<xlib_gl_extended_window>()}
		, pp{pb->lookup_impl<pose_prediction>()}
		, _m_vsync{sb->get_reader<switchboard::event_wrapper<time_point>>("vsync_estimate")}
		, _m_eyebuffer{sb->get_writer<rendered_frame>("eyebuffer")}
		, _m_clock{pb->lookup_impl<RelativeClock>()}
	{ }

	// Essentially, a crude equivalent of XRWaitFrame.
	void wait_vsync()
	{
		using namespace std::chrono_literals;
		switchboard::ptr<const switchboard::event_wrapper<time_point>> next_vsync = _m_vsync.get_ro_nullable();
		time_point now = _m_clock->now();

		time_point wait_time;

		if (next_vsync == nullptr) {
			// If no vsync data available, just sleep for roughly a vsync period.
			// We'll get synced back up later.
			std::this_thread::sleep_for(vsync_period);
			return;
		}

#ifndef NDEBUG
		if (log_count > LOG_PERIOD) {
			double vsync_in = std::chrono::duration<double, std::milli>{*next_vsync - now}.count();
            std::cout << "\033[1;32m[GL DEMO APP]\033[0m First vsync is in " << vsync_in << "ms" << std::endl;
		}
#endif
		
		bool hasRenderedThisInterval = (now - lastFrameTime) < vsync_period;

		// If less than one frame interval has passed since we last rendered...
		if (hasRenderedThisInterval)
		{
			// We'll wait until the next vsync, plus a small delay time.
			// Delay time helps with some inaccuracies in scheduling.
			wait_time = **next_vsync + VSYNC_DELAY_TIME;

			// If our sleep target is in the past, bump it forward
			// by a vsync period, so it's always in the future.
			while(wait_time < now)
			{
				wait_time += vsync_period;
			}

#ifndef NDEBUG
			if (log_count > LOG_PERIOD) {
				double wait_in = std::chrono::duration<double, std::milli>{wait_time - now}.count()
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
		RAC_ERRNO_MSG("gldemo at start of _p_thread_setup");

		// Note: glXMakeContextCurrent must be called from the thread which will be using it.
        [[maybe_unused]] const bool gl_result = static_cast<bool>(glXMakeCurrent(xwin->dpy, xwin->win, xwin->glc));
		assert(gl_result && "glXMakeCurrent should not fail");

		RAC_ERRNO_MSG("gldemo at end of _p_thread_setup");
	}

	void _p_one_iteration() override {
		{
			using namespace std::chrono_literals;

			// Essentially, XRWaitFrame.
			wait_vsync();

			glUseProgram(demoShaderProgram);
			glBindFramebuffer(GL_FRAMEBUFFER, eyeTextureFBO);

			// Determine which set of eye textures to be using.

			glUseProgram(demoShaderProgram);
			glBindVertexArray(demo_vao);
			glViewport(0, 0, EYE_TEXTURE_WIDTH, EYE_TEXTURE_HEIGHT);

			glEnable(GL_CULL_FACE);
			glEnable(GL_DEPTH_TEST);

			glClearDepth(1);

			// We'll calculate this model view matrix
			// using fresh pose data, if we have any.
			Eigen::Matrix4f modelViewMatrix;

			Eigen::Matrix4f modelMatrix = Eigen::Matrix4f::Identity();

			const fast_pose_type fast_pose = pp->get_fast_pose();
			pose_type pose = fast_pose.pose;

			Eigen::Matrix3f head_rotation_matrix = pose.orientation.toRotationMatrix();

			// 64mm IPD, why not
			// (TODO FIX, pull from centralized config!)
			// 64mm is also what TW currently uses through HMD::GetDefaultBodyInfo.
			// Unfortunately HMD:: namespace is currently private to TW. Need to
			// integrate as a config topic that can share HMD info.
			float ipd = 0.0640f; 

			// Excessive? Maybe.
			constexpr int LEFT_EYE = 0;

			for(auto eye_idx = 0; eye_idx < 2; eye_idx++) {

				// Offset of eyeball from pose
				auto eyeball = Eigen::Vector3f((eye_idx == LEFT_EYE ? -ipd/2.0f : ipd/2.0f), 0, 0);

				// Apply head rotation to eyeball offset vector
				eyeball = head_rotation_matrix * eyeball;

				// Apply head position to eyeball
				eyeball += pose.position;

				// Build our eye matrix from the pose's position + orientation.
				Eigen::Matrix4f eye_matrix = Eigen::Matrix4f::Identity();
				eye_matrix.block<3,1>(0,3) = eyeball; // Set position to eyeball's position
				eye_matrix.block<3,3>(0,0) = pose.orientation.toRotationMatrix();

				// Objects' "view matrix" is inverse of eye matrix.
				auto view_matrix = eye_matrix.inverse();

				Eigen::Matrix4f modelViewMatrix = modelMatrix * view_matrix;
				glUniformMatrix4fv(modelViewAttr, 1, GL_FALSE, (GLfloat*)(modelViewMatrix.data()));
				glUniformMatrix4fv(projectionAttr, 1, GL_FALSE, (GLfloat*)(basicProjection.data()));
				
				glBindTexture(GL_TEXTURE_2D, eyeTextures[eye_idx]);
				glFramebufferTexture(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, eyeTextures[eye_idx], 0);
				glBindTexture(GL_TEXTURE_2D, 0);
				glClearColor(0.9f, 0.9f, 0.9f, 1.0f);

				RAC_ERRNO_MSG("gldemo before glClear");
				glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                RAC_ERRNO_MSG("gldemo after glClear");
				
				demoscene.Draw();
			}

#ifndef NDEBUG
            const time_point now = _m_clock->now();
			const double frame_duration_s = std::chrono::duration<double, std::chrono::seconds::period>{now - lastTime}.count();
            const double fps = 1.0 / frame_duration_s;

			if (log_count > LOG_PERIOD) {
                std::cout << "\033[1;32m[GL DEMO APP]\033[0m Submitting frame to buffer " << which_buffer
                          << ", frametime: " << frame_duration_s
                          << ", FPS: " << fps
                          << std::endl;
			}
#endif
			lastTime = _m_clock->now();

			glFlush();

			/// Publish our submitted frame handle to Switchboard!
			auto lastFrameTime = _m_clock->now();
            _m_eyebuffer.put(_m_eyebuffer.allocate<rendered_frame>(rendered_frame{
                // Somehow, C++ won't let me construct thsi object if I remove the `rendered_frame{` and `}`.
				// `allocate<rendered_frame>(...)` _should_ forward the arguments to rendered_frame's constructor, but I guess not.
                std::array<GLuint, 2>{ eyeTextures[0], eyeTextures[1] },
                std::array<GLuint, 2>{ which_buffer, which_buffer },
                fast_pose,
                fast_pose.predict_computed_time,
                lastFrameTime
			}));

			which_buffer = !which_buffer;
		}

#ifndef NDEBUG
		if (log_count > LOG_PERIOD) {
			log_count = 0;
		} else {
			log_count++;
		}
#endif

        RAC_ERRNO_MSG("gldemo at end of _p_one_iteration");
	}

#ifndef NDEBUG
	size_t log_count = 0;
	size_t LOG_PERIOD = 20;
#endif

private:
	const std::unique_ptr<const xlib_gl_extended_window> xwin;
	const std::shared_ptr<switchboard> sb;
	const std::shared_ptr<pose_prediction> pp;
	const switchboard::reader<switchboard::event_wrapper<time_point>> _m_vsync;

	// Switchboard plug for application eye buffer.
	// We're not "writing" the actual buffer data,
	// we're just atomically writing the handle to the
	// correct eye/framebuffer in the "swapchain".
	switchboard::writer<rendered_frame> _m_eyebuffer;

	time_point lastFrameTime;

	GLuint eyeTextures[2];
	GLuint eyeTextureFBO;
	GLuint eyeTextureDepthTarget;

	unsigned char which_buffer = 0;

	GLuint demo_vao;
	GLuint demoShaderProgram;

	GLuint vertexPosAttr;
	GLuint vertexNormalAttr;
	GLuint modelViewAttr;
	GLuint projectionAttr;

	GLuint colorUniform;

	ObjScene demoscene;

	Eigen::Matrix4f basicProjection;

	time_point lastTime;

	const std::shared_ptr<const RelativeClock> _m_clock;

	int createSharedEyebuffer(GLuint* texture_handle){

		// Create the shared eye texture handle.
		glGenTextures(1, texture_handle);
		glBindTexture(GL_TEXTURE_2D, *texture_handle);

		// Set the texture parameters for the texture that the FBO will be
		// mapped into.
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, EYE_TEXTURE_WIDTH, EYE_TEXTURE_HEIGHT, 0, GL_RGB, GL_UNSIGNED_BYTE, 0);

		glBindTexture(GL_TEXTURE_2D, 0); // unbind texture, will rebind later

        const GLenum gl_err = glGetError();
		if (gl_err != GL_NO_ERROR) {
			RAC_ERRNO_MSG("[gldemo] failed error check in createSharedEyebuffer");
			return 1;
		} else {
			RAC_ERRNO();
			return 0;
		}
	}

	void createFBO(GLuint* texture_handle, GLuint* fbo, GLuint* depth_target){
        RAC_ERRNO_MSG("gldemo at start of createFBO");

		// Create a framebuffer to draw some things to the eye texture
		glGenFramebuffers(1, fbo);

		// Bind the FBO as the active framebuffer.
    	glBindFramebuffer(GL_FRAMEBUFFER, *fbo);
		glGenRenderbuffers(1, depth_target);
    	glBindRenderbuffer(GL_RENDERBUFFER, *depth_target);
    	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, EYE_TEXTURE_WIDTH, EYE_TEXTURE_HEIGHT);
    	//glRenderbufferStorageMultisample(GL_RENDERBUFFER, fboSampleCount, GL_DEPTH_COMPONENT, EYE_TEXTURE_WIDTH, EYE_TEXTURE_HEIGHT);

    	glBindRenderbuffer(GL_RENDERBUFFER, 0);

		// Bind eyebuffer texture
        std::cout << "About to bind eyebuffer texture, texture handle: " << *texture_handle << std::endl;

		glBindTexture(GL_TEXTURE_2D, *texture_handle);
		glFramebufferTexture(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, *texture_handle, 0);
    	glBindTexture(GL_TEXTURE_2D, 0);

		// attach a renderbuffer to depth attachment point
    	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, *depth_target);

		if (glGetError()) {
            std::cerr << "displayCB, error after creating fbo" << std::endl;
    	}
        RAC_ERRNO_MSG("gldemo after calling glGetError");

		// Unbind FBO.
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}

public:
	/* compatibility interface */

	// Dummy "application" overrides _p_start to control its own lifecycle/scheduling.
	virtual void start() override {
		RAC_ERRNO_MSG("gldemo at start of gldemo start function");

        [[maybe_unused]] const bool gl_result_0 = static_cast<bool>(glXMakeCurrent(xwin->dpy, xwin->win, xwin->glc));
		assert(gl_result_0 && "glXMakeCurrent should not fail");
		RAC_ERRNO_MSG("gldemo after glXMakeCurrent");

		// Init and verify GLEW
		const GLenum glew_err = glewInit();
		if (glew_err != GLEW_OK) {
            std::cerr << "[gldemo] GLEW Error: " << glewGetErrorString(glew_err) << std::endl;
            ILLIXR::abort("[gldemo] Failed to initialize GLEW");
		}
		RAC_ERRNO_MSG("gldemo after glewInit");

		glEnable(GL_DEBUG_OUTPUT);
		glDebugMessageCallback(MessageCallback, 0);

		// Create two shared eye textures.
		// Note; each "eye texture" actually contains two eyes.
		// The two eye textures here are actually for double-buffering
		// the Switchboard connection.
		createSharedEyebuffer(&(eyeTextures[0]));
		createSharedEyebuffer(&(eyeTextures[1]));

        RAC_ERRNO_MSG("gldemo after creating eye buffers");

		// Initialize FBO and depth targets, attaching to the frame handle
		createFBO(&(eyeTextures[0]), &eyeTextureFBO, &eyeTextureDepthTarget);

        RAC_ERRNO_MSG("gldemo after creating FBO");

		// Create and bind global VAO object
		glGenVertexArrays(1, &demo_vao);
    	glBindVertexArray(demo_vao);

		demoShaderProgram = init_and_link(demo_vertex_shader, demo_fragment_shader);
#ifndef NDEBUG
		std::cout << "Demo app shader program is program " << demoShaderProgram << std::endl;
#endif

		vertexPosAttr = glGetAttribLocation(demoShaderProgram, "vertexPosition");
		vertexNormalAttr = glGetAttribLocation(demoShaderProgram, "vertexNormal");
		modelViewAttr = glGetUniformLocation(demoShaderProgram, "u_modelview");
		projectionAttr = glGetUniformLocation(demoShaderProgram, "u_projection");
		colorUniform = glGetUniformLocation(demoShaderProgram, "u_color");
		RAC_ERRNO_MSG("gldemo after glGetUniformLocation");

		// Load/initialize the demo scene.

		char* obj_dir = std::getenv("ILLIXR_DEMO_DATA");
		if (obj_dir == nullptr) {
            ILLIXR::abort("Please define ILLIXR_DEMO_DATA.");
		}
		demoscene = ObjScene(std::string(obj_dir), "scene.obj");

		// Construct a basic perspective projection
		math_util::projection_fov( &basicProjection, 40.0f, 40.0f, 40.0f, 40.0f, 0.03f, 20.0f );

		RAC_ERRNO_MSG("gldemo before glXMakeCurrent");
        [[maybe_unused]] const bool gl_result_1 = static_cast<bool>(glXMakeCurrent(xwin->dpy, None, nullptr));
		assert(gl_result_1 && "glXMakeCurrent should not fail");
		RAC_ERRNO_MSG("gldemo after glXMakeCurrent");

		lastTime = _m_clock->now();

		threadloop::start();

		RAC_ERRNO_MSG("gldemo at end of start()");
	}
};

PLUGIN_MAIN(gldemo)
