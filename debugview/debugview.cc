#include <chrono>
#include <future>
#include <iostream>
#include <thread>

// IMGUI Immediate-mode GUI library
#include "imgui/imgui.h"
#include "imgui/imgui_impl_glfw.h"
#include "imgui/imgui_impl_opengl3.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include "common/plugin.hh"
#include "common/switchboard.hh"
#include "common/data_format.hh"
#include "common/shader_util.hh"
#include "utils/algebra.hh"
#include "block_i.hh"
#include "demo_model.hh"
#include "headset_model.hh"
#include "shaders/blocki_shader.hh"
#include <cmath>

using namespace ILLIXR;

// Loosely inspired by
// http://spointeau.blogspot.com/2013/12/hello-i-am-looking-at-opengl-3.html

Eigen::Matrix4f lookAt(Eigen::Vector3f eye, Eigen::Vector3f target, Eigen::Vector3f up){
	using namespace Eigen;
	Vector3f lookDir = (target - eye).normalized();
	Vector3f upDir = up.normalized();
	Vector3f sideDir = lookDir.cross(upDir).normalized();
	upDir = sideDir.cross(lookDir);

	Matrix4f result;
	result << sideDir.x(),  sideDir.y(),  sideDir.z(),-sideDir.dot(eye),
			  upDir.x(),    upDir.y(),    upDir.z(),  -upDir.dot(eye),
			 -lookDir.x(), -lookDir.y(), -lookDir.z(), lookDir.dot(eye),
			 0,             0,            0,           1;

	return result;

}

class debugview : public plugin {
public:

	// Public constructor, create_component passes Switchboard handles ("plugs")
	// to this constructor. In turn, the constructor fills in the private
	// references to the switchboard plugs, so the component can read the
	// data whenever it needs to.
	debugview(phonebook *pb)
		: sb{pb->lookup_impl<switchboard>()}
		, _m_fast_pose{sb->subscribe_latest<pose_type>("fast_pose")}
		, _m_slow_pose{sb->subscribe_latest<pose_type>("slow_pose")}
		, _m_true_pose{sb->subscribe_latest<pose_type>("fast_true_pose")}
		, glfw_context{pb->lookup_impl<global_config>()->glfw_context}
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

	void draw_GUI() {
		// Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
		ImGui::SetNextWindowSize(ImVec2(200, 900), ImGuiCond_FirstUseEver);
		ImGui::Begin("ILLIXR Debug View");

		ImGui::Text("Adjust options for the runtime debug view.");
		ImGui::Spacing();

		if(ImGui::CollapsingHeader("Headset visualization options", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Checkbox("Follow headset position", &follow_headset);

			ImGui::SliderFloat("View distance ", &view_dist, 0.1f, 10.0f);

			ImGui::SliderFloat3("Tracking \"offset\"", tracking_position_offset.data(), -10.0f, 10.0f);

			if(ImGui::Button("Reset")){
				tracking_position_offset = Eigen::Vector3f{5.0f, 2.0f, -3.0f};
			}
			ImGui::SameLine();
			ImGui::Text("Resets to default tracking universe");

			if(ImGui::Button("Zero")){
				tracking_position_offset = Eigen::Vector3f{0.0f, 0.0f, 0.0f};
			}
			ImGui::SameLine();
			ImGui::Text("Resets to zero'd out tracking universe");
		}
		ImGui::Spacing();
		const pose_type* fast_pose_ptr = _m_fast_pose->get_latest_ro();
		const pose_type* slow_pose_ptr = _m_slow_pose->get_latest_ro();
		const pose_type* true_pose_ptr = _m_true_pose->get_latest_ro();
		ImGui::Text("Switchboard connection status:");
		ImGui::Text("Fast pose topic:");
		ImGui::SameLine();
		if(fast_pose_ptr){
			ImGui::TextColored(ImVec4(0.0, 1.0, 0.0, 1.0), "Valid fast pose pointer");
			ImGui::Text("Fast pose position (XYZ):\n  (%f, %f, %f)", fast_pose_ptr->position.x(), fast_pose_ptr->position.y(), fast_pose_ptr->position.z());
			ImGui::Text("Fast pose quaternion (XYZW):\n  (%f, %f, %f, %f)", fast_pose_ptr->orientation.x(), fast_pose_ptr->orientation.y(), fast_pose_ptr->orientation.z(), fast_pose_ptr->orientation.w());
		} else {
			ImGui::TextColored(ImVec4(1.0, 0.0, 0.0, 1.0), "Invalid fast pose pointer");
		}
		ImGui::Text("Slow pose topic:");
		ImGui::SameLine();
		if(slow_pose_ptr){
			ImGui::TextColored(ImVec4(0.0, 1.0, 0.0, 1.0), "Valid slow pose pointer");
			ImGui::Text("Slow pose position (XYZ):\n  (%f, %f, %f)", slow_pose_ptr->position.x(), slow_pose_ptr->position.y(), slow_pose_ptr->position.z());
			ImGui::Text("Slow pose quaternion (XYZW):\n  (%f, %f, %f, %f)", slow_pose_ptr->orientation.x(), slow_pose_ptr->orientation.y(), slow_pose_ptr->orientation.z(), slow_pose_ptr->orientation.w());
		} else {
			ImGui::TextColored(ImVec4(1.0, 0.0, 0.0, 1.0), "Invalid slow pose pointer");
		}
		ImGui::Text("GROUND TRUTH pose topic:");
		ImGui::SameLine();
		if(true_pose_ptr){
			ImGui::TextColored(ImVec4(0.0, 1.0, 0.0, 1.0), "Valid ground truth pose pointer");
			ImGui::Text("Ground truth position (XYZ):\n  (%f, %f, %f)", true_pose_ptr->position.x(), true_pose_ptr->position.y(), true_pose_ptr->position.z());
			ImGui::Text("Ground truth quaternion (XYZW):\n  (%f, %f, %f, %f)", true_pose_ptr->orientation.x(), true_pose_ptr->orientation.y(), true_pose_ptr->orientation.z(), true_pose_ptr->orientation.w());
		} else {
			ImGui::TextColored(ImVec4(1.0, 0.0, 0.0, 1.0), "Invalid ground truth pose pointer");
		}


		ImGui::End();
		ImGui::ShowDemoWindow();

		ImGui::Render();
	}

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

	void draw_headset(){

		headsetObject.drawMe();
	}

	Eigen::Matrix4f generateHeadsetTransform(const Eigen::Vector3f& position, const Eigen::Quaternionf& rotation, const Eigen::Vector3f& positionOffset){
		Eigen::Matrix4f headsetPosition;
		headsetPosition << 1, 0, 0, position.x() + positionOffset.x(),
						   0, 1, 0, position.y() + positionOffset.y(),
						   0, 0, 1, position.z() + positionOffset.z(),
						   0, 0, 0, 1;

		// We need to convert the headset rotation quaternion to a 4x4 homogenous matrix.
		// First of all, we convert to 3x3 matrix, then extend to 4x4 by augmenting.
		Eigen::Matrix3f rotationMatrix = rotation.toRotationMatrix();
		Eigen::Matrix4f rotationMatrixHomogeneous = Eigen::Matrix4f::Identity();
		rotationMatrixHomogeneous.block(0,0,3,3) = rotationMatrix;
		// Then we apply the headset rotation.
		return headsetPosition * rotationMatrixHomogeneous; 
	}

	

	void main_loop() {
		double lastTime = glfwGetTime();
		glfwMakeContextCurrent(gui_window);
		
		while (!_m_terminate.load()) {

			glfwPollEvents();

			if (glfwGetMouseButton(gui_window, GLFW_MOUSE_BUTTON_LEFT)) 
			{
				
				double xpos, ypos;
				glfwGetCursorPos(gui_window, &xpos, &ypos);
				Eigen::Vector2d new_pos = Eigen::Vector2d{xpos, ypos};
				if(beingDragged == false){
					last_mouse_pos = new_pos;
					beingDragged = true;
				}
				mouse_velocity = new_pos-last_mouse_pos;
				last_mouse_pos = new_pos;
			} else {
				beingDragged = false;
			}

			view_euler.y() += mouse_velocity.x() * 0.002f;
			view_euler.x() += mouse_velocity.y() * 0.002f;

			mouse_velocity = mouse_velocity * 0.95;


			glUseProgram(demoShaderProgram);



			const pose_type* pose_ptr = _m_fast_pose->get_latest_ro();

			Eigen::Matrix4f headsetPose = Eigen::Matrix4f::Identity();
			Eigen::Matrix4f headsetPosition = Eigen::Matrix4f::Identity();

			

			if(pose_ptr){
				// We have a valid pose from our Switchboard plug.

				if(counter == 50){
					std::cerr << "First pose received: quat(wxyz) is " << pose_ptr->orientation.w() << ", " << pose_ptr->orientation.x() << ", " << pose_ptr->orientation.y() << ", " << pose_ptr->orientation.z() << std::endl;
					offsetQuat = Eigen::Quaternionf(pose_ptr->orientation);
				}
				counter++;

				Eigen::Quaternionf combinedQuat = offsetQuat.inverse() * pose_ptr->orientation;
				headsetPose = generateHeadsetTransform(pose_ptr->position, combinedQuat, tracking_position_offset);
			}

			Eigen::Matrix4f modelMatrix = Eigen::Matrix4f::Identity();

			// If we are following the headset, and have a valid pose, apply the optional offset.
			Eigen::Vector3f optionalOffset = (follow_headset && pose_ptr) ? (pose_ptr->position + tracking_position_offset) : Eigen::Vector3f{0.0f,0.0f,0.0f};

			Eigen::Matrix4f userView = lookAt(Eigen::Vector3f{(float)(view_dist * cos(view_euler.y())),
															  (float)(view_dist * sin(view_euler.x())), 
															  (float)(view_dist * sin(view_euler.y()))} + optionalOffset,
												optionalOffset,
												Eigen::Vector3f::UnitY());

			Eigen::Matrix4f modelView = userView * modelMatrix;


			glUseProgram(demoShaderProgram);

			// Size viewport to window size.
			int display_w, display_h;
        	glfwGetFramebufferSize(gui_window, &display_w, &display_h);
			glViewport(0, 0, display_w, display_h);
			float ratio = (float)display_h / (float)display_w;
			// Construct a basic perspective projection
			ksAlgebra::ksMatrix4x4f_CreateProjectionFov( &basicProjection, 40.0f, 40.0f, 40.0f * ratio, 40.0f * ratio, 0.03f, 20.0f );

			glEnable(GL_CULL_FACE);
			glEnable(GL_DEPTH_TEST);
			glClearDepth(1);

			glUniformMatrix4fv(modelViewAttr, 1, GL_FALSE, (GLfloat*)modelView.data());
			glUniformMatrix4fv(projectionAttr, 1, GL_FALSE, (GLfloat*)&(basicProjection.m[0][0]));

			glBindVertexArray(demo_vao);
			
			// Draw things
			glClearColor(0.6f, 0.8f, 0.9f, 1.0f);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

			draw_scene();

			modelView = userView * headsetPose;
			glUniformMatrix4fv(modelViewAttr, 1, GL_FALSE, (GLfloat*)modelView.data());
			headsetObject.color = {0.2,0.2,0.2,1};
			headsetObject.drawMe();

			const pose_type* groundtruth_pose_ptr = _m_true_pose->get_latest_ro();
			if(groundtruth_pose_ptr){
				headsetPose = generateHeadsetTransform(groundtruth_pose_ptr->position, groundtruth_pose_ptr->orientation, tracking_position_offset);
			}
			modelView = userView * headsetPose;
			glUniformMatrix4fv(modelViewAttr, 1, GL_FALSE, (GLfloat*)modelView.data());

			headsetObject.color = {0,0.8,0,1};
			headsetObject.drawMe();

			draw_GUI();
			ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

			glfwSwapBuffers(gui_window);
			
		}
		
	}
private:
	std::thread _m_thread;
	std::atomic<bool> _m_terminate {false};

	GLFWwindow * const glfw_context;
	switchboard* sb;

	std::unique_ptr<reader_latest<pose_type>> _m_fast_pose;
	std::unique_ptr<reader_latest<pose_type>> _m_slow_pose;
	std::unique_ptr<reader_latest<pose_type>> _m_true_pose;
	GLFWwindow* gui_window;

	uint counter = 0;
	Eigen::Quaternionf offsetQuat;

	Eigen::Vector3d view_euler;
	Eigen::Vector2d last_mouse_pos;
	Eigen::Vector2d mouse_velocity;
	bool beingDragged = false;

	float view_dist = 6.0;

	bool follow_headset = false;

	// Currently, the GL demo app applies this offset to the camera view.
	// This is just to make it look nicer with the included SLAM dataset.
	// Therefore, the debug view also applies this pose offset.
	Eigen::Vector3f tracking_position_offset = Eigen::Vector3f{5.0f, 2.0f, -3.0f};

	GLuint demo_vao;
	GLuint demoShaderProgram;

	GLuint vertexPosAttr;
	GLuint vertexNormalAttr;
	GLuint modelViewAttr;
	GLuint projectionAttr;

	GLuint ground_vbo;
	GLuint ground_normal_vbo;
	GLuint water_vbo;
	GLuint water_normal_vbo;
	GLuint trees_vbo;
	GLuint trees_normal_vbo;
	GLuint rocks_vbo;
	GLuint rocks_normal_vbo;

	GLuint headset_vbo;
	GLuint headset_normal_vbo;

	GLuint colorUniform;

	// Scenery
	DebugDrawable groundObject = DebugDrawable({0.1, 0.2, 0.1, 1.0});
	DebugDrawable waterObject =  DebugDrawable({0.0, 0.3, 0.5, 1.0});
	DebugDrawable treesObject =  DebugDrawable({0.0, 0.3, 0.0, 1.0});
	DebugDrawable rocksObject =  DebugDrawable({0.3, 0.3, 0.3, 1.0});

	// Headset debug model
	DebugDrawable headsetObject = DebugDrawable({0.3, 0.3, 0.3, 1.0});

	ksAlgebra::ksMatrix4x4f basicProjection;

	static void GLAPIENTRY
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

public:
	/* compatibility interface */

	// Debug view application overrides _p_start to control its own lifecycle/scheduling.
	virtual void start() override {

		glfwWindowHint(GLFW_VISIBLE, GL_TRUE);
		const char* glsl_version = "#version 430 core";
		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
		glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
		glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

		gui_window = glfwCreateWindow(1600, 1000, "ILLIXR Debug View", NULL, NULL);
		glfwSetWindowSize(gui_window, 1600, 1000);

		if(gui_window == NULL){
			std::cerr << "Debug view couldn't create window." << std::endl;
		}

		glfwMakeContextCurrent(gui_window);
		glfwSwapInterval(1); // Enable vsync!

		glEnable              ( GL_DEBUG_OUTPUT );
		glDebugMessageCallback( MessageCallback, 0 );
		
		// Init and verify GLEW
		if(glewInit()){
			std::cerr << "Failed to init GLEW" << std::endl;
			glfwDestroyWindow(gui_window);
		}

		// Initialize IMGUI context.
		IMGUI_CHECKVERSION();
    	ImGui::CreateContext();
		// Dark theme, of course.
		ImGui::StyleColorsDark();
		// Init IMGUI for OpenGL
		ImGui_ImplGlfw_InitForOpenGL(gui_window, true);
    	ImGui_ImplOpenGL3_Init(glsl_version);

		// Create and bind global VAO object
		glGenVertexArrays(1, &demo_vao);
    	glBindVertexArray(demo_vao);

		demoShaderProgram = init_and_link(blocki_vertex_shader, blocki_fragment_shader);
		std::cout << "Demo app shader program is program " << demoShaderProgram << std::endl;

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

		headsetObject.init(vertexPosAttr,
			vertexNormalAttr,
			colorUniform,
			headset_NUM_TRIANGLES,
			&(headset_vertex_data[0]),
			&(headset_normal_data[0]),
			GL_DYNAMIC_DRAW
		);

		// Construct a basic perspective projection
		ksAlgebra::ksMatrix4x4f_CreateProjectionFov( &basicProjection, 40.0f, 40.0f, 40.0f, 40.0f, 0.03f, 20.0f );

		glfwMakeContextCurrent(NULL);

		_m_thread = std::thread{&debugview::main_loop, this};

	}

	void stop() {
		_m_terminate.store(true);
		_m_thread.join();
	}

	virtual ~debugview() override {
		stop();
		ImGui_ImplOpenGL3_Shutdown();
		ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext();

		glfwDestroyWindow(gui_window);
		glfwTerminate();
	}
};

PLUGIN_MAIN(debugview);


