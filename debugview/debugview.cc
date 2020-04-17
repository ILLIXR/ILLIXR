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
#include "common/component.hh"
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

class debugview : public component {
public:

	// Public constructor, create_component passes Switchboard handles ("plugs")
	// to this constructor. In turn, the constructor fills in the private
	// references to the switchboard plugs, so the component can read the
	// data whenever it needs to.
	debugview(std::unique_ptr<reader_latest<pose_type>>&& fast_pose_plug,
		  std::unique_ptr<reader_latest<pose_type>>&& slow_pose_plug,
		  std::unique_ptr<reader_latest<global_config>>&& config_plug)
		: _m_fast_pose{std::move(fast_pose_plug)}
		, _m_slow_pose{std::move(slow_pose_plug)}
		, _m_config{std::move(config_plug)}
	{ }

	void draw_GUI() {
		// Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

		// Create a window called "My First Tool", with a menu bar.
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

		ImGui::End();
		ImGui::ShowDemoWindow();

		ImGui::Render();
	}

	void draw_scene() {

		// OBJ exporter is having winding order issues currently.
		// Please excuse the strange GL_CW and GL_CCW mode switches.
		
		glFrontFace(GL_CW);

		glBindBuffer(GL_ARRAY_BUFFER, ground_vbo);
		glVertexAttribPointer(vertexPosAttr, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
		glEnableVertexAttribArray(vertexPosAttr);
		glBindBuffer(GL_ARRAY_BUFFER, ground_normal_vbo);
		glVertexAttribPointer(vertexNormalAttr, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
		glEnableVertexAttribArray(vertexNormalAttr);
		glUniform4fv(colorUniform, 1, &(ground_color[0]));
		glDrawArrays(GL_TRIANGLES, 0, Ground_plane_NUM_TRIANGLES * 3);

		glFrontFace(GL_CCW);

		glBindBuffer(GL_ARRAY_BUFFER, water_vbo);
		glVertexAttribPointer(vertexPosAttr, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
		glEnableVertexAttribArray(vertexPosAttr);
		glBindBuffer(GL_ARRAY_BUFFER, water_normal_vbo);
		glVertexAttribPointer(vertexNormalAttr, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
		glEnableVertexAttribArray(vertexNormalAttr);
		glUniform4fv(colorUniform, 1, &(water_color[0]));
		glDrawArrays(GL_TRIANGLES, 0, Water_plane001_NUM_TRIANGLES * 3);

		glBindBuffer(GL_ARRAY_BUFFER, trees_vbo);
		glVertexAttribPointer(vertexPosAttr, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
		glEnableVertexAttribArray(vertexPosAttr);
		glBindBuffer(GL_ARRAY_BUFFER, trees_normal_vbo);
		glVertexAttribPointer(vertexNormalAttr, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
		glEnableVertexAttribArray(vertexNormalAttr);
		glUniform4fv(colorUniform, 1, &(tree_color[0]));
		glDrawArrays(GL_TRIANGLES, 0, Trees_cone_NUM_TRIANGLES * 3);

		glBindBuffer(GL_ARRAY_BUFFER, rocks_vbo);
		glVertexAttribPointer(vertexPosAttr, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
		glEnableVertexAttribArray(vertexPosAttr);
		glBindBuffer(GL_ARRAY_BUFFER, rocks_normal_vbo);
		glVertexAttribPointer(vertexNormalAttr, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
		glEnableVertexAttribArray(vertexNormalAttr);
		glUniform4fv(colorUniform, 1, &(rock_color[0]));
		glDrawArrays(GL_TRIANGLES, 0, Rocks_plane002_NUM_TRIANGLES * 3);

		glFrontFace(GL_CCW);
	}

	void draw_headset(){
		glBindBuffer(GL_ARRAY_BUFFER, headset_vbo);
		glVertexAttribPointer(vertexPosAttr, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
		glEnableVertexAttribArray(vertexPosAttr);
		glBindBuffer(GL_ARRAY_BUFFER, headset_normal_vbo);
		glVertexAttribPointer(vertexNormalAttr, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
		glEnableVertexAttribArray(vertexNormalAttr);
		glUniform4fv(colorUniform, 1, &(rock_color[0]));
		glDrawArrays(GL_TRIANGLES, 0, headset_NUM_TRIANGLES * 3);
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

				
				headsetPosition << 1, 0, 0, pose_ptr->position.x() + tracking_position_offset.x(),
				               	   0, 1, 0, pose_ptr->position.y() + tracking_position_offset.y(),
							       0, 0, 1, pose_ptr->position.z() + tracking_position_offset.z(),
							       0, 0, 0, 1;

				// We need to convert the headset rotation quaternion to a 4x4 homogenous matrix.
				// First of all, we convert to 3x3 matrix, then extend to 4x4 by augmenting.
				Eigen::Matrix3f combinedRot = combinedQuat.toRotationMatrix();
				Eigen::Matrix4f combinedRotHomogeneous = Eigen::Matrix4f::Identity();
				combinedRotHomogeneous.block(0,0,3,3) = combinedRot;

				// Then we apply the headset rotation.
				headsetPose = headsetPosition * combinedRotHomogeneous;
				//headsetPose = headsetPosition;
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

			draw_headset();

			draw_GUI();
			ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

			glfwSwapBuffers(gui_window);
			
		}
		
	}
private:
	std::thread _m_thread;
	std::atomic<bool> _m_terminate {false};

	// Switchboard plug for pose prediction.
	std::unique_ptr<reader_latest<pose_type>> _m_fast_pose;

	// Switchboard plug for slow pose.
	std::unique_ptr<reader_latest<pose_type>> _m_slow_pose;

	// Switchboard plug for global config data, including GLFW/GPU context handles.
	std::unique_ptr<reader_latest<global_config>> _m_config;

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

	GLfloat water_color[4] = {
		0.0, 0.3, 0.5, 1.0
	};

	GLfloat ground_color[4] = {
		0.1, 0.2, 0.1, 1.0
	};

	GLfloat tree_color[4] = {
		0.0, 0.3, 0.0, 1.0
	};

	GLfloat rock_color[4] = {
		0.3, 0.3, 0.3, 1.0
	};

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
	virtual void _p_start() override {

		glfwWindowHint(GLFW_VISIBLE, GL_TRUE);
		const char* glsl_version = "#version 430 core";
		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
		glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
		glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

		auto fetched_config = _m_config->get_latest_ro();
		if(!fetched_config){
			std::cerr << "Debug view failed to fetch global config." << std::endl;
		}
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

		// Config mesh position vbo
		glGenBuffers(1, &ground_vbo);
		glBindBuffer(GL_ARRAY_BUFFER, ground_vbo);
		glBufferData(GL_ARRAY_BUFFER, (Ground_plane_NUM_TRIANGLES * 3 * 3) * sizeof(GLfloat), &(Ground_Plane_vertex_data[0]), GL_STATIC_DRAW);
		glGenBuffers(1, &water_vbo);
		glBindBuffer(GL_ARRAY_BUFFER, water_vbo);
		glBufferData(GL_ARRAY_BUFFER, (Water_plane001_NUM_TRIANGLES * 3 * 3) * sizeof(GLfloat), &(Water_Plane001_vertex_data[0]), GL_STATIC_DRAW);
		glGenBuffers(1, &trees_vbo);
		glBindBuffer(GL_ARRAY_BUFFER, trees_vbo);
		glBufferData(GL_ARRAY_BUFFER, (Trees_cone_NUM_TRIANGLES * 3 * 3) * sizeof(GLfloat), &(Trees_Cone_vertex_data[0]), GL_STATIC_DRAW);
		glGenBuffers(1, &rocks_vbo);
		glBindBuffer(GL_ARRAY_BUFFER, rocks_vbo);
		glBufferData(GL_ARRAY_BUFFER, (Rocks_plane002_NUM_TRIANGLES * 3 * 3) * sizeof(GLfloat), &(Rocks_Plane002_vertex_data[0]), GL_STATIC_DRAW);

		glGenBuffers(1, &ground_normal_vbo);
		glBindBuffer(GL_ARRAY_BUFFER, ground_normal_vbo);
		glBufferData(GL_ARRAY_BUFFER, (Ground_plane_NUM_TRIANGLES * 3 * 3) * sizeof(GLfloat), &(Ground_Plane_normal_data[0]), GL_STATIC_DRAW);
		glGenBuffers(1, &water_normal_vbo);
		glBindBuffer(GL_ARRAY_BUFFER, water_normal_vbo);
		glBufferData(GL_ARRAY_BUFFER, (Water_plane001_NUM_TRIANGLES * 3 * 3) * sizeof(GLfloat), &(Water_Plane001_normal_data[0]), GL_STATIC_DRAW);
		glGenBuffers(1, &trees_normal_vbo);
		glBindBuffer(GL_ARRAY_BUFFER, trees_normal_vbo);
		glBufferData(GL_ARRAY_BUFFER, (Trees_cone_NUM_TRIANGLES * 3 * 3) * sizeof(GLfloat), &(Trees_Cone_normal_data[0]), GL_STATIC_DRAW);
		glGenBuffers(1, &rocks_normal_vbo);
		glBindBuffer(GL_ARRAY_BUFFER, rocks_normal_vbo);
		glBufferData(GL_ARRAY_BUFFER, (Rocks_plane002_NUM_TRIANGLES * 3 * 3) * sizeof(GLfloat), &(Rocks_Plane002_normal_data[0]), GL_STATIC_DRAW);

		glGenBuffers(1, &headset_vbo);
		glBindBuffer(GL_ARRAY_BUFFER, headset_vbo);
		glBufferData(GL_ARRAY_BUFFER, (headset_NUM_TRIANGLES * 3 * 3) * sizeof(GLfloat), &(headset_vertex_data[0]), GL_STATIC_DRAW);
		glGenBuffers(1, &headset_normal_vbo);
		glBindBuffer(GL_ARRAY_BUFFER, headset_normal_vbo);
		glBufferData(GL_ARRAY_BUFFER, (headset_NUM_TRIANGLES * 3 * 3) * sizeof(GLfloat), &(headset_normal_data[0]), GL_STATIC_DRAW);
		
		glVertexAttribPointer(vertexPosAttr, 3, GL_FLOAT, GL_FALSE, 0, 0);
		glVertexAttribPointer(vertexNormalAttr, 3, GL_FLOAT, GL_FALSE, 0, 0);

		// Construct a basic perspective projection
		ksAlgebra::ksMatrix4x4f_CreateProjectionFov( &basicProjection, 40.0f, 40.0f, 40.0f, 40.0f, 0.03f, 20.0f );

		glfwMakeContextCurrent(NULL);

		_m_thread = std::thread{&debugview::main_loop, this};

	}

	virtual void _p_stop() override {
		_m_terminate.store(true);
		_m_thread.join();
	}

	virtual ~debugview() override {
		ImGui_ImplOpenGL3_Shutdown();
		ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext();

		glfwDestroyWindow(gui_window);
		glfwTerminate();
	}
};

extern "C" component* create_component(switchboard* sb) {
	/* First, we declare intent to read/write topics. Switchboard
	   returns handles to those topics. */

	// We sample the up-to-date, predicted pose.
	auto fast_pose_ev = sb->subscribe_latest<pose_type>("fast_pose");

	// We sample the slow pose.
	auto slow_pose_ev = sb->subscribe_latest<pose_type>("slow_pose");

	auto config_ev = sb->subscribe_latest<global_config>("global_config");

	return new debugview {std::move(fast_pose_ev), std::move(slow_pose_ev),std::move(config_ev)};
}

