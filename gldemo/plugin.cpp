#include <chrono>
#include <future>
#include <iostream>
#include <thread>
#include <cmath>
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <png.h>
#include "common/threadloop.hpp"
#include "common/switchboard.hpp"
#include "common/data_format.hpp"
#include "common/extended_window.hpp"
#include "common/shader_util.hpp"
#include "common/math_util.hpp"
#include "common/pose_prediction.hpp"
#include "block_i.hpp"
#include "demo_model.hpp"
#include "shaders/blocki_shader.hpp"

#define TINYOBJLOADER_IMPLEMENTATION
#include "lib/tiny_obj_loader.h"

#define STB_IMAGE_IMPLEMENTATION
#include "lib/stb_image.h"

using namespace ILLIXR;

static constexpr int   EYE_TEXTURE_WIDTH   = 1024;
static constexpr int   EYE_TEXTURE_HEIGHT  = 1024;

static constexpr std::chrono::nanoseconds vsync_period {std::size_t(NANO_SEC/60)};
static constexpr std::chrono::milliseconds VSYNC_DELAY_TIME {std::size_t{2}};

// C++20 allows for string constexprs, TODO upgrade?
static constexpr char scene_filename[] = "scene.obj";

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
		, vsync{sb->subscribe_latest<time_type>("vsync_estimate")}
#ifdef USE_ALT_EYE_FORMAT
		, _m_eyebuffer{sb->publish<rendered_frame_alt>("eyebuffer")}
#else
		, _m_eyebuffer{sb->publish<rendered_frame>("eyebuffer")}
#endif
	{ }

	struct vertex_t {
		GLfloat position[3];
		GLfloat uv[2];
	};

	// Struct for drawable debug objects (scenery, headset visualization, etc)
	struct object_t {
		GLuint vbo_handle;
		GLuint num_triangles;

		void Draw() {
			glBindBuffer(GL_ARRAY_BUFFER, vbo_handle);
			glEnableVertexAttribArray(0);
			glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(vertex_t), (void*)offsetof(vertex_t, position));
			glEnableVertexAttribArray(1);
			glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(vertex_t), (void*)offsetof(vertex_t, uv));

			glDrawArrays(GL_TRIANGLES, 0, num_triangles * 3);
		}
	};

	class ObjScene {
		public:
		ObjScene() {}
		ObjScene(std::string obj_filename, std::string tex_filename) {

			// If any of the following procedures fail to correctly load,
			// we'll set this flag false (for the relevant operation)
			successfully_loaded_model = true;
			successfully_loaded_texture = true;

			std::string warn, err;
			bool success = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, obj_filename.c_str());
			if(!warn.empty()){
				std::cout << "[OBJ WARN] " << warn << std::endl;
			}
			if(!err.empty()){
				std::cout << "[OBJ ERROR] " << err << std::endl;
				successfully_loaded_model = false;
			}
			if(!success){
				std::cout << "[OBJ FATAL] Loading of " << obj_filename << " failed." << std::endl;
				successfully_loaded_model = false;
			} else {

				// Process mesh data.
				// Iterate over "shapes" (objects in .obj file)
				for(size_t shape_idx = 0; shape_idx < shapes.size(); shape_idx++){
					
					std::cout << "[OBJ INFO] Num verts in shape: " << shapes[shape_idx].mesh.indices.size() << std::endl;
					std::cout << "[OBJ INFO] Num tris in shape: " << shapes[shape_idx].mesh.indices.size() / 3 << std::endl;

					// Unified buffer for pos + uv. Interleaving vertex data (good practice!)
					std::vector<vertex_t> buffer;
					// Iterate over triangles
					for(size_t tri_idx = 0; tri_idx < shapes[shape_idx].mesh.indices.size() / 3; tri_idx++){
						tinyobj::index_t idx0 = shapes[shape_idx].mesh.indices[3 * tri_idx + 0];
						tinyobj::index_t idx1 = shapes[shape_idx].mesh.indices[3 * tri_idx + 1];
						tinyobj::index_t idx2 = shapes[shape_idx].mesh.indices[3 * tri_idx + 2];

						// Unfortunately we have to unpack/linearize the polygons
						// because OpenGL can't use OBJ-style indices :/

						float verts[3][3]; // [vert][xyz]
						int f0 = idx0.vertex_index;
						int f1 = idx1.vertex_index;
						int f2 = idx2.vertex_index;
						for(int axis = 0; axis < 3; axis++){
							verts[0][axis] = attrib.vertices[3 * f0 + axis];
							verts[1][axis] = attrib.vertices[3 * f1 + axis];
							verts[2][axis] = attrib.vertices[3 * f2 + axis];
						}

						float tex_coords[3][2] = {{0,0},{0,0},{0,0}}; // [vert][uv] for each vertex.
						if(attrib.texcoords.size() > 0){
							if ((idx0.texcoord_index >= 0) || (idx1.texcoord_index >= 0) ||
								(idx2.texcoord_index >= 0)) {

								// Flip Y coord.
								tex_coords[0][0] = attrib.texcoords[2 * idx0.texcoord_index];
								tex_coords[0][1] = 1.0f - attrib.texcoords[2 * idx0.texcoord_index + 1];
								tex_coords[1][0] = attrib.texcoords[2 * idx1.texcoord_index];
								tex_coords[1][1] = 1.0f - attrib.texcoords[2 * idx1.texcoord_index + 1];
								tex_coords[2][0] = attrib.texcoords[2 * idx2.texcoord_index];
								tex_coords[2][1] = 1.0f - attrib.texcoords[2 * idx2.texcoord_index + 1];
							}
						}

						for(int vert = 0; vert < 3; vert++){
							buffer.push_back( vertex_t {
								.position = {verts[vert][0], verts[vert][1], verts[vert][2]},
								.uv = {tex_coords[vert][0], tex_coords[vert][1]}
							});
						}
						
					}

					std::cout << "[OBJ INFO] Num vertices: " << buffer.size() << std::endl;

					object_t newObject;
					newObject.vbo_handle = 0;
					newObject.num_triangles = 0;

					if(buffer.size() > 0){

						// Create/bind/fill vbo.
						glGenBuffers(1, &newObject.vbo_handle);
						glBindBuffer(GL_ARRAY_BUFFER, newObject.vbo_handle);
						glBufferData(GL_ARRAY_BUFFER, buffer.size() * sizeof(vertex_t), &buffer.at(0), GL_STATIC_DRAW);
						glBindBuffer(GL_ARRAY_BUFFER, 0);

						// Compute the number of triangles for this object.
						newObject.num_triangles = buffer.size() / 3;
					}

					objects.push_back(newObject);
				}
			}


			if(!tex_filename.empty()){
				int x,y,n;
				unsigned char* texture_data = stbi_load(tex_filename.c_str(), &x, &y, &n, 0);

				if(texture_data == NULL){
					std::cout << "[TEXTURE ERROR] Loading of " << tex_filename << "failed." << std::endl;
					successfully_loaded_texture = false;
				} else {
					std::cout << "[TEXTURE INFO] Loaded " << tex_filename <<
								": Resolution (" << x << ", " << y << ")" << std::endl;

					// Create and bind OpenGL resource.
					glGenTextures(1, &texture_handle);
					glBindTexture(GL_TEXTURE_2D, texture_handle);
					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

					// Configure number of color channels in texture.
					if(n == 3){
						// 3-channel -> RGB
						glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, x, y, 0, GL_RGB, GL_UNSIGNED_BYTE, texture_data);
					} else if(n == 4) {
						// 4-channel -> RGBA
						glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, x, y, 0, GL_RGBA, GL_UNSIGNED_BYTE, texture_data);
					}
					
					// Unbind.
					glBindTexture(GL_TEXTURE_2D, 0);

				}

				// Free stbi image regardless of load success
				stbi_image_free(texture_data);
			} else {
				std::cout << "[TEXTURE INFO] No texture specified." << std::endl;
			}

			
		}

		~ObjScene() {
		}

		void Draw(GLuint attrib) {
			glBindTexture(GL_TEXTURE_2D, texture_handle);
			for(auto obj : objects){
				obj.Draw();
			}
		}

		bool successfully_loaded_model = false;
		bool successfully_loaded_texture = false;

		tinyobj::attrib_t attrib;
		std::vector<tinyobj::shape_t> shapes;
		std::vector<tinyobj::material_t> materials;
		
		GLuint texture_handle;

		std::vector<object_t> objects;
	};

	// Essentially, a crude equivalent of XRWaitFrame.
	void wait_vsync()
	{
		using namespace std::chrono_literals;
		const time_type* next_vsync = vsync->get_latest_ro();
		time_type now = std::chrono::high_resolution_clock::now();

		time_type wait_time;

		if(next_vsync == nullptr)
		{
			// If no vsync data available, just sleep for roughly a vsync period.
			// We'll get synced back up later.
			std::this_thread::sleep_for(vsync_period);
			return;
		}

#ifndef NDEBUG
		std::chrono::duration<double, std::milli> vsync_in = *next_vsync - now;
		printf("\033[1;32m[GL DEMO APP]\033[0m First vsync is in %4fms\n", vsync_in.count());
#endif
		
		bool hasRenderedThisInterval = (now - lastFrameTime) < vsync_period;

		// If less than one frame interval has passed since we last rendered...
		if(hasRenderedThisInterval)
		{
			// We'll wait until the next vsync, plus a small delay time.
			// Delay time helps with some inaccuracies in scheduling.
			wait_time = *next_vsync + VSYNC_DELAY_TIME;

			// If our sleep target is in the past, bump it forward
			// by a vsync period, so it's always in the future.
			while(wait_time < now)
			{
				wait_time += vsync_period;
			}
#ifndef NDEBUG
			std::chrono::duration<double, std::milli> wait_in = wait_time - now;
			printf("\033[1;32m[GL DEMO APP]\033[0m Waiting until next vsync, in %4fms\n", wait_in.count());
#endif
		} else {
#ifndef NDEBUG
			printf("\033[1;32m[GL DEMO APP]\033[0m We haven't rendered yet, rendering immediately.");
#endif
			return;
		}

		// Perform the sleep.
		// TODO: Consider using Monado-style sleeping, where we nanosleep for
		// most of the wait, and then spin-wait for the rest?
		std::this_thread::sleep_until(wait_time);
	}

	void _p_thread_setup() override {
		// Note: glfwMakeContextCurrent must be called from the thread which will be using it.
		glXMakeCurrent(xwin->dpy, xwin->win, xwin->glc);
	}

	void _p_one_iteration() override {
		{
			using namespace std::chrono_literals;

			// Essentially, XRWaitFrame.
			wait_vsync();

			glUseProgram(demoShaderProgram);

			glBindFramebuffer(GL_FRAMEBUFFER, eyeTextureFBO);

			// Determine which set of eye textures to be using.
			int buffer_to_use = which_buffer.load();

			// We'll calculate this model view matrix
			// using fresh pose data, if we have any.
			Eigen::Matrix4f modelViewMatrix;

			Eigen::Matrix4f modelMatrix = Eigen::Matrix4f::Identity();

			{
				const fast_pose_type fast_pose = pp->get_fast_pose();
				const pose_type pose = fast_pose.pose;

				// Build our head matrix from the pose's position + orientation.
				Eigen::Matrix4f head_matrix = Eigen::Matrix4f::Identity();
				head_matrix.block<3,1>(0,3) = pose.position;
				head_matrix.block<3,3>(0,0) = pose.orientation.toRotationMatrix();

				// View matrix is inverse of head matrix.
				Eigen::Matrix4f viewMatrix = head_matrix.inverse();

				modelViewMatrix = modelMatrix * viewMatrix;
			}

			glUseProgram(demoShaderProgram);
			glViewport(0, 0, EYE_TEXTURE_WIDTH, EYE_TEXTURE_HEIGHT);
			glEnable(GL_CULL_FACE);
			glEnable(GL_DEPTH_TEST);
			glClearDepth(1);

			glUniformMatrix4fv(modelViewAttr, 1, GL_FALSE, (GLfloat*)(modelViewMatrix.data()));
			glUniformMatrix4fv(projectionAttr, 1, GL_FALSE, (GLfloat*)(basicProjection.data()));

			glBindVertexArray(demo_vao);

			#ifdef USE_ALT_EYE_FORMAT

			// Draw things to left eye.
			glBindTexture(GL_TEXTURE_2D, eyeTextures[0]);
			glFramebufferTexture(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, eyeTextures[0], 0);
			glBindTexture(GL_TEXTURE_2D, 0);
			glClearColor(0.6f, 0.8f, 0.9f, 1.0f);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			
			demoscene->Draw(0);

			
			//glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, idx_vbo);
			//glDrawElements(GL_TRIANGLES, BLOCKI_NUM_POLYS * 3, GL_UNSIGNED_INT, (void*)0);
			
			
			// Draw things to right eye.
			glBindTexture(GL_TEXTURE_2D, eyeTextures[1]);
			glFramebufferTexture(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, eyeTextures[1], 0);
			glBindTexture(GL_TEXTURE_2D, 0);
			glClearColor(0.6f, 0.8f, 0.9f, 1.0f);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

			demoscene->Draw(0);

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

			const fast_pose_type fast_pose = pp->get_fast_pose();
			frame->render_pose = fast_pose;
			which_buffer.store(buffer_to_use == 1 ? 0 : 1);
			#else
			auto frame = new rendered_frame;
			frame->texture_handle = eyeTextures[buffer_to_use];
			frame->render_pose = pp->get_fast_pose();
			assert(pose);
			which_buffer.store(buffer_to_use == 1 ? 0 : 1);
			#endif
			frame->render_time = std::chrono::high_resolution_clock::now();
			_m_eyebuffer->put(frame);
			lastFrameTime = std::chrono::high_resolution_clock::now();
		}
	}

private:
	const std::unique_ptr<const xlib_gl_extended_window> xwin;
	const std::shared_ptr<switchboard> sb;
	const std::shared_ptr<pose_prediction> pp;
	const std::unique_ptr<reader_latest<time_type>> vsync;

	// Switchboard plug for application eye buffer.
	// We're not "writing" the actual buffer data,
	// we're just atomically writing the handle to the
	// correct eye/framebuffer in the "swapchain".
	#ifdef USE_ALT_EYE_FORMAT
	std::unique_ptr<writer<rendered_frame_alt>> _m_eyebuffer;
	#else
	std::unique_ptr<writer<rendered_frame>> _m_eyebuffer;
	#endif

	time_type lastFrameTime;

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

	ObjScene* demoscene;


	Eigen::Matrix4f basicProjection;

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

		demoShaderProgram = init_and_link(demo_vertex_shader, demo_fragment_shader);
#ifndef NDEBUG
		std::cout << "Demo app shader program is program " << demoShaderProgram << std::endl;
#endif

		vertexPosAttr = glGetAttribLocation(demoShaderProgram, "vertexPosition");
		vertexNormalAttr = glGetAttribLocation(demoShaderProgram, "vertexNormal");
		modelViewAttr = glGetUniformLocation(demoShaderProgram, "u_modelview");
		projectionAttr = glGetUniformLocation(demoShaderProgram, "u_projection");

		colorUniform = glGetUniformLocation(demoShaderProgram, "u_color");

		// Load/initialize the demo scene.
		demoscene = new ObjScene("/home/finn/ILLIXR/gldemo/demo.obj", "/home/finn/ILLIXR/gldemo/demo.png");
		
		// Construct a basic perspective projection
		math_util::projection_fov( &basicProjection, 40.0f, 40.0f, 40.0f, 40.0f, 0.03f, 20.0f );

		glXMakeCurrent(xwin->dpy, None, NULL);

		lastTime = glfwGetTime();

		threadloop::start();
	}
};

PLUGIN_MAIN(gldemo)
