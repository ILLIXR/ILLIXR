#include <chrono>
#include <future>
#include <iostream>
#include <thread>
#include <functional>

// IMGUI Immediate-mode GUI library
#include "imgui/imgui.h"
#include "imgui/imgui_impl_glfw.h"
#include "imgui/imgui_impl_opengl3.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include "common/threadloop.hpp"
#include "common/switchboard.hpp"
#include "common/data_format.hpp"
#include "common/shader_util.hpp"
#include "common/math_util.hpp"
#include "common/pose_prediction.hpp"
#include "common/gl_util/obj.hpp"
#include "common/global_module_defs.hpp"
#include "shaders/demo_shader.hpp"
#include <opencv2/opencv.hpp>
#include <cmath>

using namespace ILLIXR;

constexpr size_t TEST_PATTERN_WIDTH = 256;
constexpr size_t TEST_PATTERN_HEIGHT = 256;

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

class debugview : public threadloop {
public:

	// Public constructor, Spindle passes the phonebook to this
	// constructor. In turn, the constructor fills in the private
	// references to the switchboard plugs, so the plugin can read
	// the data whenever it needs to.
	debugview(std::string name_, phonebook *pb_)
		: threadloop{name_, pb_}
		, sb{pb->lookup_impl<switchboard>()}
		, pp{pb->lookup_impl<pose_prediction>()}
		, _m_slow_pose{sb->subscribe_latest<pose_type>("slow_pose")}
		, _m_fast_pose{sb->subscribe_latest<imu_raw_type>("imu_raw")}
		//, glfw_context{pb->lookup_impl<global_config>()->glfw_context}
	{}

	void imu_cam_handler(const imu_cam_type *datum) {
		if (datum != nullptr && datum->img0.has_value() && datum->img1.has_value()) {
			last_datum_with_images = datum;
        }
	}

	void draw_GUI() {
        assert(errno == 0 && "Errno should not be set at start of draw_GUI");

		// Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        RAC_ERRNO_MSG("debugview after ImGui_ImplOpenGL3_NewFrame");

		// Calls glfw within source code which sets errno
        ImGui_ImplGlfw_NewFrame();
		if (glfwGetError(NULL) != GLFW_NO_ERROR) {
			std::cerr << "Error in glfwPollEvents " << __FILE__ << ":" << __LINE__ << std::endl;
		}
		RAC_ERRNO_MSG("debugview after ImGui_ImplGlfw_NewFrame");

        ImGui::NewFrame();

		// Init the window docked to the bottom left corner.
		ImGui::SetNextWindowPos(ImVec2(0.0f, ImGui::GetIO().DisplaySize.y), ImGuiCond_Once, ImVec2(0.0f, 1.0f));
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

			if(ImGui::Button("Zero orientation")){
				const pose_type predicted_pose = pp->get_fast_pose().pose;
				if (pp->fast_pose_reliable()) {
					// Can only zero if predicted_pose is valid
					pp->set_offset(predicted_pose.orientation);
				}
			}
			ImGui::SameLine();
			ImGui::Text("Resets to zero'd out tracking universe");
		}
		ImGui::Spacing();
		ImGui::Text("Switchboard connection status:");
		ImGui::Text("Predicted pose topic:");
		ImGui::SameLine();

		if (pp->fast_pose_reliable()) {
			const pose_type predicted_pose = pp->get_fast_pose().pose;
			ImGui::TextColored(ImVec4(0.0, 1.0, 0.0, 1.0), "Valid predicted pose pointer");
			ImGui::Text("Prediced pose position (XYZ):\n  (%f, %f, %f)", predicted_pose.position.x(), predicted_pose.position.y(), predicted_pose.position.z());
			ImGui::Text("Predicted pose quaternion (XYZW):\n  (%f, %f, %f, %f)", predicted_pose.orientation.x(), predicted_pose.orientation.y(), predicted_pose.orientation.z(), predicted_pose.orientation.w());
		} else {
			ImGui::TextColored(ImVec4(1.0, 0.0, 0.0, 1.0), "Invalid predicted pose pointer");
		}

		ImGui::Text("Fast pose topic:");
		ImGui::SameLine();

		const imu_raw_type *raw_imu = _m_fast_pose->get_latest_ro();
		if (raw_imu) {
			pose_type raw_pose;
			raw_pose.position = Eigen::Vector3f{float(raw_imu->pos(0)), float(raw_imu->pos(1)), float(raw_imu->pos(2))};
            raw_pose.orientation = Eigen::Quaternionf{float(raw_imu->quat.w()), float(raw_imu->quat.x()), float(raw_imu->quat.y()), float(raw_imu->quat.z())};
			pose_type swapped_pose = pp->correct_pose(raw_pose);

			ImGui::TextColored(ImVec4(0.0, 1.0, 0.0, 1.0), "Valid fast pose pointer");
			ImGui::Text("Fast pose position (XYZ):\n  (%f, %f, %f)", swapped_pose.position.x(), swapped_pose.position.y(), swapped_pose.position.z());
			ImGui::Text("Fast pose quaternion (XYZW):\n  (%f, %f, %f, %f)", swapped_pose.orientation.x(), swapped_pose.orientation.y(), swapped_pose.orientation.z(), swapped_pose.orientation.w());
		} else {
			ImGui::TextColored(ImVec4(1.0, 0.0, 0.0, 1.0), "Invalid fast pose pointer");
		}

		ImGui::Text("Slow pose topic:");
		ImGui::SameLine();

		const pose_type *slow_pose_ptr = _m_slow_pose->get_latest_ro();
		if (slow_pose_ptr) {
			pose_type swapped_pose = pp->correct_pose(*slow_pose_ptr);

			ImGui::TextColored(ImVec4(0.0, 1.0, 0.0, 1.0), "Valid slow pose pointer");
			ImGui::Text("Slow pose position (XYZ):\n  (%f, %f, %f)", swapped_pose.position.x(), swapped_pose.position.y(), swapped_pose.position.z());
			ImGui::Text("Slow pose quaternion (XYZW):\n  (%f, %f, %f, %f)", swapped_pose.orientation.x(), swapped_pose.orientation.y(), swapped_pose.orientation.z(), swapped_pose.orientation.w());
		} else {
			ImGui::TextColored(ImVec4(1.0, 0.0, 0.0, 1.0), "Invalid slow pose pointer");
		}

		ImGui::Text("Ground truth pose topic:");
		ImGui::SameLine();

		if (pp->true_pose_reliable()) {
			const pose_type true_pose = pp->get_true_pose();
			ImGui::TextColored(ImVec4(0.0, 1.0, 0.0, 1.0), "Valid ground truth pose pointer");
			ImGui::Text("Ground truth position (XYZ):\n  (%f, %f, %f)", true_pose.position.x(), true_pose.position.y(), true_pose.position.z());
			ImGui::Text("Ground truth quaternion (XYZW):\n  (%f, %f, %f, %f)", true_pose.orientation.x(), true_pose.orientation.y(), true_pose.orientation.z(), true_pose.orientation.w());
		} else {
			ImGui::TextColored(ImVec4(1.0, 0.0, 0.0, 1.0), "Invalid ground truth pose pointer");
		}

		ImGui::Text("Debug view eulers:");
		ImGui::Text("	(%f, %f)", view_euler.x(), view_euler.y());

		ImGui::End();

		ImGui::Begin("Camera + IMU");
		ImGui::Text("Camera view buffers: ");
		ImGui::Text("	Camera0: (%d, %d) \n		GL texture handle: %d", camera_texture_sizes[0].x(), camera_texture_sizes[0].y(), camera_textures[0]);
		ImGui::Text("	Camera1: (%d, %d) \n		GL texture handle: %d", camera_texture_sizes[1].x(), camera_texture_sizes[1].y(), camera_textures[1]);
		ImGui::End();

		ImGui::SetNextWindowSize(ImVec2(700,350), ImGuiCond_Once);
		ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y), ImGuiCond_Once, ImVec2(1.0f, 1.0f));
		ImGui::Begin("Onboard camera views");
		auto windowSize = ImGui::GetWindowSize();
		auto verticalOffset = ImGui::GetCursorPos().y;
		ImGui::Image((void*)(intptr_t)camera_textures[0], ImVec2(windowSize.x/2,windowSize.y - verticalOffset * 2));
		ImGui::SameLine();
		ImGui::Image((void*)(intptr_t)camera_textures[1], ImVec2(windowSize.x/2,windowSize.y - verticalOffset * 2));
		ImGui::End();

		ImGui::Render();

		RAC_ERRNO_MSG("debugview after ImGui render");
	}

	bool load_camera_images() {
        assert(errno == 0 && "Errno should not be set at start of load_camera_images");

		if (last_datum_with_images == nullptr) {
			return false;
		}

		if (last_datum_with_images->img0.has_value()) {
			glBindTexture(GL_TEXTURE_2D, camera_textures[0]);
            RAC_ERRNO();

			cv::Mat img0 = *last_datum_with_images->img0.value();
			glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, img0.cols, img0.rows, 0, GL_RED, GL_UNSIGNED_BYTE, img0.ptr());
			RAC_ERRNO();

			camera_texture_sizes[0] = Eigen::Vector2i(img0.cols, img0.rows);

			GLint swizzleMask[] = {GL_RED, GL_RED, GL_RED, GL_RED};
			RAC_ERRNO();
			glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swizzleMask);
			RAC_ERRNO();
		} else {
			std::cerr << "img0 has no value!" << std::endl;

			glBindTexture(GL_TEXTURE_2D, camera_textures[0]);
			RAC_ERRNO();

			glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, TEST_PATTERN_WIDTH, TEST_PATTERN_HEIGHT, 0, GL_RED, GL_UNSIGNED_BYTE, &(test_pattern[0][0]));
			RAC_ERRNO();

			glFlush();
			RAC_ERRNO();

			camera_texture_sizes[0] = Eigen::Vector2i(TEST_PATTERN_WIDTH, TEST_PATTERN_HEIGHT);
		}
		
		if (last_datum_with_images->img1.has_value()) {
			glBindTexture(GL_TEXTURE_2D, camera_textures[1]);
			RAC_ERRNO();

			cv::Mat img1 = *last_datum_with_images->img1.value();

			glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, img1.cols, img1.rows, 0, GL_RED, GL_UNSIGNED_BYTE, img1.ptr());
			RAC_ERRNO();

			camera_texture_sizes[1] = Eigen::Vector2i(img1.cols, img1.rows);

			GLint swizzleMask[] = {GL_RED, GL_RED, GL_RED, GL_RED};
			RAC_ERRNO();

			glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swizzleMask);
			RAC_ERRNO();
		} else {
			glBindTexture(GL_TEXTURE_2D, camera_textures[1]);
			RAC_ERRNO();

			glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, TEST_PATTERN_WIDTH, TEST_PATTERN_HEIGHT, 0, GL_RED, GL_UNSIGNED_BYTE, &(test_pattern[0][0]));
			RAC_ERRNO();

			glFlush();
			RAC_ERRNO();

			camera_texture_sizes[1] = Eigen::Vector2i(TEST_PATTERN_WIDTH, TEST_PATTERN_HEIGHT);
		}

		RAC_ERRNO_MSG("debugview at end of load_camera_images");
		return true;
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
		rotationMatrixHomogeneous.block(0, 0, 3, 3) = rotationMatrix;
		// Then we apply the headset rotation.
		return headsetPosition * rotationMatrixHomogeneous; 
	}

	void _p_thread_setup() override {
        assert(errno == 0 && "Errno should not be set at start of _p_thread_setup");

		// Note: glfwMakeContextCurrent must be called from the thread which will be using it.
		glfwMakeContextCurrent(gui_window);
        RAC_ERRNO();

		// https://www.glfw.org/docs/latest/intro_guide.html#error_handling
		if (glfwGetError(NULL) != GLFW_NO_ERROR) {
			std::cerr << "Error in creating glfw context " << __FILE__ << ":" << __LINE__ << std::endl;
		}
		RAC_ERRNO();
	}

	void _p_one_iteration() override {
        assert(errno == 0 && "Errno should not be set at stat of _p_one_iteration");

        glfwPollEvents();
        RAC_ERRNO_MSG("debugview after glfwPollEvents");

        if (glfwGetError(NULL) != GLFW_NO_ERROR) {
            std::cerr << "Error in glfwPollEvents " << __FILE__ << ":" << __LINE__ << std::endl;
        }
        RAC_ERRNO();

        if (glfwGetMouseButton(gui_window, GLFW_MOUSE_BUTTON_LEFT)) {
            RAC_ERRNO();

            double xpos, ypos;

            glfwGetCursorPos(gui_window, &xpos, &ypos);
            RAC_ERRNO();


            Eigen::Vector2d new_pos = Eigen::Vector2d{xpos, ypos};
            if (beingDragged == false) {
                last_mouse_pos = new_pos;
                beingDragged = true;
            }
            mouse_velocity = new_pos-last_mouse_pos;
            last_mouse_pos = new_pos;
        } else {
            beingDragged = false;
        }
        RAC_ERRNO();

        view_euler.y() += mouse_velocity.x() * 0.002f;
        view_euler.x() += mouse_velocity.y() * 0.002f;

        mouse_velocity = mouse_velocity * 0.95;

        load_camera_images();

        glUseProgram(demoShaderProgram);
        RAC_ERRNO();

        Eigen::Matrix4f headsetPose = Eigen::Matrix4f::Identity();

        const fast_pose_type predicted_pose = pp->get_fast_pose();
        if (pp->fast_pose_reliable()) {
            const pose_type pose = predicted_pose.pose;
            Eigen::Quaternionf combinedQuat = pose.orientation;
            headsetPose = generateHeadsetTransform(pose.position, combinedQuat, tracking_position_offset);
        }

        Eigen::Matrix4f modelMatrix = Eigen::Matrix4f::Identity();

        // If we are following the headset, and have a valid pose, apply the optional offset.
        Eigen::Vector3f optionalOffset = (follow_headset && pp->fast_pose_reliable())
            ? (predicted_pose.pose.position + tracking_position_offset)
            : Eigen::Vector3f{0.0f,0.0f,0.0f}
        ;

        Eigen::Matrix4f userView = lookAt(Eigen::Vector3f{(float)(view_dist * cos(view_euler.y())),
                                                          (float)(view_dist * sin(view_euler.x())), 
                                                          (float)(view_dist * sin(view_euler.y()))} + optionalOffset,
                                            optionalOffset,
                                            Eigen::Vector3f::UnitY());

        Eigen::Matrix4f modelView = userView * modelMatrix;

        RAC_ERRNO_MSG("debugview before glUseProgram");
        glUseProgram(demoShaderProgram);
        RAC_ERRNO_MSG("debugview after glUseProgram");

        // Size viewport to window size.
        int display_w, display_h;

        glfwGetFramebufferSize(gui_window, &display_w, &display_h);
        RAC_ERRNO_MSG("debugview after glfwGetFramebufferSize");

        glViewport(0, 0, display_w, display_h);
        RAC_ERRNO_MSG("debugview after glViewport");

        float ratio = (float)display_h / (float)display_w;

        // Construct a basic perspective projection
        math_util::projection_fov( &basicProjection, 40.0f, 40.0f, 40.0f * ratio, 40.0f * ratio, 0.03f, 20.0f );
        RAC_ERRNO_MSG("debugview after projection_fov");

        glEnable(GL_CULL_FACE);
        RAC_ERRNO_MSG("debugview after enabling GL_CULL_FACE");

        glEnable(GL_DEPTH_TEST);
        RAC_ERRNO_MSG("debugview after enabling GL_DEPTH_TEST");

        glClearDepth(1);
        RAC_ERRNO_MSG("debugview after glClearDepth");

        glUniformMatrix4fv(modelViewAttr, 1, GL_FALSE, (GLfloat*)modelView.data());
        RAC_ERRNO();

        glUniformMatrix4fv(projectionAttr, 1, GL_FALSE, (GLfloat*)(basicProjection.data()));
        RAC_ERRNO();

        glBindVertexArray(demo_vao);
        RAC_ERRNO_MSG("debugview after glBindVertexArray");

        // Draw things
        glClearColor(0.8f, 0.8f, 0.8f, 1.0f);
        RAC_ERRNO_MSG("debugview after glClearColor");

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        RAC_ERRNO_MSG("debugview after glClear");

        demoscene.Draw();
        RAC_ERRNO_MSG("debugview after demoscene draw");

        modelView = userView * headsetPose;

        glUniformMatrix4fv(modelViewAttr, 1, GL_FALSE, (GLfloat*)modelView.data());
        RAC_ERRNO();

        headset.Draw();
        RAC_ERRNO_MSG("debugview after headset draw");

        draw_GUI();
        RAC_ERRNO_MSG("debugview after draw_GUI");

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        RAC_ERRNO_MSG("debugview after imgui GetDrawData");

        glfwSwapBuffers(gui_window);
        RAC_ERRNO_MSG("debugview after glfwSwapBuffers");
	}
private:

	//GLFWwindow * const glfw_context;
	const std::shared_ptr<switchboard> sb;
	const std::shared_ptr<pose_prediction> pp;

	std::unique_ptr<reader_latest<pose_type>> _m_slow_pose;
	std::unique_ptr<reader_latest<imu_raw_type>> _m_fast_pose;
	// std::unique_ptr<reader_latest<imu_cam_type>> _m_imu_cam_data;
	GLFWwindow* gui_window;

	uint8_t test_pattern[TEST_PATTERN_WIDTH][TEST_PATTERN_HEIGHT];

	Eigen::Vector3d view_euler = Eigen::Vector3d::Zero();
	Eigen::Vector2d last_mouse_pos = Eigen::Vector2d::Zero();
	Eigen::Vector2d mouse_velocity = Eigen::Vector2d::Zero();
	bool beingDragged = false;

	float view_dist = 2.0;

	bool follow_headset = true;

	double lastTime;

	Eigen::Vector3f tracking_position_offset = Eigen::Vector3f{0.0f, 0.0f, 0.0f};


	const imu_cam_type* last_datum_with_images = NULL;
	// std::vector<std::optional<cv::Mat>> camera_data = {std::nullopt, std::nullopt};
	GLuint camera_textures[2];
	Eigen::Vector2i camera_texture_sizes[2] = {Eigen::Vector2i::Zero(), Eigen::Vector2i::Zero()};

	GLuint demo_vao;
	GLuint demoShaderProgram;

	GLuint vertexPosAttr;
	GLuint vertexNormalAttr;
	GLuint modelViewAttr;
	GLuint projectionAttr;

	GLuint colorUniform;

	ObjScene demoscene;
	ObjScene headset;

	Eigen::Matrix4f basicProjection;

public:
	/* compatibility interface */

	// Debug view application overrides _p_start to control its own lifecycle/scheduling.
	virtual void start() override {
        assert(errno == 0 && "Errno should not be set at the top of start()");

		// The "imu_cam" topic is not really a topic, in the current implementation.
		// It serves more as an event stream. Camera frames are only available on this topic
		// the very split second they are made available. Subsequently published packets to this
		// topic do not contain the camera frames.
   		sb->schedule<imu_cam_type>(id, "imu_cam", [&](const imu_cam_type *datum) {
        	this->imu_cam_handler(datum);
    	});

		glfwWindowHint(GLFW_VISIBLE, GL_TRUE);
		RAC_ERRNO();

        /// TODO: This does not match the glsl version flag in `common/common.mk` (330 core)
		const char* glsl_version = "#version 430 core";

		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
		RAC_ERRNO();

		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
		RAC_ERRNO();

		glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
		RAC_ERRNO();

		glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
		RAC_ERRNO();

		gui_window = glfwCreateWindow(1600, 1000, "ILLIXR Debug View", NULL, NULL);
		RAC_ERRNO();

		if (gui_window == nullptr) {
			std::cerr << "Debug view couldn't create window " << __FILE__ << ":" << __LINE__ << std::endl;
			exit(1);
		}
		RAC_ERRNO_MSG("debugview after glfwCreateWindow");

		glfwSetWindowSize(gui_window, 1600, 1000);
		RAC_ERRNO();

		if (glfwGetError(NULL) != GLFW_NO_ERROR) {
			std::cerr << "Error in creating glfw context " << __FILE__ << ":" << __LINE__ << std::endl;
		}
		RAC_ERRNO_MSG("debugview after glfwSetWindowSize");

		glfwMakeContextCurrent(gui_window);
        RAC_ERRNO();

		// https://www.glfw.org/docs/latest/intro_guide.html#error_handling
		if (glfwGetError(NULL) != GLFW_NO_ERROR) {
			std::cerr << "Error in creating glfw context " << __FILE__ << ":" << __LINE__ << std::endl;
		}
		RAC_ERRNO_MSG("debugview after glfwMakeContextCurrent");

		glfwSwapInterval(1); // Enable vsync!
		RAC_ERRNO_MSG("debugview after vysnc recover block");

		glEnable(GL_DEBUG_OUTPUT);
		RAC_ERRNO_MSG("debugview after enabling GL");

		glDebugMessageCallback(MessageCallback, 0);
		RAC_ERRNO_MSG("debugview after setting the debug callback");

		// Init and verify GLEW
		if (glewInit()) {
		    RAC_ERRNO();

			std::cerr << "Failed to init GLEW" << std::endl;

			glfwDestroyWindow(gui_window);
			RAC_ERRNO();
		}
		RAC_ERRNO_MSG("debugview after glewInit");

		// Initialize IMGUI context.
		IMGUI_CHECKVERSION();
    	ImGui::CreateContext();
		// Dark theme, of course.
		ImGui::StyleColorsDark();

		// Init IMGUI for OpenGL
		assert(errno == 0);
		ImGui_ImplGlfw_InitForOpenGL(gui_window, true);
		RAC_ERRNO_MSG("debugview after ImGui_ImplGlfw_InitForOpenGL");

    	ImGui_ImplOpenGL3_Init(glsl_version);

		// Create and bind global VAO object
		glGenVertexArrays(1, &demo_vao);
		RAC_ERRNO();

    	glBindVertexArray(demo_vao);
		RAC_ERRNO();

		demoShaderProgram = init_and_link(demo_vertex_shader, demo_fragment_shader);
		#ifndef NDEBUG
			std::cout << "Demo app shader program is program " << demoShaderProgram << std::endl;
		#endif

		vertexPosAttr = glGetAttribLocation(demoShaderProgram, "vertexPosition");
		RAC_ERRNO();

		vertexNormalAttr = glGetAttribLocation(demoShaderProgram, "vertexNormal");
		RAC_ERRNO();

		modelViewAttr = glGetUniformLocation(demoShaderProgram, "u_modelview");
		RAC_ERRNO();

		projectionAttr = glGetUniformLocation(demoShaderProgram, "u_projection");
		RAC_ERRNO();

		colorUniform = glGetUniformLocation(demoShaderProgram, "u_color");

		// Load/initialize the demo scene.
		char* obj_dir = std::getenv("ILLIXR_DEMO_DATA");
		if (obj_dir == NULL) {
			std::cerr << "Please define ILLIXR_DEMO_DATA." << std::endl;
			abort();
		}

		demoscene = ObjScene(std::string(obj_dir), "scene.obj");
		headset = ObjScene(std::string(obj_dir), "headset.obj");

		// Generate fun test pattern for missing camera images.
		for (unsigned x = 0; x < TEST_PATTERN_WIDTH; x++) {
			for (unsigned y = 0; y < TEST_PATTERN_HEIGHT; y++) {
				test_pattern[x][y] = ((x+y) % 6 == 0) ? 255 : 0;
			}
		}

		glGenTextures(2, &(camera_textures[0]));
        RAC_ERRNO();

		glBindTexture(GL_TEXTURE_2D, camera_textures[0]);
        RAC_ERRNO();

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        RAC_ERRNO();

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        RAC_ERRNO();

		glBindTexture(GL_TEXTURE_2D, camera_textures[1]);
        RAC_ERRNO();

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        RAC_ERRNO();

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        RAC_ERRNO();

		// Construct a basic perspective projection
		math_util::projection_fov( &basicProjection, 40.0f, 40.0f, 40.0f, 40.0f, 0.03f, 20.0f );

		glfwMakeContextCurrent(NULL);
		RAC_ERRNO();

		// https://www.glfw.org/docs/latest/intro_guide.html#error_handling
		if (glfwGetError(NULL) != GLFW_NO_ERROR) {
			std::cerr << "Error in creating glfw context " << __FILE__ << ":" << __LINE__ << std::endl;
		}
		RAC_ERRNO_MSG("debugview after glfwMakeContextCurrent");

		lastTime = glfwGetTime();
		RAC_ERRNO();

		threadloop::start();
		RAC_ERRNO_MSG("debuview at bottom of start()");
	}

	virtual ~debugview() override {
		assert(errno == 0 && "Errno should not be set at start of destructor");

		ImGui_ImplOpenGL3_Shutdown();

		ImGui_ImplGlfw_Shutdown();

		if (glfwGetError(NULL) != GLFW_NO_ERROR) {
			std::cerr << "Error in ImGui_ImplGlfw_Shutdown " << __FILE__ << ":" << __LINE__ << std::endl;
		}
		RAC_ERRNO();

		ImGui::DestroyContext();
		RAC_ERRNO();

		glfwDestroyWindow(gui_window);
		RAC_ERRNO();

		if (glfwGetError(NULL) != GLFW_NO_ERROR) {
			std::cerr << "Error in glfwDestroyWindow " << __FILE__ << ":" << __LINE__ << std::endl;
		}
		RAC_ERRNO();

		assert(errno == 0);
		glfwTerminate();
		if (glfwGetError(NULL) != GLFW_NO_ERROR) {
			std::cerr << "Error in glfwTerminate" << __FILE__ << ":" << __LINE__ << std::endl;
		}
		RAC_ERRNO();
	}
};

PLUGIN_MAIN(debugview);
