#include <chrono>
#include <future>
#include <iostream>
#include <thread>
#include <cmath>
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include "common/threadloop.hpp"
#include "common/switchboard.hpp"
#include "common/data_format.hpp"
#include "common/extended_window.hpp"
#include "common/shader_util.hpp"
#include "common/pose_prediction.hpp"
#include "utils/algebra.hpp"
#include "block_i.hpp"
#include "demo_model.hpp"
#include "shaders/blocki_shader.hpp"

using namespace ILLIXR;

static constexpr int   EYE_TEXTURE_WIDTH   = 1024;
static constexpr int   EYE_TEXTURE_HEIGHT  = 1024;

// Monado-style eyebuffers:
// These are two eye textures; however, each eye texture
// represnts a swapchain. eyeTextures[0] is a swapchain of
// left eyes, and eyeTextures[1] is a swapchain of right eyes

// ILLIXR-style eyebuffers:
// These are two eye textures; however, each eye texture
// really contains two eyes. The reason we have two of
// them is for double buffering the Switchboard connection.

// If this is defined, gldemo will use Monado-style eyebuffers
//#define USE_ALT_EYE_FORMAT

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
#ifdef USE_ALT_EYE_FORMAT
		, _m_eyebuffer{sb->publish<rendered_frame_alt>("eyebuffer")}
#else
		, _m_eyebuffer{sb->publish<rendered_frame>("eyebuffer")}
#endif
	{ }


	// Struct for drawable debug objects (scenery, headset visualization, etc)
	struct DebugDrawable {
		DebugDrawable() {}
		DebugDrawable(std::vector<GLfloat> uniformColor) : color(uniformColor) {}

		GLuint num_triangles;
		GLuint positionVBO;
		GLuint positionAttribute;
		GLuint normalVBO;
		GLuint normalAttribute;
		GLuint colorUniform;
		std::vector<GLfloat> color;

		void init(GLuint positionAttribute, GLuint normalAttribute, GLuint colorUniform, GLuint num_triangles, 
					GLfloat* meshData, GLfloat* normalData, GLenum drawMode) {

			this->positionAttribute = positionAttribute;
			this->normalAttribute = normalAttribute;
			this->colorUniform = colorUniform;
			this->num_triangles = num_triangles;

			glGenBuffers(1, &positionVBO);
			glBindBuffer(GL_ARRAY_BUFFER, positionVBO);
			glBufferData(GL_ARRAY_BUFFER, (num_triangles * 3 *3) * sizeof(GLfloat), meshData, drawMode);
			
			glGenBuffers(1, &normalVBO);
			glBindBuffer(GL_ARRAY_BUFFER, normalVBO);
			glBufferData(GL_ARRAY_BUFFER, (num_triangles * 3 * 3) * sizeof(GLfloat), normalData, drawMode);

		}

		void drawMe() {
			glBindBuffer(GL_ARRAY_BUFFER, positionVBO);
			glVertexAttribPointer(positionAttribute, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
			glEnableVertexAttribArray(positionAttribute);
			glBindBuffer(GL_ARRAY_BUFFER, normalVBO);
			glVertexAttribPointer(normalAttribute, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
			glEnableVertexAttribArray(normalAttribute);
			glUniform4fv(colorUniform, 1, color.data());
			glDrawArrays(GL_TRIANGLES, 0, num_triangles * 3);
		}
	};

	void draw_scene() {

		// OBJ exporter is having winding order issues currently.
		// Please excuse the strange GL_CW and GL_CCW mode switches.

		glFrontFace(GL_CW);
		groundObject.drawMe();
		glFrontFace(GL_CCW);
		waterObject.drawMe();
		treesObject.drawMe();
		rocksObject.drawMe();
		glFrontFace(GL_CCW);
	}

	void _p_thread_setup() override {
		// Note: glfwMakeContextCurrent must be called from the thread which will be using it.
		glXMakeCurrent(xwin->dpy, xwin->win, xwin->glc);
	}

	void _p_one_iteration() override {
		{
			using namespace std::chrono_literals;
			// This "app" is "very slow"!
			//std::this_thread::sleep_for(cosf(glfwGetTime()) * 50ms + 100ms);
			std::this_thread::sleep_for(16ms);
			glUseProgram(demoShaderProgram);

			glBindFramebuffer(GL_FRAMEBUFFER, eyeTextureFBO);

			// Determine which set of eye textures to be using.
			int buffer_to_use = which_buffer.load();

			// We'll calculate this model view matrix
			// using fresh pose data, if we have any.
			ksAlgebra::ksMatrix4x4f modelViewMatrix;

			// Model matrix is just a spinny fun animation
			ksAlgebra::ksMatrix4x4f modelMatrix;
			ksAlgebra::ksMatrix4x4f_CreateTranslation(&modelMatrix, 0, 0, 0);

			if (pp->fast_pose_reliable()) {
				// We have a valid pose from our Switchboard plug.

				const pose_type pose = pp->get_fast_pose();
				if(counter == 50){
					std::cerr << "First pose received: quat(wxyz) is " << pose.orientation.w() << ", " << pose.orientation.x() << ", " << pose.orientation.y() << ", " << pose.orientation.z() << std::endl;
					offsetQuat = Eigen::Quaternionf(pose.orientation);
				}

				counter++;

				Eigen::Quaternionf combinedQuat = offsetQuat.inverse() * pose.orientation;

				auto latest_quat = ksAlgebra::ksQuatf {
					.x = combinedQuat.x(),
					.y = combinedQuat.y(),
					.z = combinedQuat.z(),
					.w = combinedQuat.w()
				};

				auto latest_position = ksAlgebra::ksVector3f {
					.x = pose.position[0] + 5.0f,
					.y = pose.position[1] + 2.0f,
					.z = pose.position[2] + -3.0f
				};
				auto scale = ksAlgebra::ksVector3f{1,1,1};
				ksAlgebra::ksMatrix4x4f head_matrix;
				std::cout<< "App using position: " << latest_position.z << std::endl;
				ksAlgebra::ksMatrix4x4f_CreateTranslationRotationScale(&head_matrix, &latest_position, &latest_quat, &scale);
				ksAlgebra::ksMatrix4x4f viewMatrix;
				// View matrix is the inverse of the camera's position/rotation/etc.
				ksAlgebra::ksMatrix4x4f_Invert(&viewMatrix, &head_matrix);
				ksAlgebra::ksMatrix4x4f_Multiply(&modelViewMatrix, &viewMatrix, &modelMatrix);
			} else {
				// We have no pose data from our pose topic :(
				ksAlgebra::ksMatrix4x4f_CreateIdentity(&modelViewMatrix);
			}

			glUseProgram(demoShaderProgram);
			glViewport(0, 0, EYE_TEXTURE_WIDTH, EYE_TEXTURE_HEIGHT);
			glEnable(GL_CULL_FACE);
			glEnable(GL_DEPTH_TEST);
			glClearDepth(1);

			glUniformMatrix4fv(modelViewAttr, 1, GL_FALSE, (GLfloat*)&(modelViewMatrix.m[0][0]));
			glUniformMatrix4fv(projectionAttr, 1, GL_FALSE, (GLfloat*)&(basicProjection.m[0][0]));

			glBindVertexArray(demo_vao);

			#ifdef USE_ALT_EYE_FORMAT

			// Draw things to left eye.
			glBindTexture(GL_TEXTURE_2D, eyeTextures[0]);
			glFramebufferTexture(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, eyeTextures[0], 0);
			glBindTexture(GL_TEXTURE_2D, 0);
			glClearColor(0.6f, 0.8f, 0.9f, 1.0f);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			
			draw_scene();

			
			//glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, idx_vbo);
			//glDrawElements(GL_TRIANGLES, BLOCKI_NUM_POLYS * 3, GL_UNSIGNED_INT, (void*)0);
			
			
			// Draw things to right eye.
			glBindTexture(GL_TEXTURE_2D, eyeTextures[1]);
			glFramebufferTexture(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, eyeTextures[1], 0);
			glBindTexture(GL_TEXTURE_2D, 0);
			glClearColor(0.6f, 0.8f, 0.9f, 1.0f);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

			draw_scene();

			#else
			
			// Draw things to left eye.
			glBindTexture(GL_TEXTURE_2D_ARRAY, eyeTextures[buffer_to_use]);
			glFramebufferTextureLayer(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, eyeTextures[buffer_to_use], 0, 0);
			glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
			glClearColor(0.6f, 0.8f, 0.9f, 1.0f);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			
			draw_scene();

			
			//glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, idx_vbo);
			//glDrawElements(GL_TRIANGLES, BLOCKI_NUM_POLYS * 3, GL_UNSIGNED_INT, (void*)0);
			
			
			// Draw things to right eye.
			glBindTexture(GL_TEXTURE_2D_ARRAY, eyeTextures[buffer_to_use]);
			glFramebufferTextureLayer(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, eyeTextures[buffer_to_use], 0, 1);
			glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
			glClearColor(0.6f, 0.8f, 0.9f, 1.0f);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

			draw_scene();

			#endif

			/*
			glBindBuffer(GL_ARRAY_BUFFER, pos_vbo);
			glVertexAttribPointer(vertexPosAttr, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
			glEnableVertexAttribArray(vertexPosAttr);
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, idx_vbo);
			glDrawElements(GL_TRIANGLES, BLOCKI_NUM_POLYS * 3, GL_UNSIGNED_INT, (void*)0);
			*/

#ifndef NDEBUG
			printf("\033[1;32m[GL DEMO APP]\033[0m Submitting frame to buffer %d, frametime %f, FPS: %f\n", buffer_to_use, (float)(glfwGetTime() - lastTime),  (float)(1.0/(glfwGetTime() - lastTime)));
#endif
			lastTime = glfwGetTime();
			glFlush();

			// Publish our submitted frame handle to Switchboard!
			#ifdef USE_ALT_EYE_FORMAT
			auto frame = new rendered_frame_alt;
			frame->texture_handles[0] = eyeTextures[0];
			frame->texture_handles[1] = eyeTextures[1];
			frame->swap_indices[0] = buffer_to_use;
			frame->swap_indices[1] = buffer_to_use;
			frame->render_pose = pp->get_fast_pose();
			which_buffer.store(buffer_to_use == 1 ? 0 : 1);
			#else
			auto frame = new rendered_frame;
			frame->texture_handle = eyeTextures[buffer_to_use];
			frame->render_pose = pp->get_fast_pose();
			assert(pose);
			which_buffer.store(buffer_to_use == 1 ? 0 : 1);
			#endif

			_m_eyebuffer->put(frame);
		}
	}

private:
	const std::unique_ptr<const xlib_gl_extended_window> xwin;
	const std::shared_ptr<switchboard> sb;
	const std::shared_ptr<const pose_prediction> pp;
	
	// Switchboard plug for application eye buffer.
	// We're not "writing" the actual buffer data,
	// we're just atomically writing the handle to the
	// correct eye/framebuffer in the "swapchain".
	#ifdef USE_ALT_EYE_FORMAT
	std::unique_ptr<writer<rendered_frame_alt>> _m_eyebuffer;
	#else
	std::unique_ptr<writer<rendered_frame>> _m_eyebuffer;
	#endif

	uint counter = 0;
	Eigen::Quaternionf offsetQuat;

	GLuint eyeTextures[2];
	GLuint eyeTextureFBO;
	GLuint eyeTextureDepthTarget;

	// This doesn't really need to be atomic right now,
	// as it's only used by the "app's" thread, but 
	// we'll keep it atomic just in case for now!
	std::atomic<int> which_buffer = 0;


	GLuint demo_vao;
	GLuint demoShaderProgram;

	GLuint vertexPosAttr;
	GLuint vertexNormalAttr;
	GLuint modelViewAttr;
	GLuint projectionAttr;

	GLuint colorUniform;
	
	DebugDrawable groundObject = DebugDrawable({0.1, 0.2, 0.1, 1.0});
	DebugDrawable waterObject =  DebugDrawable({0.0, 0.3, 0.5, 1.0});
	DebugDrawable treesObject =  DebugDrawable({0.0, 0.3, 0.0, 1.0});
	DebugDrawable rocksObject =  DebugDrawable({0.3, 0.3, 0.3, 1.0});


	ksAlgebra::ksMatrix4x4f basicProjection;

	double lastTime;

	int createSharedEyebuffer(GLuint* texture_handle){

		#ifdef USE_ALT_EYE_FORMAT

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

		#else
		// Create the shared eye texture handle.
		glGenTextures(1, texture_handle);
		glBindTexture(GL_TEXTURE_2D_ARRAY, *texture_handle);

		// Set the texture parameters for the texture that the FBO will be
		// mapped into.
		glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_LEVEL, 0);
		glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
		glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
		glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGB8, EYE_TEXTURE_WIDTH, EYE_TEXTURE_HEIGHT, 2, 0, GL_RGB, GL_UNSIGNED_BYTE, 0);

		glBindTexture(GL_TEXTURE_2D_ARRAY, 0); // unbind texture, will rebind later

		#endif

		if(glGetError()){
			return 0;
		} else {
			return 1;
		}
	}

	void createFBO(GLuint* texture_handle, GLuint* fbo, GLuint* depth_target){
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
		printf("About to bind eyebuffer texture, texture handle: %d\n", *texture_handle);

		#ifdef USE_ALT_EYE_FORMAT
		glBindTexture(GL_TEXTURE_2D, *texture_handle);
		glFramebufferTexture(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, *texture_handle, 0);
    	glBindTexture(GL_TEXTURE_2D, 0);
    	#else
		glBindTexture(GL_TEXTURE_2D_ARRAY, *texture_handle);
		glFramebufferTextureLayer(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, *texture_handle, 0, 0);
    	glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    	#endif
		// attach a renderbuffer to depth attachment point
    	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, *depth_target);

		if(glGetError()){
        	printf("displayCB, error after creating fbo\n");
    	}

		// Unbind FBO.
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}

public:
	/* compatibility interface */

	// Dummy "application" overrides _p_start to control its own lifecycle/scheduling.
	// This may be changed later, but it really doesn't matter for this purpose because
	// it will be replaced by a real, Monado-interfaced application.
	virtual void start() override {
		glXMakeCurrent(xwin->dpy, xwin->win, xwin->glc);

		// Initialize the GLFW library, still need it to get time
		if(!glfwInit()){
			printf("Failed to initialize glfw\n");
		}
		// Init and verify GLEW
		if(glewInit()){
			printf("Failed to init GLEW\n");
			exit(0);
		}

		glEnable              ( GL_DEBUG_OUTPUT );
		glDebugMessageCallback( MessageCallback, 0 );

		// Create two shared eye textures.
		// Note; each "eye texture" actually contains two eyes.
		// The two eye textures here are actually for double-buffering
		// the Switchboard connection.
		createSharedEyebuffer(&(eyeTextures[0]));
		createSharedEyebuffer(&(eyeTextures[1]));

		// Initialize FBO and depth targets, attaching to the frame handle
		createFBO(&(eyeTextures[0]), &eyeTextureFBO, &eyeTextureDepthTarget);

		// Create and bind global VAO object
		glGenVertexArrays(1, &demo_vao);
    	glBindVertexArray(demo_vao);

		demoShaderProgram = init_and_link(blocki_vertex_shader, blocki_fragment_shader);
#ifndef NDEBUG
		std::cout << "Demo app shader program is program " << demoShaderProgram << std::endl;
#endif

		vertexPosAttr = glGetAttribLocation(demoShaderProgram, "vertexPosition");
		vertexNormalAttr = glGetAttribLocation(demoShaderProgram, "vertexNormal");
		modelViewAttr = glGetUniformLocation(demoShaderProgram, "u_modelview");
		projectionAttr = glGetUniformLocation(demoShaderProgram, "u_projection");

		colorUniform = glGetUniformLocation(demoShaderProgram, "u_color");

		
		groundObject.init(vertexPosAttr,
			vertexNormalAttr,
			colorUniform,
			Ground_plane_NUM_TRIANGLES,
			&(Ground_Plane_vertex_data[0]),
			&(Ground_Plane_normal_data[0]),
			GL_STATIC_DRAW
		);

		waterObject.init(vertexPosAttr,
			vertexNormalAttr,
			colorUniform,
			Water_plane001_NUM_TRIANGLES,
			&(Water_Plane001_vertex_data[0]),
			&(Water_Plane001_normal_data[0]),
			GL_STATIC_DRAW
		);

		treesObject.init(vertexPosAttr,
			vertexNormalAttr,
			colorUniform,
			Trees_cone_NUM_TRIANGLES,
			&(Trees_Cone_vertex_data[0]),
			&(Trees_Cone_normal_data[0]),
			GL_STATIC_DRAW
		);

		rocksObject.init(vertexPosAttr,
			vertexNormalAttr,
			colorUniform,
			Rocks_plane002_NUM_TRIANGLES,
			&(Rocks_Plane002_vertex_data[0]),
			&(Rocks_Plane002_normal_data[0]),
			GL_STATIC_DRAW
		);
		
		// Construct a basic perspective projection
		ksAlgebra::ksMatrix4x4f_CreateProjectionFov( &basicProjection, 40.0f, 40.0f, 40.0f, 40.0f, 0.03f, 20.0f );

		glXMakeCurrent(xwin->dpy, None, NULL);

		lastTime = glfwGetTime();

		threadloop::start();
	}
};

PLUGIN_MAIN(gldemo)
