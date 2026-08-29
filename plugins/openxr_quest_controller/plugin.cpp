#include "plugin.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <stdexcept>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace {

constexpr float         kTriggerPressedThreshold    = 0.75F;
constexpr float         kSqueezePressedThreshold    = 0.85F;
constexpr float         kThumbstickMoveThreshold    = 0.15F;
constexpr int           kWindowSize                 = 64;
constexpr float         kPanelDistanceMeters        = 1.1F;
constexpr float         kPanelWidthMeters           = 1.2F;
constexpr float         kPanelYOffsetMeters         = 0.0F;
constexpr float         kNearZ                      = 0.02F;
constexpr float         kFarZ                       = 100.0F;
constexpr std::uint32_t kOverlayCommandStrideFloats = 14;

constexpr const char* kSimpleControllerProfile = "/interaction_profiles/khr/simple_controller";
constexpr const char* kOculusTouchProfile      = "/interaction_profiles/oculus/touch_controller";
constexpr const char* kHtcViveProfile          = "/interaction_profiles/htc/vive_controller";
constexpr const char* kValveIndexProfile       = "/interaction_profiles/valve/index_controller";
constexpr const char* kMicrosoftMotionProfile  = "/interaction_profiles/microsoft/motion_controller";

template<typename T>
T make_xr_struct(XrStructureType type) {
    T value{};
    value.type = type;
    return value;
}

bool has_extension(const std::vector<XrExtensionProperties>& extensions, const char* name) {
    return std::any_of(extensions.begin(), extensions.end(), [name](const XrExtensionProperties& extension) {
        return std::strcmp(extension.extensionName, name) == 0;
    });
}

struct mat4 {
    float values[16]{};
};

mat4 identity_matrix() {
    mat4 result{};
    result.values[0]  = 1.0F;
    result.values[5]  = 1.0F;
    result.values[10] = 1.0F;
    result.values[15] = 1.0F;
    return result;
}

mat4 multiply(const mat4& left, const mat4& right) {
    mat4 result{};
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            for (int index = 0; index < 4; ++index) {
                result.values[column * 4 + row] += left.values[index * 4 + row] * right.values[column * 4 + index];
            }
        }
    }
    return result;
}

mat4 translation_matrix(float x, float y, float z) {
    mat4 result       = identity_matrix();
    result.values[12] = x;
    result.values[13] = y;
    result.values[14] = z;
    return result;
}

mat4 scale_matrix(float x, float y, float z) {
    mat4 result{};
    result.values[0]  = x;
    result.values[5]  = y;
    result.values[10] = z;
    result.values[15] = 1.0F;
    return result;
}

mat4 pose_matrix(const XrPosef& pose) {
    const float x  = pose.orientation.x;
    const float y  = pose.orientation.y;
    const float z  = pose.orientation.z;
    const float w  = pose.orientation.w;
    const float xx = x * x;
    const float yy = y * y;
    const float zz = z * z;
    const float xy = x * y;
    const float xz = x * z;
    const float yz = y * z;
    const float wx = w * x;
    const float wy = w * y;
    const float wz = w * z;

    mat4 result       = identity_matrix();
    result.values[0]  = 1.0F - 2.0F * (yy + zz);
    result.values[1]  = 2.0F * (xy + wz);
    result.values[2]  = 2.0F * (xz - wy);
    result.values[4]  = 2.0F * (xy - wz);
    result.values[5]  = 1.0F - 2.0F * (xx + zz);
    result.values[6]  = 2.0F * (yz + wx);
    result.values[8]  = 2.0F * (xz + wy);
    result.values[9]  = 2.0F * (yz - wx);
    result.values[10] = 1.0F - 2.0F * (xx + yy);
    result.values[12] = pose.position.x;
    result.values[13] = pose.position.y;
    result.values[14] = pose.position.z;
    return result;
}

mat4 inverse_rigid_transform(const mat4& matrix) {
    mat4 result       = identity_matrix();
    result.values[0]  = matrix.values[0];
    result.values[1]  = matrix.values[4];
    result.values[2]  = matrix.values[8];
    result.values[4]  = matrix.values[1];
    result.values[5]  = matrix.values[5];
    result.values[6]  = matrix.values[9];
    result.values[8]  = matrix.values[2];
    result.values[9]  = matrix.values[6];
    result.values[10] = matrix.values[10];

    const float x     = matrix.values[12];
    const float y     = matrix.values[13];
    const float z     = matrix.values[14];
    result.values[12] = -(result.values[0] * x + result.values[4] * y + result.values[8] * z);
    result.values[13] = -(result.values[1] * x + result.values[5] * y + result.values[9] * z);
    result.values[14] = -(result.values[2] * x + result.values[6] * y + result.values[10] * z);
    return result;
}

mat4 projection_matrix(const XrFovf& fov, float near_z, float far_z) {
    const float tangent_left   = std::tan(fov.angleLeft);
    const float tangent_right  = std::tan(fov.angleRight);
    const float tangent_down   = std::tan(fov.angleDown);
    const float tangent_up     = std::tan(fov.angleUp);
    const float tangent_width  = tangent_right - tangent_left;
    const float tangent_height = tangent_up - tangent_down;

    mat4 result{};
    result.values[0]  = 2.0F / tangent_width;
    result.values[5]  = 2.0F / tangent_height;
    result.values[8]  = (tangent_right + tangent_left) / tangent_width;
    result.values[9]  = (tangent_up + tangent_down) / tangent_height;
    result.values[10] = -(far_z + near_z) / (far_z - near_z);
    result.values[11] = -1.0F;
    result.values[14] = -(2.0F * far_z * near_z) / (far_z - near_z);
    return result;
}

GLuint compile_shader(GLenum type, const char* source) {
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint status = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (status == GL_TRUE) {
        return shader;
    }
    GLint log_length = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_length);
    std::vector<GLchar> log(static_cast<std::size_t>(std::max(1, log_length)), '\0');
    glGetShaderInfoLog(shader, log_length, nullptr, log.data());
    std::cerr << "OpenXR Quest shader compilation failed: " << log.data() << '\n';
    glDeleteShader(shader);
    return 0;
}

GLuint link_program(const char* vertex_source, const char* fragment_source) {
    const GLuint vertex_shader   = compile_shader(GL_VERTEX_SHADER, vertex_source);
    const GLuint fragment_shader = compile_shader(GL_FRAGMENT_SHADER, fragment_source);
    if (vertex_shader == 0 || fragment_shader == 0) {
        if (vertex_shader != 0) {
            glDeleteShader(vertex_shader);
        }
        if (fragment_shader != 0) {
            glDeleteShader(fragment_shader);
        }
        return 0;
    }
    const GLuint program = glCreateProgram();
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glLinkProgram(program);
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);
    GLint status = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (status == GL_TRUE) {
        return program;
    }
    GLint log_length = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &log_length);
    std::vector<GLchar> log(static_cast<std::size_t>(std::max(1, log_length)), '\0');
    glGetProgramInfoLog(program, log_length, nullptr, log.data());
    std::cerr << "OpenXR Quest shader program link failed: " << log.data() << '\n';
    glDeleteProgram(program);
    return 0;
}

GLuint create_panel_program() {
    constexpr const char* vertex_source   = R"GLSL(
        #version 330 core
        out vec2 uv;
        uniform mat4 uMvp;
        const vec3 positions[6] = vec3[6](
            vec3(-0.5, -0.5, 0.0), vec3( 0.5, -0.5, 0.0),
            vec3( 0.5,  0.5, 0.0), vec3(-0.5, -0.5, 0.0),
            vec3( 0.5,  0.5, 0.0), vec3(-0.5,  0.5, 0.0)
        );
        const vec2 texcoords[6] = vec2[6](
            vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0),
            vec2(0.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0)
        );
        void main() {
            gl_Position = uMvp * vec4(positions[gl_VertexID], 1.0);
            uv = texcoords[gl_VertexID];
        }
    )GLSL";
    constexpr const char* fragment_source = R"GLSL(
        #version 330 core
        in vec2 uv;
        out vec4 frag;
        uniform sampler2D uSource;
        uniform bool uFlipY;
        void main() {
            frag = texture(uSource, vec2(uv.x, uFlipY ? 1.0 - uv.y : uv.y));
        }
    )GLSL";
    return link_program(vertex_source, fragment_source);
}

GLuint create_overlay_program() {
    constexpr const char* vertex_source   = R"GLSL(
        #version 330 core
        layout(location = 0) in vec2 aPos;
        layout(location = 1) in vec4 aColor;
        uniform vec2 uSourceSize;
        out vec4 vColor;
        void main() {
            vec2 ndc = vec2(
                (aPos.x / max(uSourceSize.x, 1.0)) * 2.0 - 1.0,
                1.0 - (aPos.y / max(uSourceSize.y, 1.0)) * 2.0
            );
            gl_Position = vec4(ndc, 0.0, 1.0);
            vColor = aColor;
        }
    )GLSL";
    constexpr const char* fragment_source = R"GLSL(
        #version 330 core
        in vec4 vColor;
        out vec4 frag;
        void main() { frag = vColor; }
    )GLSL";
    return link_program(vertex_source, fragment_source);
}

GLuint create_modal_program() {
    constexpr const char* vertex_source   = R"GLSL(
        #version 330 core
        layout(location = 0) in vec2 aPos;
        layout(location = 1) in vec2 aUv;
        uniform vec2 uSourceSize;
        out vec2 vUv;
        void main() {
            vec2 ndc = vec2(
                (aPos.x / max(uSourceSize.x, 1.0)) * 2.0 - 1.0,
                1.0 - (aPos.y / max(uSourceSize.y, 1.0)) * 2.0
            );
            gl_Position = vec4(ndc, 0.0, 1.0);
            vUv = aUv;
        }
    )GLSL";
    constexpr const char* fragment_source = R"GLSL(
        #version 330 core
        in vec2 vUv;
        out vec4 frag;
        uniform sampler2D uModal;
        void main() { frag = texture(uModal, vUv); }
    )GLSL";
    return link_program(vertex_source, fragment_source);
}

void append_overlay_vertex(std::vector<float>* vertices, float x, float y, float red, float green, float blue, float alpha) {
    vertices->insert(vertices->end(),
                     {x, y, std::clamp(red / 255.0F, 0.0F, 1.0F), std::clamp(green / 255.0F, 0.0F, 1.0F),
                      std::clamp(blue / 255.0F, 0.0F, 1.0F), std::clamp(alpha, 0.0F, 1.0F)});
}

void append_overlay_triangle(std::vector<float>* vertices, float x0, float y0, float x1, float y1, float x2, float y2,
                             float red, float green, float blue, float alpha) {
    append_overlay_vertex(vertices, x0, y0, red, green, blue, alpha);
    append_overlay_vertex(vertices, x1, y1, red, green, blue, alpha);
    append_overlay_vertex(vertices, x2, y2, red, green, blue, alpha);
}

void append_overlay_command(std::vector<float>* vertices, const float* command) {
    const int   command_type = static_cast<int>(std::round(command[0]));
    const float radius       = std::max(command_type == 0 ? 0.5F : 1.0F, command[5]);
    const float alpha        = command[6];
    const float red          = command[7];
    const float green        = command[8];
    const float blue         = command[9];
    if (command_type == 1) {
        const float x0 = command[1] - radius;
        const float y0 = command[2] - radius;
        const float x1 = command[1] + radius;
        const float y1 = command[2] + radius;
        append_overlay_triangle(vertices, x0, y0, x1, y0, x1, y1, red, green, blue, alpha);
        append_overlay_triangle(vertices, x0, y0, x1, y1, x0, y1, red, green, blue, alpha);
        return;
    }
    if (command_type != 0) {
        return;
    }
    const float delta_x = command[3] - command[1];
    const float delta_y = command[4] - command[2];
    const float length  = std::sqrt(delta_x * delta_x + delta_y * delta_y);
    if (length <= 1.0e-4F) {
        return;
    }
    const float normal_x = -delta_y / length * radius;
    const float normal_y = delta_x / length * radius;
    append_overlay_triangle(vertices, command[1] + normal_x, command[2] + normal_y, command[3] + normal_x,
                            command[4] + normal_y, command[3] - normal_x, command[4] - normal_y, red, green, blue, alpha);
    append_overlay_triangle(vertices, command[1] + normal_x, command[2] + normal_y, command[3] - normal_x,
                            command[4] - normal_y, command[1] - normal_x, command[2] - normal_y, red, green, blue, alpha);
}

void draw_overlay_commands(const std::vector<float>& commands, GLuint program, GLuint vao, GLuint vbo,
                           GLint source_size_location, std::uint32_t source_width, std::uint32_t source_height) {
    if (commands.empty() || program == 0 || vao == 0 || vbo == 0) {
        return;
    }
    std::vector<float> vertices;
    const std::size_t  command_count = commands.size() / kOverlayCommandStrideFloats;
    vertices.reserve(command_count * 36);
    for (std::size_t command_index = 0; command_index < command_count; ++command_index) {
        append_overlay_command(&vertices, commands.data() + command_index * kOverlayCommandStrideFloats);
    }
    if (vertices.empty()) {
        return;
    }
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(program);
    glUniform2f(source_size_location, static_cast<float>(source_width), static_cast<float>(source_height));
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(float)), vertices.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size() / 6));
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    glUseProgram(0);
    glDisable(GL_BLEND);
}

void draw_modal_overlay(const ILLIXR::data_format::stereo_modal_overlay& modal, GLuint texture, GLuint program, GLuint vao,
                        GLuint vbo, GLint source_size_location, GLint texture_location, std::size_t eye_index,
                        std::uint32_t source_width, std::uint32_t source_height) {
    const bool eye_valid = eye_index == 0 ? modal.left_valid : modal.right_valid;
    if (!modal.visible || !eye_valid || modal.width == 0 || modal.height == 0 || texture == 0 || program == 0 || vao == 0 ||
        vbo == 0) {
        return;
    }
    const auto& quad       = eye_index == 0 ? modal.left_quad_pixels : modal.right_quad_pixels;
    const float vertices[] = {
        quad[0].x(), quad[0].y(), 0.0F, 0.0F, quad[1].x(), quad[1].y(), 1.0F, 0.0F, quad[2].x(), quad[2].y(), 1.0F, 1.0F,
        quad[0].x(), quad[0].y(), 0.0F, 0.0F, quad[2].x(), quad[2].y(), 1.0F, 1.0F, quad[3].x(), quad[3].y(), 0.0F, 1.0F,
    };
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(program);
    glUniform2f(source_size_location, static_cast<float>(source_width), static_cast<float>(source_height));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glUniform1i(texture_location, 0);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
    glDisable(GL_BLEND);
}

bool byte_range_valid(std::size_t mapping_size, std::uint64_t offset, std::uint64_t byte_count) {
    return offset <= mapping_size && byte_count <= mapping_size - static_cast<std::size_t>(offset);
}

bool read_generation(const std::uint8_t* data, std::size_t size, std::uint64_t offset, std::uint64_t* generation) {
    if (!byte_range_valid(size, offset, sizeof(*generation))) {
        return false;
    }
    std::memcpy(generation, data + offset, sizeof(*generation));
    return true;
}

} // namespace

namespace ILLIXR {

openxr_quest_controller::mapped_file::~mapped_file() {
    reset();
}

void openxr_quest_controller::mapped_file::reset() {
    if (data != nullptr) {
        munmap(const_cast<std::uint8_t*>(data), size);
        data = nullptr;
        size = 0;
    }
    if (fd >= 0) {
        close(fd);
        fd = -1;
    }
    path.clear();
}

openxr_quest_controller::openxr_quest_controller(const std::string& name, phonebook* pb)
    : plugin{name, pb}
    , switchboard_{pb->lookup_impl<switchboard>()}
    , clock_{pb->lookup_impl<relative_clock>()}
    , stoplight_{pb->lookup_impl<stoplight>()}
    , controller_writer_{switchboard_->get_writer<controller_input>("quest_controller")}
    , view_writer_{switchboard_->get_writer<view_frame>("openxr_view")}
    , stereo_reader_{switchboard_->get_reader<stereo_frame>("stereo_frame")} {
    spdlogger(switchboard_->get_env_char("OPENXR_QUEST_CONTROLLER_LOG_LEVEL", "info"));
    log_input_ = switchboard_->get_env_bool("OPENXR_QUEST_CONTROLLER_LOG_INPUT", "true");
}

openxr_quest_controller::~openxr_quest_controller() {
    stop_requested_.store(true);
    if (worker_.joinable()) {
        worker_.join();
    }
    destroy_graphics();
}

void openxr_quest_controller::start() {
    initialize_graphics();
    plugin::start();

    stop_requested_.store(false);
    worker_ = std::thread([this]() {
        run();
    });
}

void openxr_quest_controller::stop() {
    stop_requested_.store(true);
    if (worker_.joinable()) {
        worker_.join();
    }
    destroy_graphics();
    plugin::stop();
}

void openxr_quest_controller::initialize_graphics() {
    std::string display_mode = switchboard_->get_env("ILLIXR_DISPLAY_MODE", "glfw");
    std::transform(display_mode.begin(), display_mode.end(), display_mode.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    if (display_mode != "none") {
        throw std::runtime_error{
            "openxr_quest_controller requires ILLIXR_DISPLAY_MODE=none because it owns the MVP OpenXR graphics session"};
    }

    if (XInitThreads() == 0) {
        plugin_logger_->warn("XInitThreads reported that Xlib thread support could not be initialized");
    }

    if (glfwInit() != GLFW_TRUE) {
        const char* description = nullptr;
        glfwGetError(&description);
        throw std::runtime_error{std::string{"glfwInit failed"} + (description ? ": " + std::string{description} : "")};
    }
    glfw_initialized_ = true;

    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    window_ = glfwCreateWindow(kWindowSize, kWindowSize, "ILLIXR Quest Controller", nullptr, nullptr);
    if (window_ == nullptr) {
        const char* description = nullptr;
        glfwGetError(&description);
        destroy_graphics();
        throw std::runtime_error{std::string{"glfwCreateWindow failed"} + (description ? ": " + std::string{description} : "")};
    }

    glfwMakeContextCurrent(window_);
    glfwSwapInterval(0);
    if (!resolve_glx_binding()) {
        glfwMakeContextCurrent(nullptr);
        destroy_graphics();
        throw std::runtime_error{"Failed to resolve the hidden OpenGL window's GLX handles"};
    }

    const GLubyte* gl_version = glGetString(GL_VERSION);
    plugin_logger_->info("Created hidden OpenGL context: {}",
                         gl_version ? reinterpret_cast<const char*>(gl_version) : "unknown version");
    glfwMakeContextCurrent(nullptr);
}

void openxr_quest_controller::destroy_graphics() {
    if (window_ != nullptr) {
        glfwDestroyWindow(window_);
        window_ = nullptr;
    }
    if (glfw_initialized_) {
        glfwTerminate();
        glfw_initialized_ = false;
    }

    x_display_     = nullptr;
    glx_fb_config_ = nullptr;
    glx_drawable_  = 0;
    glx_context_   = nullptr;
    visual_id_     = 0;
}

bool openxr_quest_controller::resolve_glx_binding() {
    x_display_    = glfwGetX11Display();
    glx_drawable_ = glfwGetGLXWindow(window_);
    glx_context_  = glfwGetGLXContext(window_);
    if (x_display_ == nullptr || glx_drawable_ == 0 || glx_context_ == nullptr) {
        plugin_logger_->error("Failed to get GLFW's native X11/GLX handles");
        return false;
    }

    unsigned int fb_config_id = 0;
    glXQueryDrawable(x_display_, glx_drawable_, GLX_FBCONFIG_ID, &fb_config_id);
    if (fb_config_id == 0) {
        plugin_logger_->error("GLX_FBCONFIG_ID query returned zero");
        return false;
    }

    const int    attribs[]    = {GLX_FBCONFIG_ID, static_cast<int>(fb_config_id), None};
    int          config_count = 0;
    GLXFBConfig* configs      = glXChooseFBConfig(x_display_, DefaultScreen(x_display_), attribs, &config_count);
    if (configs == nullptr || config_count < 1) {
        plugin_logger_->error("glXChooseFBConfig failed for GLX_FBCONFIG_ID={}", fb_config_id);
        return false;
    }

    glx_fb_config_           = configs[0];
    XVisualInfo* visual_info = glXGetVisualFromFBConfig(x_display_, glx_fb_config_);
    if (visual_info == nullptr) {
        XFree(configs);
        plugin_logger_->error("glXGetVisualFromFBConfig failed");
        return false;
    }

    visual_id_ = static_cast<std::uint32_t>(visual_info->visualid);
    XFree(visual_info);
    XFree(configs);
    return true;
}

void openxr_quest_controller::run() {
    stoplight_->wait_for_ready();
    glfwMakeContextCurrent(window_);

    try {
        if (initialize_openxr()) {
            while (!stop_requested_.load() && !stoplight_->check_should_stop() && !exit_requested_) {
                if (!pump_events()) {
                    break;
                }
                if (!session_running_) {
                    std::this_thread::sleep_for(std::chrono::milliseconds{10});
                    continue;
                }
                if (!process_frame()) {
                    break;
                }
            }
        }
    } catch (const std::exception& error) {
        plugin_logger_->error("OpenXR controller thread failed: {}", error.what());
    }

    destroy_openxr();
    glfwMakeContextCurrent(nullptr);
}

bool openxr_quest_controller::initialize_openxr() {
    const char* runtime_json = std::getenv("XR_RUNTIME_JSON");
    plugin_logger_->info("Starting Quest controller input through the OpenXR loader (XR_RUNTIME_JSON={})",
                         runtime_json ? runtime_json : "loader default");

    std::uint32_t extension_count = 0;
    if (!check_xr(xrEnumerateInstanceExtensionProperties(nullptr, 0, &extension_count, nullptr),
                  "xrEnumerateInstanceExtensionProperties(count)")) {
        return false;
    }

    std::vector<XrExtensionProperties> extensions(extension_count);
    for (XrExtensionProperties& extension : extensions) {
        extension = make_xr_struct<XrExtensionProperties>(XR_TYPE_EXTENSION_PROPERTIES);
    }
    if (!check_xr(xrEnumerateInstanceExtensionProperties(nullptr, extension_count, &extension_count, extensions.data()),
                  "xrEnumerateInstanceExtensionProperties(list)")) {
        return false;
    }
    if (!has_extension(extensions, XR_KHR_OPENGL_ENABLE_EXTENSION_NAME)) {
        plugin_logger_->error("The active OpenXR runtime does not expose {}", XR_KHR_OPENGL_ENABLE_EXTENSION_NAME);
        return false;
    }

    const char*          enabled_extensions[] = {XR_KHR_OPENGL_ENABLE_EXTENSION_NAME};
    XrInstanceCreateInfo instance_info        = make_xr_struct<XrInstanceCreateInfo>(XR_TYPE_INSTANCE_CREATE_INFO);
    std::strncpy(instance_info.applicationInfo.applicationName, "ILLIXR Quest Controller", XR_MAX_APPLICATION_NAME_SIZE - 1);
    std::strncpy(instance_info.applicationInfo.engineName, "ILLIXR", XR_MAX_ENGINE_NAME_SIZE - 1);
    instance_info.applicationInfo.applicationVersion = 1;
    instance_info.applicationInfo.engineVersion      = 1;
    instance_info.applicationInfo.apiVersion         = XR_CURRENT_API_VERSION;
    instance_info.enabledExtensionCount              = 1;
    instance_info.enabledExtensionNames              = enabled_extensions;
    if (!check_xr(xrCreateInstance(&instance_info, &instance_), "xrCreateInstance")) {
        return false;
    }

    XrInstanceProperties instance_properties = make_xr_struct<XrInstanceProperties>(XR_TYPE_INSTANCE_PROPERTIES);
    if (check_xr(xrGetInstanceProperties(instance_, &instance_properties), "xrGetInstanceProperties")) {
        plugin_logger_->info("OpenXR runtime: {} {}.{}.{}", instance_properties.runtimeName,
                             XR_VERSION_MAJOR(instance_properties.runtimeVersion),
                             XR_VERSION_MINOR(instance_properties.runtimeVersion),
                             XR_VERSION_PATCH(instance_properties.runtimeVersion));
    }

    XrSystemGetInfo system_info = make_xr_struct<XrSystemGetInfo>(XR_TYPE_SYSTEM_GET_INFO);
    system_info.formFactor      = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    if (!check_xr(xrGetSystem(instance_, &system_info, &system_id_), "xrGetSystem(HMD)")) {
        plugin_logger_->error("SteamVR did not expose a running HMD; connect ALVR and the Quest before launching ILLIXR");
        return false;
    }

    XrSystemProperties system_properties = make_xr_struct<XrSystemProperties>(XR_TYPE_SYSTEM_PROPERTIES);
    if (check_xr(xrGetSystemProperties(instance_, system_id_, &system_properties), "xrGetSystemProperties")) {
        plugin_logger_->info("OpenXR HMD: {}", system_properties.systemName);
    }

    if (!enumerate_view_configuration()) {
        return false;
    }

    PFN_xrGetOpenGLGraphicsRequirementsKHR get_graphics_requirements = nullptr;
    if (!check_xr(xrGetInstanceProcAddr(instance_, "xrGetOpenGLGraphicsRequirementsKHR",
                                        reinterpret_cast<PFN_xrVoidFunction*>(&get_graphics_requirements)),
                  "xrGetInstanceProcAddr(xrGetOpenGLGraphicsRequirementsKHR)")) {
        return false;
    }
    XrGraphicsRequirementsOpenGLKHR graphics_requirements =
        make_xr_struct<XrGraphicsRequirementsOpenGLKHR>(XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR);
    if (!check_xr(get_graphics_requirements(instance_, system_id_, &graphics_requirements),
                  "xrGetOpenGLGraphicsRequirementsKHR")) {
        return false;
    }

    XrGraphicsBindingOpenGLXlibKHR graphics_binding =
        make_xr_struct<XrGraphicsBindingOpenGLXlibKHR>(XR_TYPE_GRAPHICS_BINDING_OPENGL_XLIB_KHR);
    graphics_binding.xDisplay    = x_display_;
    graphics_binding.visualid    = visual_id_;
    graphics_binding.glxFBConfig = glx_fb_config_;
    graphics_binding.glxDrawable = glx_drawable_;
    graphics_binding.glxContext  = glx_context_;

    XrSessionCreateInfo session_info = make_xr_struct<XrSessionCreateInfo>(XR_TYPE_SESSION_CREATE_INFO);
    session_info.next                = &graphics_binding;
    session_info.systemId            = system_id_;
    if (!check_xr(xrCreateSession(instance_, &session_info, &session_), "xrCreateSession")) {
        return false;
    }

    std::uint32_t blend_mode_count = 0;
    if (check_xr(
            xrEnumerateEnvironmentBlendModes(instance_, system_id_, view_configuration_type_, 0, &blend_mode_count, nullptr),
            "xrEnumerateEnvironmentBlendModes(count)") &&
        blend_mode_count > 0) {
        std::vector<XrEnvironmentBlendMode> blend_modes(blend_mode_count);
        if (check_xr(xrEnumerateEnvironmentBlendModes(instance_, system_id_, view_configuration_type_, blend_mode_count,
                                                      &blend_mode_count, blend_modes.data()),
                     "xrEnumerateEnvironmentBlendModes(list)")) {
            blend_mode_       = blend_modes.front();
            const auto opaque = std::find(blend_modes.begin(), blend_modes.end(), XR_ENVIRONMENT_BLEND_MODE_OPAQUE);
            if (opaque != blend_modes.end()) {
                blend_mode_ = *opaque;
            }
        }
    }

    if (!create_actions()) {
        return false;
    }

    XrReferenceSpaceCreateInfo local_space_info =
        make_xr_struct<XrReferenceSpaceCreateInfo>(XR_TYPE_REFERENCE_SPACE_CREATE_INFO);
    local_space_info.referenceSpaceType                 = XR_REFERENCE_SPACE_TYPE_LOCAL;
    local_space_info.poseInReferenceSpace.orientation.w = 1.0F;
    if (!check_xr(xrCreateReferenceSpace(session_, &local_space_info, &local_space_), "xrCreateReferenceSpace(LOCAL)")) {
        return false;
    }

    if (!create_swapchains() || !initialize_stereo_renderer()) {
        return false;
    }

    plugin_logger_->info(
        "OpenXR Quest input/display session initialized; waiting for SteamVR session focus and stereo_frame data");
    return true;
}

bool openxr_quest_controller::create_actions() {
    XrActionSetCreateInfo action_set_info = make_xr_struct<XrActionSetCreateInfo>(XR_TYPE_ACTION_SET_CREATE_INFO);
    std::strncpy(action_set_info.actionSetName, "quest_controller", XR_MAX_ACTION_SET_NAME_SIZE - 1);
    std::strncpy(action_set_info.localizedActionSetName, "Quest Controller Input", XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE - 1);
    if (!check_xr(xrCreateActionSet(instance_, &action_set_info, &action_set_), "xrCreateActionSet")) {
        return false;
    }

    if (!check_xr(xrStringToPath(instance_, "/user/hand/left", &hand_paths_[0]), "xrStringToPath(left hand)") ||
        !check_xr(xrStringToPath(instance_, "/user/hand/right", &hand_paths_[1]), "xrStringToPath(right hand)")) {
        return false;
    }

    if (!create_action(XR_ACTION_TYPE_POSE_INPUT, "grip_pose", "Grip Pose", &grip_pose_action_) ||
        !create_action(XR_ACTION_TYPE_POSE_INPUT, "aim_pose", "Aim Pose", &aim_pose_action_) ||
        !create_action(XR_ACTION_TYPE_BOOLEAN_INPUT, "trigger_click", "Trigger Click", &trigger_click_action_) ||
        !create_action(XR_ACTION_TYPE_FLOAT_INPUT, "trigger_value", "Trigger Value", &trigger_value_action_) ||
        !create_action(XR_ACTION_TYPE_BOOLEAN_INPUT, "primary_click", "Primary Button", &primary_click_action_) ||
        !create_action(XR_ACTION_TYPE_BOOLEAN_INPUT, "secondary_click", "Secondary Button", &secondary_click_action_) ||
        !create_action(XR_ACTION_TYPE_BOOLEAN_INPUT, "thumbstick_click", "Thumbstick Click", &thumbstick_click_action_) ||
        !create_action(XR_ACTION_TYPE_VECTOR2F_INPUT, "thumbstick_axis", "Thumbstick Axis", &thumbstick_axis_action_) ||
        !create_action(XR_ACTION_TYPE_FLOAT_INPUT, "squeeze_value", "Squeeze Value", &squeeze_value_action_)) {
        return false;
    }

    if (!suggest_bindings()) {
        return false;
    }

    XrSessionActionSetsAttachInfo attach_info =
        make_xr_struct<XrSessionActionSetsAttachInfo>(XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO);
    attach_info.countActionSets = 1;
    attach_info.actionSets      = &action_set_;
    if (!check_xr(xrAttachSessionActionSets(session_, &attach_info), "xrAttachSessionActionSets")) {
        return false;
    }

    for (std::size_t hand_index = 0; hand_index < hand_paths_.size(); ++hand_index) {
        const char* grip_label = hand_index == 0 ? "xrCreateActionSpace(grip left)" : "xrCreateActionSpace(grip right)";
        const char* aim_label  = hand_index == 0 ? "xrCreateActionSpace(aim left)" : "xrCreateActionSpace(aim right)";
        if (!create_action_space(grip_pose_action_, hand_paths_[hand_index], &grip_spaces_[hand_index], grip_label) ||
            !create_action_space(aim_pose_action_, hand_paths_[hand_index], &aim_spaces_[hand_index], aim_label)) {
            return false;
        }
    }
    return true;
}

bool openxr_quest_controller::create_action(XrActionType type, const char* name, const char* localized_name, XrAction* action) {
    XrActionCreateInfo action_info = make_xr_struct<XrActionCreateInfo>(XR_TYPE_ACTION_CREATE_INFO);
    std::strncpy(action_info.actionName, name, XR_MAX_ACTION_NAME_SIZE - 1);
    std::strncpy(action_info.localizedActionName, localized_name, XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
    action_info.actionType          = type;
    action_info.countSubactionPaths = static_cast<std::uint32_t>(hand_paths_.size());
    action_info.subactionPaths      = hand_paths_.data();
    return check_xr(xrCreateAction(action_set_, &action_info, action), "xrCreateAction");
}

bool openxr_quest_controller::suggest_bindings() {
    const char* profiles[] = {kSimpleControllerProfile, kOculusTouchProfile, kHtcViveProfile, kValveIndexProfile,
                              kMicrosoftMotionProfile};

    for (const char* profile : profiles) {
        std::vector<XrActionSuggestedBinding> bindings;
        if (!add_binding(&bindings, grip_pose_action_, "/user/hand/left/input/grip/pose") ||
            !add_binding(&bindings, grip_pose_action_, "/user/hand/right/input/grip/pose") ||
            !add_binding(&bindings, aim_pose_action_, "/user/hand/left/input/aim/pose") ||
            !add_binding(&bindings, aim_pose_action_, "/user/hand/right/input/aim/pose")) {
            return false;
        }

        if (std::strcmp(profile, kSimpleControllerProfile) == 0) {
            if (!add_binding(&bindings, trigger_click_action_, "/user/hand/left/input/select/click") ||
                !add_binding(&bindings, trigger_click_action_, "/user/hand/right/input/select/click")) {
                return false;
            }
        } else {
            if (!add_binding(&bindings, trigger_click_action_, "/user/hand/left/input/trigger/value") ||
                !add_binding(&bindings, trigger_click_action_, "/user/hand/right/input/trigger/value") ||
                !add_binding(&bindings, trigger_value_action_, "/user/hand/left/input/trigger/value") ||
                !add_binding(&bindings, trigger_value_action_, "/user/hand/right/input/trigger/value") ||
                !add_binding(&bindings, squeeze_value_action_, "/user/hand/left/input/squeeze/value") ||
                !add_binding(&bindings, squeeze_value_action_, "/user/hand/right/input/squeeze/value")) {
                return false;
            }
        }

        if (std::strcmp(profile, kOculusTouchProfile) == 0) {
            if (!add_binding(&bindings, primary_click_action_, "/user/hand/left/input/x/click") ||
                !add_binding(&bindings, primary_click_action_, "/user/hand/right/input/a/click") ||
                !add_binding(&bindings, secondary_click_action_, "/user/hand/left/input/y/click") ||
                !add_binding(&bindings, secondary_click_action_, "/user/hand/right/input/b/click") ||
                !add_binding(&bindings, thumbstick_click_action_, "/user/hand/left/input/thumbstick/click") ||
                !add_binding(&bindings, thumbstick_click_action_, "/user/hand/right/input/thumbstick/click") ||
                !add_binding(&bindings, thumbstick_axis_action_, "/user/hand/left/input/thumbstick") ||
                !add_binding(&bindings, thumbstick_axis_action_, "/user/hand/right/input/thumbstick")) {
                return false;
            }
        }

        if (!suggest_profile_bindings(profile, bindings)) {
            return false;
        }
    }
    return true;
}

bool openxr_quest_controller::add_binding(std::vector<XrActionSuggestedBinding>* bindings, XrAction action,
                                          const char* path_string_value) {
    XrPath path = XR_NULL_PATH;
    if (!check_xr(xrStringToPath(instance_, path_string_value, &path), path_string_value)) {
        return false;
    }
    bindings->push_back({action, path});
    return true;
}

bool openxr_quest_controller::suggest_profile_bindings(const char*                                  profile_string,
                                                       const std::vector<XrActionSuggestedBinding>& bindings) {
    XrPath profile_path = XR_NULL_PATH;
    if (!check_xr(xrStringToPath(instance_, profile_string, &profile_path), profile_string)) {
        return false;
    }

    XrInteractionProfileSuggestedBinding suggested =
        make_xr_struct<XrInteractionProfileSuggestedBinding>(XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING);
    suggested.interactionProfile     = profile_path;
    suggested.countSuggestedBindings = static_cast<std::uint32_t>(bindings.size());
    suggested.suggestedBindings      = bindings.data();

    const XrResult result = xrSuggestInteractionProfileBindings(instance_, &suggested);
    if (result == XR_ERROR_PATH_UNSUPPORTED || result == XR_ERROR_PATH_INVALID) {
        plugin_logger_->debug("OpenXR runtime does not support interaction profile {}", profile_string);
        return true;
    }
    return check_xr(result, "xrSuggestInteractionProfileBindings");
}

bool openxr_quest_controller::create_action_space(XrAction action, XrPath hand_path, XrSpace* space, const char* label) {
    XrActionSpaceCreateInfo space_info         = make_xr_struct<XrActionSpaceCreateInfo>(XR_TYPE_ACTION_SPACE_CREATE_INFO);
    space_info.action                          = action;
    space_info.subactionPath                   = hand_path;
    space_info.poseInActionSpace.orientation.w = 1.0F;
    return check_xr(xrCreateActionSpace(session_, &space_info, space), label);
}

bool openxr_quest_controller::pump_events() {
    XrEventDataBuffer event = make_xr_struct<XrEventDataBuffer>(XR_TYPE_EVENT_DATA_BUFFER);
    for (;;) {
        const XrResult result = xrPollEvent(instance_, &event);
        if (result == XR_EVENT_UNAVAILABLE) {
            return true;
        }
        if (!check_xr(result, "xrPollEvent")) {
            return false;
        }

        switch (event.type) {
        case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
            const auto* changed = reinterpret_cast<const XrEventDataSessionStateChanged*>(&event);
            session_state_      = changed->state;
            plugin_logger_->info("OpenXR session state -> {}", session_state_label(session_state_));

            if (session_state_ == XR_SESSION_STATE_READY && !session_running_) {
                XrSessionBeginInfo begin_info           = make_xr_struct<XrSessionBeginInfo>(XR_TYPE_SESSION_BEGIN_INFO);
                begin_info.primaryViewConfigurationType = view_configuration_type_;
                if (!check_xr(xrBeginSession(session_, &begin_info), "xrBeginSession")) {
                    return false;
                }
                session_running_            = true;
                interaction_profiles_dirty_ = true;
            } else if (session_state_ == XR_SESSION_STATE_STOPPING && session_running_) {
                if (!check_xr(xrEndSession(session_), "xrEndSession")) {
                    return false;
                }
                session_running_ = false;
            } else if (session_state_ == XR_SESSION_STATE_EXITING || session_state_ == XR_SESSION_STATE_LOSS_PENDING) {
                exit_requested_ = true;
            }
            break;
        }
        case XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED:
            interaction_profiles_dirty_ = true;
            break;
        case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
            plugin_logger_->warn("OpenXR runtime reported instance loss pending");
            exit_requested_ = true;
            break;
        case XR_TYPE_EVENT_DATA_EVENTS_LOST: {
            const auto* lost = reinterpret_cast<const XrEventDataEventsLost*>(&event);
            plugin_logger_->warn("OpenXR runtime reported {} lost events", lost->lostEventCount);
            break;
        }
        default:
            break;
        }

        event = make_xr_struct<XrEventDataBuffer>(XR_TYPE_EVENT_DATA_BUFFER);
    }
}

bool openxr_quest_controller::process_frame() {
    XrFrameWaitInfo wait_info   = make_xr_struct<XrFrameWaitInfo>(XR_TYPE_FRAME_WAIT_INFO);
    XrFrameState    frame_state = make_xr_struct<XrFrameState>(XR_TYPE_FRAME_STATE);
    if (!check_xr(xrWaitFrame(session_, &wait_info, &frame_state), "xrWaitFrame")) {
        return false;
    }

    XrFrameBeginInfo begin_info = make_xr_struct<XrFrameBeginInfo>(XR_TYPE_FRAME_BEGIN_INFO);
    if (!check_xr(xrBeginFrame(session_, &begin_info), "xrBeginFrame")) {
        return false;
    }

    bool              sample_ok = true;
    XrActiveActionSet active_action_set{action_set_, XR_NULL_PATH};
    XrActionsSyncInfo sync_info     = make_xr_struct<XrActionsSyncInfo>(XR_TYPE_ACTIONS_SYNC_INFO);
    sync_info.countActiveActionSets = 1;
    sync_info.activeActionSets      = &active_action_set;
    if (!check_xr(xrSyncActions(session_, &sync_info), "xrSyncActions")) {
        sample_ok = false;
    }

    if (sample_ok && interaction_profiles_dirty_) {
        refresh_interaction_profiles();
    }

    controller_input input;
    input.sequence       = sequence_;
    input.sample_time    = clock_->now();
    input.xr_sample_time = static_cast<std::int64_t>(frame_state.predictedDisplayTime);
    if (sample_ok &&
        (!query_hand(0, frame_state.predictedDisplayTime, &input.left) ||
         !query_hand(1, frame_state.predictedDisplayTime, &input.right))) {
        sample_ok = false;
    }

    view_frame views;
    views.sequence                    = sequence_;
    views.sample_time                 = input.sample_time;
    views.xr_sample_time              = static_cast<std::int64_t>(frame_state.predictedDisplayTime);
    views.xr_predicted_display_period = static_cast<std::int64_t>(frame_state.predictedDisplayPeriod);
    views.should_render               = frame_state.shouldRender == XR_TRUE;
    if (sample_ok && !query_views(frame_state.predictedDisplayTime, &views)) {
        sample_ok = false;
    }

    update_stereo_frame();
    XrCompositionLayerProjection                    projection_layer{};
    std::array<XrCompositionLayerProjectionView, 2> projection_views{};
    const XrCompositionLayerBaseHeader*             submitted_layer = nullptr;
    if (sample_ok && views.should_render && active_stereo_frame_valid_ && located_views_valid_) {
        if (render_projection_layer(&projection_layer, &projection_views)) {
            submitted_layer = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projection_layer);
        } else {
            sample_ok = false;
        }
    }

    XrFrameEndInfo end_info       = make_xr_struct<XrFrameEndInfo>(XR_TYPE_FRAME_END_INFO);
    end_info.displayTime          = frame_state.predictedDisplayTime;
    end_info.environmentBlendMode = blend_mode_;
    end_info.layerCount           = submitted_layer == nullptr ? 0U : 1U;
    end_info.layers               = submitted_layer == nullptr ? nullptr : &submitted_layer;
    if (!check_xr(xrEndFrame(session_, &end_info), "xrEndFrame")) {
        return false;
    }
    if (!sample_ok) {
        return false;
    }

    log_input_changes(input);
    controller_writer_.put(controller_writer_.allocate(input));
    view_writer_.put(view_writer_.allocate(views));
    ++sequence_;
    return true;
}

bool openxr_quest_controller::enumerate_view_configuration() {
    std::uint32_t view_count = 0;
    if (!check_xr(xrEnumerateViewConfigurationViews(instance_, system_id_, view_configuration_type_, 0, &view_count, nullptr),
                  "xrEnumerateViewConfigurationViews(count)")) {
        return false;
    }
    if (view_count != view_configuration_views_.size()) {
        plugin_logger_->error("PRIMARY_STEREO reported {} views; this MVP requires exactly two", view_count);
        return false;
    }
    for (XrViewConfigurationView& view : view_configuration_views_) {
        view = make_xr_struct<XrViewConfigurationView>(XR_TYPE_VIEW_CONFIGURATION_VIEW);
    }
    if (!check_xr(xrEnumerateViewConfigurationViews(instance_, system_id_, view_configuration_type_, view_count, &view_count,
                                                    view_configuration_views_.data()),
                  "xrEnumerateViewConfigurationViews(list)")) {
        return false;
    }
    plugin_logger_->info(
        "OpenXR recommended eye sizes: left={}x{}, right={}x{}", view_configuration_views_[0].recommendedImageRectWidth,
        view_configuration_views_[0].recommendedImageRectHeight, view_configuration_views_[1].recommendedImageRectWidth,
        view_configuration_views_[1].recommendedImageRectHeight);
    return true;
}

bool openxr_quest_controller::create_swapchains() {
    std::uint32_t format_count = 0;
    if (!check_xr(xrEnumerateSwapchainFormats(session_, 0, &format_count, nullptr), "xrEnumerateSwapchainFormats(count)")) {
        return false;
    }
    std::vector<std::int64_t> formats(format_count);
    if (!check_xr(xrEnumerateSwapchainFormats(session_, format_count, &format_count, formats.data()),
                  "xrEnumerateSwapchainFormats(list)")) {
        return false;
    }
    const std::int64_t preferred_formats[] = {
        static_cast<std::int64_t>(GL_SRGB8_ALPHA8),
        static_cast<std::int64_t>(GL_RGBA8),
    };
    swapchain_format_ = 0;
    for (const std::int64_t candidate : preferred_formats) {
        if (std::find(formats.begin(), formats.end(), candidate) != formats.end()) {
            swapchain_format_ = candidate;
            break;
        }
    }
    if (swapchain_format_ == 0 && !formats.empty()) {
        swapchain_format_ = formats.front();
    }
    if (swapchain_format_ == 0) {
        plugin_logger_->error("The OpenXR runtime did not expose a usable OpenGL swapchain format");
        return false;
    }

    for (std::size_t eye_index = 0; eye_index < swapchain_views_.size(); ++eye_index) {
        swapchain_view&                destination   = swapchain_views_[eye_index];
        const XrViewConfigurationView& configuration = view_configuration_views_[eye_index];
        destination.width                            = configuration.recommendedImageRectWidth;
        destination.height                           = configuration.recommendedImageRectHeight;

        XrSwapchainCreateInfo create_info = make_xr_struct<XrSwapchainCreateInfo>(XR_TYPE_SWAPCHAIN_CREATE_INFO);
        create_info.usageFlags            = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
        create_info.format                = swapchain_format_;
        create_info.sampleCount           = 1;
        create_info.width                 = destination.width;
        create_info.height                = destination.height;
        create_info.faceCount             = 1;
        create_info.arraySize             = 1;
        create_info.mipCount              = 1;
        if (!check_xr(xrCreateSwapchain(session_, &create_info, &destination.handle), "xrCreateSwapchain(eye)")) {
            return false;
        }

        std::uint32_t image_count = 0;
        if (!check_xr(xrEnumerateSwapchainImages(destination.handle, 0, &image_count, nullptr),
                      "xrEnumerateSwapchainImages(count)")) {
            return false;
        }
        destination.images.resize(image_count);
        for (XrSwapchainImageOpenGLKHR& image : destination.images) {
            image = make_xr_struct<XrSwapchainImageOpenGLKHR>(XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR);
        }
        if (!check_xr(xrEnumerateSwapchainImages(destination.handle, image_count, &image_count,
                                                 reinterpret_cast<XrSwapchainImageBaseHeader*>(destination.images.data())),
                      "xrEnumerateSwapchainImages(list)")) {
            return false;
        }
        for (const XrSwapchainImageOpenGLKHR& image : destination.images) {
            glBindTexture(GL_TEXTURE_2D, image.image);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    plugin_logger_->info("Created OpenXR eye swapchains: left={}x{}, right={}x{}, format={}", swapchain_views_[0].width,
                         swapchain_views_[0].height, swapchain_views_[1].width, swapchain_views_[1].height, swapchain_format_);
    return true;
}

void openxr_quest_controller::destroy_swapchains() {
    for (swapchain_view& view : swapchain_views_) {
        if (view.handle != XR_NULL_HANDLE) {
            xrDestroySwapchain(view.handle);
            view.handle = XR_NULL_HANDLE;
        }
        view.images.clear();
        view.width  = 0;
        view.height = 0;
    }
    swapchain_format_ = 0;
}

bool openxr_quest_controller::initialize_stereo_renderer() {
    panel_program_   = create_panel_program();
    overlay_program_ = create_overlay_program();
    modal_program_   = create_modal_program();
    if (panel_program_ == 0 || overlay_program_ == 0 || modal_program_ == 0) {
        plugin_logger_->error("Failed to create the OpenGL programs used for Quest stereo submission");
        return false;
    }

    panel_source_location_        = glGetUniformLocation(panel_program_, "uSource");
    panel_mvp_location_           = glGetUniformLocation(panel_program_, "uMvp");
    panel_flip_y_location_        = glGetUniformLocation(panel_program_, "uFlipY");
    overlay_source_size_location_ = glGetUniformLocation(overlay_program_, "uSourceSize");
    modal_source_size_location_   = glGetUniformLocation(modal_program_, "uSourceSize");
    modal_texture_location_       = glGetUniformLocation(modal_program_, "uModal");

    glGenFramebuffers(1, &framebuffer_);
    glGenVertexArrays(1, &panel_vao_);

    glGenVertexArrays(1, &overlay_vao_);
    glGenBuffers(1, &overlay_vbo_);
    glBindVertexArray(overlay_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, overlay_vbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(6 * sizeof(float)), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(6 * sizeof(float)),
                          reinterpret_cast<void*>(2 * sizeof(float)));

    glGenVertexArrays(1, &modal_vao_);
    glGenBuffers(1, &modal_vbo_);
    glBindVertexArray(modal_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, modal_vbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(4 * sizeof(float)), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(4 * sizeof(float)),
                          reinterpret_cast<void*>(2 * sizeof(float)));
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    glGenTextures(static_cast<GLsizei>(modal_textures_.size()), modal_textures_.data());
    for (const GLuint texture : modal_textures_) {
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glBindTexture(GL_TEXTURE_2D, 0);

    if (framebuffer_ == 0 || panel_vao_ == 0 || overlay_vao_ == 0 || overlay_vbo_ == 0 || modal_vao_ == 0 || modal_vbo_ == 0 ||
        modal_textures_[0] == 0 || modal_textures_[1] == 0) {
        plugin_logger_->error("Failed to allocate OpenGL resources for Quest stereo submission");
        return false;
    }
    return true;
}

void openxr_quest_controller::destroy_stereo_renderer() {
    for (std::array<GLuint, 2>& texture_set : source_textures_) {
        glDeleteTextures(static_cast<GLsizei>(texture_set.size()), texture_set.data());
        texture_set = {};
    }
    glDeleteTextures(static_cast<GLsizei>(modal_textures_.size()), modal_textures_.data());
    modal_textures_ = {};
    if (overlay_vbo_ != 0) {
        glDeleteBuffers(1, &overlay_vbo_);
        overlay_vbo_ = 0;
    }
    if (modal_vbo_ != 0) {
        glDeleteBuffers(1, &modal_vbo_);
        modal_vbo_ = 0;
    }
    if (panel_vao_ != 0) {
        glDeleteVertexArrays(1, &panel_vao_);
        panel_vao_ = 0;
    }
    if (overlay_vao_ != 0) {
        glDeleteVertexArrays(1, &overlay_vao_);
        overlay_vao_ = 0;
    }
    if (modal_vao_ != 0) {
        glDeleteVertexArrays(1, &modal_vao_);
        modal_vao_ = 0;
    }
    if (framebuffer_ != 0) {
        glDeleteFramebuffers(1, &framebuffer_);
        framebuffer_ = 0;
    }
    if (panel_program_ != 0) {
        glDeleteProgram(panel_program_);
        panel_program_ = 0;
    }
    if (overlay_program_ != 0) {
        glDeleteProgram(overlay_program_);
        overlay_program_ = 0;
    }
    if (modal_program_ != 0) {
        glDeleteProgram(modal_program_);
        modal_program_ = 0;
    }

    pixel_mapping_.reset();
    overlay_mapping_.reset();
    modal_mapping_.reset();
    active_texture_set_        = -1;
    source_width_              = 0;
    source_height_             = 0;
    active_stereo_frame_id_    = 0;
    active_stereo_frame_valid_ = false;
    active_overlay_commands_   = {};
    active_modal_              = {};
    world_panel_model_         = {};
    world_panel_initialized_   = false;
}

bool openxr_quest_controller::ensure_mapped(const std::string& path, mapped_file* mapping) {
    if (path.empty()) {
        mapping->reset();
        return false;
    }
    if (mapping->data != nullptr && mapping->path == path) {
        return true;
    }
    mapping->reset();
    const int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        return false;
    }
    struct stat status{};
    if (fstat(fd, &status) != 0 || status.st_size <= 0) {
        close(fd);
        return false;
    }
    void* data = mmap(nullptr, static_cast<std::size_t>(status.st_size), PROT_READ, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        close(fd);
        return false;
    }
    mapping->fd   = fd;
    mapping->data = static_cast<const std::uint8_t*>(data);
    mapping->size = static_cast<std::size_t>(status.st_size);
    mapping->path = path;
    return true;
}

bool openxr_quest_controller::ensure_source_textures(std::uint32_t width, std::uint32_t height) {
    if (width == 0 || height == 0 || width > 8192 || height > 8192) {
        return false;
    }
    if (source_width_ == width && source_height_ == height && source_textures_[0][0] != 0 && source_textures_[0][1] != 0 &&
        source_textures_[1][0] != 0 && source_textures_[1][1] != 0) {
        return true;
    }

    while (glGetError() != GL_NO_ERROR) { }
    for (std::array<GLuint, 2>& texture_set : source_textures_) {
        glDeleteTextures(static_cast<GLsizei>(texture_set.size()), texture_set.data());
        texture_set = {};
        glGenTextures(static_cast<GLsizei>(texture_set.size()), texture_set.data());
        for (const GLuint texture : texture_set) {
            glBindTexture(GL_TEXTURE_2D, texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, static_cast<GLsizei>(width), static_cast<GLsizei>(height), 0, GL_RGBA,
                         GL_UNSIGNED_BYTE, nullptr);
        }
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    source_width_              = width;
    source_height_             = height;
    active_texture_set_        = -1;
    active_stereo_frame_valid_ = false;
    world_panel_initialized_   = false;
    return source_textures_[0][0] != 0 && source_textures_[0][1] != 0 && source_textures_[1][0] != 0 &&
        source_textures_[1][1] != 0 && glGetError() == GL_NO_ERROR;
}

bool openxr_quest_controller::update_stereo_frame() {
    const auto frame = stereo_reader_.get_ro_nullable();
    if (frame == nullptr || frame->source_frame_id == 0 || frame->source_frame_id <= active_stereo_frame_id_) {
        return false;
    }
    if (frame->format != data_format::stereo_pixel_format::rgba8_unorm || frame->left.width != frame->right.width ||
        frame->left.height != frame->right.height || frame->left.width == 0 || frame->left.height == 0 ||
        frame->left.width > 8192 || frame->left.height > 8192 || frame->left.row_stride_bytes < frame->left.width * 4U ||
        frame->right.row_stride_bytes < frame->right.width * 4U || frame->left.row_stride_bytes % 4U != 0 ||
        frame->right.row_stride_bytes % 4U != 0) {
        plugin_logger_->warn("Rejected malformed stereo_frame id={}", frame->source_frame_id);
        return false;
    }
    if (!ensure_mapped(frame->pixel_buffer_path, &pixel_mapping_)) {
        return false;
    }

    const auto image_required_bytes = [](const data_format::stereo_shared_image& image) -> std::uint64_t {
        return static_cast<std::uint64_t>(image.height - 1U) * image.row_stride_bytes +
            static_cast<std::uint64_t>(image.width) * 4U;
    };
    const std::uint64_t left_required_bytes  = image_required_bytes(frame->left);
    const std::uint64_t right_required_bytes = image_required_bytes(frame->right);
    if (frame->left.byte_count < left_required_bytes || frame->right.byte_count < right_required_bytes ||
        !byte_range_valid(pixel_mapping_.size, frame->left.byte_offset, left_required_bytes) ||
        !byte_range_valid(pixel_mapping_.size, frame->right.byte_offset, right_required_bytes)) {
        plugin_logger_->warn("Rejected out-of-range stereo_frame id={}", frame->source_frame_id);
        return false;
    }

    std::uint64_t pixel_generation = 0;
    if (!read_generation(pixel_mapping_.data, pixel_mapping_.size, frame->pixel_generation_offset, &pixel_generation) ||
        pixel_generation != frame->source_frame_id || !ensure_source_textures(frame->left.width, frame->left.height)) {
        return false;
    }

    const int candidate_texture_set = active_texture_set_ == 0 ? 1 : 0;
    while (glGetError() != GL_NO_ERROR) { }
    const data_format::stereo_shared_image* images[] = {&frame->left, &frame->right};
    for (std::size_t eye_index = 0; eye_index < 2; ++eye_index) {
        const data_format::stereo_shared_image& image = *images[eye_index];
        glBindTexture(GL_TEXTURE_2D, source_textures_[candidate_texture_set][eye_index]);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, static_cast<GLint>(image.row_stride_bytes / 4U));
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, static_cast<GLsizei>(image.width), static_cast<GLsizei>(image.height), GL_RGBA,
                        GL_UNSIGNED_BYTE, pixel_mapping_.data + image.byte_offset);
    }
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    if (glGetError() != GL_NO_ERROR) {
        plugin_logger_->warn("OpenGL rejected stereo_frame upload id={}", frame->source_frame_id);
        return false;
    }

    std::array<std::vector<float>, 2> candidate_overlay_commands;
    bool                              overlay_generation_required = false;
    if (!frame->overlay_buffer_path.empty()) {
        if (!ensure_mapped(frame->overlay_buffer_path, &overlay_mapping_)) {
            return false;
        }
        std::uint64_t overlay_generation = 0;
        if (!read_generation(overlay_mapping_.data, overlay_mapping_.size, frame->overlay_generation_offset,
                             &overlay_generation) ||
            overlay_generation != frame->source_frame_id) {
            return false;
        }
        overlay_generation_required                               = true;
        const data_format::stereo_overlay_command_range* ranges[] = {
            &frame->left_overlay_commands,
            &frame->right_overlay_commands,
        };
        for (std::size_t eye_index = 0; eye_index < 2; ++eye_index) {
            const auto& range = *ranges[eye_index];
            if (range.command_stride_floats != kOverlayCommandStrideFloats && range.command_count != 0) {
                return false;
            }
            const std::uint64_t float_count = static_cast<std::uint64_t>(range.command_count) * range.command_stride_floats;
            const std::uint64_t byte_count  = float_count * sizeof(float);
            if (!byte_range_valid(overlay_mapping_.size, range.byte_offset, byte_count)) {
                return false;
            }
            candidate_overlay_commands[eye_index].resize(static_cast<std::size_t>(float_count));
            if (byte_count != 0) {
                std::memcpy(candidate_overlay_commands[eye_index].data(), overlay_mapping_.data + range.byte_offset,
                            static_cast<std::size_t>(byte_count));
            }
        }
    } else {
        overlay_mapping_.reset();
    }

    data_format::stereo_modal_overlay candidate_modal = frame->modal;
    std::vector<std::uint8_t>         candidate_modal_pixels;
    bool                              modal_generation_required = false;
    if (!frame->modal_buffer_path.empty()) {
        if (!ensure_mapped(frame->modal_buffer_path, &modal_mapping_)) {
            return false;
        }
        std::uint64_t modal_generation = 0;
        if (!read_generation(modal_mapping_.data, modal_mapping_.size, frame->modal_generation_offset, &modal_generation) ||
            modal_generation != frame->source_frame_id) {
            return false;
        }
        modal_generation_required = true;
        if (candidate_modal.visible) {
            if (candidate_modal.width == 0 || candidate_modal.height == 0 || candidate_modal.width > 8192 ||
                candidate_modal.height > 8192 || candidate_modal.source_row_stride_bytes < candidate_modal.width * 4U) {
                return false;
            }
            const std::uint64_t modal_required_bytes =
                static_cast<std::uint64_t>(candidate_modal.height - 1U) * candidate_modal.source_row_stride_bytes +
                static_cast<std::uint64_t>(candidate_modal.width) * 4U;
            if (!byte_range_valid(modal_mapping_.size, candidate_modal.byte_offset, modal_required_bytes)) {
                return false;
            }
            const std::size_t tight_row_bytes = static_cast<std::size_t>(candidate_modal.width) * 4U;
            candidate_modal_pixels.resize(tight_row_bytes * candidate_modal.height);
            for (std::uint32_t row = 0; row < candidate_modal.height; ++row) {
                std::memcpy(candidate_modal_pixels.data() + static_cast<std::size_t>(row) * tight_row_bytes,
                            modal_mapping_.data + candidate_modal.byte_offset +
                                static_cast<std::uint64_t>(row) * candidate_modal.source_row_stride_bytes,
                            tight_row_bytes);
            }
        }
    } else {
        modal_mapping_.reset();
        if (candidate_modal.visible) {
            return false;
        }
    }

    std::uint64_t confirmed_generation = 0;
    if (!read_generation(pixel_mapping_.data, pixel_mapping_.size, frame->pixel_generation_offset, &confirmed_generation) ||
        confirmed_generation != frame->source_frame_id) {
        return false;
    }
    if (overlay_generation_required &&
        (!read_generation(overlay_mapping_.data, overlay_mapping_.size, frame->overlay_generation_offset,
                          &confirmed_generation) ||
         confirmed_generation != frame->source_frame_id)) {
        return false;
    }
    if (modal_generation_required &&
        (!read_generation(modal_mapping_.data, modal_mapping_.size, frame->modal_generation_offset, &confirmed_generation) ||
         confirmed_generation != frame->source_frame_id)) {
        return false;
    }

    if (candidate_modal.visible) {
        glBindTexture(GL_TEXTURE_2D, modal_textures_[candidate_texture_set]);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, static_cast<GLsizei>(candidate_modal.width),
                     static_cast<GLsizei>(candidate_modal.height), 0, GL_RGBA, GL_UNSIGNED_BYTE, candidate_modal_pixels.data());
        glBindTexture(GL_TEXTURE_2D, 0);
        if (glGetError() != GL_NO_ERROR) {
            return false;
        }
    }

    if (frame->presentation_mode == data_format::stereo_presentation_mode::mono_panel &&
        active_presentation_mode_ != data_format::stereo_presentation_mode::mono_panel) {
        world_panel_initialized_ = false;
    }
    active_texture_set_        = candidate_texture_set;
    active_stereo_frame_id_    = frame->source_frame_id;
    active_stereo_frame_valid_ = true;
    active_origin_             = frame->origin;
    active_presentation_mode_  = frame->presentation_mode;
    active_render_views_       = {frame->left_render_view, frame->right_render_view};
    active_overlay_commands_   = std::move(candidate_overlay_commands);
    active_modal_              = std::move(candidate_modal);
    if (active_stereo_frame_id_ == 1 || active_stereo_frame_id_ % 300 == 0) {
        plugin_logger_->info("Uploaded stereo_frame id={} size={}x{} for OpenXR submission", active_stereo_frame_id_,
                             source_width_, source_height_);
    }
    return true;
}

bool openxr_quest_controller::render_eye(std::size_t eye_index, GLuint swapchain_image) {
    if (!active_stereo_frame_valid_ || active_texture_set_ < 0 || eye_index >= 2) {
        return false;
    }
    while (glGetError() != GL_NO_ERROR) { }
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, swapchain_image, 0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        plugin_logger_->error("OpenXR swapchain framebuffer is incomplete for eye {}", eye_index);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return false;
    }

    const bool  render_as_panel   = active_presentation_mode_ != data_format::stereo_presentation_mode::stereo_fullscreen;
    mat4        mvp               = scale_matrix(2.0F, 2.0F, 1.0F);
    std::size_t texture_eye_index = eye_index;
    if (render_as_panel) {
        texture_eye_index        = 0;
        const float panel_height = kPanelWidthMeters * static_cast<float>(source_height_) /
            static_cast<float>(std::max<std::uint32_t>(source_width_, 1));
        const mat4 eye_pose = pose_matrix(located_views_[eye_index].pose);
        mat4       panel_model{};
        if (active_presentation_mode_ == data_format::stereo_presentation_mode::mono_panel) {
            if (!world_panel_initialized_) {
                const mat4 initial_head_pose = pose_matrix(located_views_[0].pose);
                panel_model                  = multiply(initial_head_pose,
                                                        multiply(translation_matrix(0.0F, kPanelYOffsetMeters, -kPanelDistanceMeters),
                                                                 scale_matrix(kPanelWidthMeters, panel_height, 1.0F)));
                std::copy(std::begin(panel_model.values), std::end(panel_model.values), world_panel_model_.begin());
                world_panel_initialized_ = true;
            } else {
                std::copy(world_panel_model_.begin(), world_panel_model_.end(), std::begin(panel_model.values));
            }
        } else {
            panel_model = multiply(eye_pose,
                                   multiply(translation_matrix(0.0F, kPanelYOffsetMeters, -kPanelDistanceMeters),
                                            scale_matrix(kPanelWidthMeters, panel_height, 1.0F)));
        }
        const mat4 view       = inverse_rigid_transform(eye_pose);
        const mat4 projection = projection_matrix(located_views_[eye_index].fov, kNearZ, kFarZ);
        mvp                   = multiply(projection, multiply(view, panel_model));
    }

    glViewport(0, 0, static_cast<GLsizei>(swapchain_views_[eye_index].width),
               static_cast<GLsizei>(swapchain_views_[eye_index].height));
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(panel_program_);
    glBindVertexArray(panel_vao_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, source_textures_[active_texture_set_][texture_eye_index]);
    glUniform1i(panel_source_location_, 0);
    glUniform1i(panel_flip_y_location_, active_origin_ == data_format::stereo_image_origin::upper_left ? 1 : 0);
    glUniformMatrix4fv(panel_mvp_location_, 1, GL_FALSE, mvp.values);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    if (!render_as_panel) {
        draw_overlay_commands(active_overlay_commands_[eye_index], overlay_program_, overlay_vao_, overlay_vbo_,
                              overlay_source_size_location_, source_width_, source_height_);
        draw_modal_overlay(active_modal_, modal_textures_[active_texture_set_], modal_program_, modal_vao_, modal_vbo_,
                           modal_source_size_location_, modal_texture_location_, eye_index, source_width_, source_height_);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindVertexArray(0);
    glUseProgram(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glFlush();
    return glGetError() == GL_NO_ERROR;
}

bool openxr_quest_controller::render_projection_layer(XrCompositionLayerProjection*                    layer,
                                                      std::array<XrCompositionLayerProjectionView, 2>* projection_views) {
    if (!active_stereo_frame_valid_ || !located_views_valid_) {
        return false;
    }
    *layer           = make_xr_struct<XrCompositionLayerProjection>(XR_TYPE_COMPOSITION_LAYER_PROJECTION);
    layer->space     = local_space_;
    layer->viewCount = static_cast<std::uint32_t>(projection_views->size());
    layer->views     = projection_views->data();

    for (std::size_t eye_index = 0; eye_index < projection_views->size(); ++eye_index) {
        XrCompositionLayerProjectionView& projection_view = (*projection_views)[eye_index];
        projection_view      = make_xr_struct<XrCompositionLayerProjectionView>(XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW);
        projection_view.pose = located_views_[eye_index].pose;
        projection_view.fov  = located_views_[eye_index].fov;
        if (active_presentation_mode_ == data_format::stereo_presentation_mode::stereo_fullscreen &&
            active_render_views_[eye_index].valid) {
            const auto& source               = active_render_views_[eye_index];
            projection_view.pose.position    = {source.position.x(), source.position.y(), source.position.z()};
            projection_view.pose.orientation = {source.orientation.x(), source.orientation.y(), source.orientation.z(),
                                                source.orientation.w()};
            projection_view.fov              = {source.angle_left, source.angle_right, source.angle_up, source.angle_down};
        }

        swapchain_view&             swapchain = swapchain_views_[eye_index];
        XrSwapchainImageAcquireInfo acquire_info =
            make_xr_struct<XrSwapchainImageAcquireInfo>(XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO);
        std::uint32_t image_index = 0;
        if (!check_xr(xrAcquireSwapchainImage(swapchain.handle, &acquire_info, &image_index), "xrAcquireSwapchainImage")) {
            return false;
        }
        XrSwapchainImageWaitInfo wait_info = make_xr_struct<XrSwapchainImageWaitInfo>(XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO);
        wait_info.timeout                  = XR_INFINITE_DURATION;
        if (!check_xr(xrWaitSwapchainImage(swapchain.handle, &wait_info), "xrWaitSwapchainImage")) {
            return false;
        }
        if (image_index >= swapchain.images.size() || !render_eye(eye_index, swapchain.images[image_index].image)) {
            XrSwapchainImageReleaseInfo release_info =
                make_xr_struct<XrSwapchainImageReleaseInfo>(XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO);
            xrReleaseSwapchainImage(swapchain.handle, &release_info);
            return false;
        }
        XrSwapchainImageReleaseInfo release_info =
            make_xr_struct<XrSwapchainImageReleaseInfo>(XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO);
        if (!check_xr(xrReleaseSwapchainImage(swapchain.handle, &release_info), "xrReleaseSwapchainImage")) {
            return false;
        }

        projection_view.subImage.swapchain        = swapchain.handle;
        projection_view.subImage.imageRect.offset = {0, 0};
        projection_view.subImage.imageRect.extent = {static_cast<std::int32_t>(swapchain.width),
                                                     static_cast<std::int32_t>(swapchain.height)};
        projection_view.subImage.imageArrayIndex  = 0;
    }
    return true;
}

bool openxr_quest_controller::query_views(XrTime sample_time, view_frame* frame) {
    for (XrView& view : located_views_) {
        view = make_xr_struct<XrView>(XR_TYPE_VIEW);
    }

    XrViewLocateInfo locate_info      = make_xr_struct<XrViewLocateInfo>(XR_TYPE_VIEW_LOCATE_INFO);
    locate_info.viewConfigurationType = view_configuration_type_;
    locate_info.displayTime           = sample_time;
    locate_info.space                 = local_space_;

    XrViewState   view_state = make_xr_struct<XrViewState>(XR_TYPE_VIEW_STATE);
    std::uint32_t view_count = 0;
    if (!check_xr(xrLocateViews(session_, &locate_info, &view_state, static_cast<std::uint32_t>(located_views_.size()),
                                &view_count, located_views_.data()),
                  "xrLocateViews")) {
        located_views_valid_ = false;
        return false;
    }
    if (view_count != located_views_.size()) {
        plugin_logger_->error("xrLocateViews returned {} views; expected two", view_count);
        located_views_valid_ = false;
        return false;
    }

    const bool pose_valid = (view_state.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) != 0 &&
        (view_state.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) != 0;
    const bool pose_tracked = (view_state.viewStateFlags & XR_VIEW_STATE_POSITION_TRACKED_BIT) != 0 &&
        (view_state.viewStateFlags & XR_VIEW_STATE_ORIENTATION_TRACKED_BIT) != 0;
    located_views_valid_ = pose_valid;

    const auto copy_view = [pose_valid, pose_tracked](const XrView& source, const XrViewConfigurationView& configuration,
                                                      eye_view* destination) {
        destination->pose_valid         = pose_valid;
        destination->pose_tracked       = pose_tracked;
        destination->recommended_width  = configuration.recommendedImageRectWidth;
        destination->recommended_height = configuration.recommendedImageRectHeight;
        destination->angle_left         = source.fov.angleLeft;
        destination->angle_right        = source.fov.angleRight;
        destination->angle_up           = source.fov.angleUp;
        destination->angle_down         = source.fov.angleDown;
        if (pose_valid) {
            destination->position    = {source.pose.position.x, source.pose.position.y, source.pose.position.z};
            destination->orientation = {source.pose.orientation.w, source.pose.orientation.x, source.pose.orientation.y,
                                        source.pose.orientation.z};
        }
    };
    copy_view(located_views_[0], view_configuration_views_[0], &frame->left);
    copy_view(located_views_[1], view_configuration_views_[1], &frame->right);
    return true;
}

bool openxr_quest_controller::query_hand(std::size_t hand_index, XrTime sample_time, hand_controller* hand) {
    *hand                     = hand_controller{};
    hand->interaction_profile = interaction_profiles_[hand_index];

    if (!query_pose(grip_pose_action_, grip_spaces_[hand_index], hand_paths_[hand_index], sample_time, &hand->grip_pose) ||
        !query_pose(aim_pose_action_, aim_spaces_[hand_index], hand_paths_[hand_index], sample_time, &hand->aim_pose)) {
        return false;
    }

    controller_button trigger_click;
    controller_button trigger_value;
    if (!query_boolean(trigger_click_action_, hand_paths_[hand_index], &trigger_click) ||
        !query_float(trigger_value_action_, hand_paths_[hand_index], kTriggerPressedThreshold, &trigger_value) ||
        !query_float(squeeze_value_action_, hand_paths_[hand_index], kSqueezePressedThreshold, &hand->squeeze) ||
        !query_boolean(primary_click_action_, hand_paths_[hand_index], &hand->primary) ||
        !query_boolean(secondary_click_action_, hand_paths_[hand_index], &hand->secondary) ||
        !query_boolean(thumbstick_click_action_, hand_paths_[hand_index], &hand->thumbstick_click) ||
        !query_axis2d(thumbstick_axis_action_, hand_paths_[hand_index], &hand->thumbstick)) {
        return false;
    }
    hand->trigger = trigger_click;
    merge_button(&hand->trigger, trigger_value);

    hand->available = hand->grip_pose.active || hand->aim_pose.active || hand->trigger.active || hand->squeeze.active ||
        hand->primary.active || hand->secondary.active || hand->thumbstick_click.active || hand->thumbstick.active;
    return true;
}

bool openxr_quest_controller::query_pose(XrAction action, XrSpace action_space, XrPath hand_path, XrTime sample_time,
                                         controller_pose* pose) {
    *pose                         = controller_pose{};
    XrActionStateGetInfo get_info = make_xr_struct<XrActionStateGetInfo>(XR_TYPE_ACTION_STATE_GET_INFO);
    get_info.action               = action;
    get_info.subactionPath        = hand_path;

    XrActionStatePose state = make_xr_struct<XrActionStatePose>(XR_TYPE_ACTION_STATE_POSE);
    if (!check_xr(xrGetActionStatePose(session_, &get_info, &state), "xrGetActionStatePose")) {
        return false;
    }
    pose->active = state.isActive == XR_TRUE;
    if (!pose->active) {
        return true;
    }

    XrSpaceLocation location = make_xr_struct<XrSpaceLocation>(XR_TYPE_SPACE_LOCATION);
    if (!check_xr(xrLocateSpace(action_space, local_space_, sample_time, &location), "xrLocateSpace(controller)")) {
        return false;
    }

    pose->position_valid      = (location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0;
    pose->orientation_valid   = (location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0;
    pose->position_tracked    = (location.locationFlags & XR_SPACE_LOCATION_POSITION_TRACKED_BIT) != 0;
    pose->orientation_tracked = (location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT) != 0;

    if (pose->position_valid) {
        pose->position = {location.pose.position.x, location.pose.position.y, location.pose.position.z};
    }
    if (pose->orientation_valid) {
        pose->orientation = {location.pose.orientation.w, location.pose.orientation.x, location.pose.orientation.y,
                             location.pose.orientation.z};
    }
    return true;
}

bool openxr_quest_controller::query_boolean(XrAction action, XrPath hand_path, controller_button* button) {
    *button                       = controller_button{};
    XrActionStateGetInfo get_info = make_xr_struct<XrActionStateGetInfo>(XR_TYPE_ACTION_STATE_GET_INFO);
    get_info.action               = action;
    get_info.subactionPath        = hand_path;

    XrActionStateBoolean state = make_xr_struct<XrActionStateBoolean>(XR_TYPE_ACTION_STATE_BOOLEAN);
    if (!check_xr(xrGetActionStateBoolean(session_, &get_info, &state), "xrGetActionStateBoolean")) {
        return false;
    }
    button->active                  = state.isActive == XR_TRUE;
    button->pressed                 = button->active && state.currentState == XR_TRUE;
    button->changed_since_last_sync = state.changedSinceLastSync == XR_TRUE;
    button->value                   = button->pressed ? 1.0F : 0.0F;
    button->last_change_time        = static_cast<std::int64_t>(state.lastChangeTime);
    return true;
}

bool openxr_quest_controller::query_float(XrAction action, XrPath hand_path, float pressed_threshold,
                                          controller_button* button) {
    *button                       = controller_button{};
    XrActionStateGetInfo get_info = make_xr_struct<XrActionStateGetInfo>(XR_TYPE_ACTION_STATE_GET_INFO);
    get_info.action               = action;
    get_info.subactionPath        = hand_path;

    XrActionStateFloat state = make_xr_struct<XrActionStateFloat>(XR_TYPE_ACTION_STATE_FLOAT);
    if (!check_xr(xrGetActionStateFloat(session_, &get_info, &state), "xrGetActionStateFloat")) {
        return false;
    }
    button->active                  = state.isActive == XR_TRUE;
    button->value                   = button->active ? state.currentState : 0.0F;
    button->pressed                 = button->active && state.currentState >= pressed_threshold;
    button->changed_since_last_sync = state.changedSinceLastSync == XR_TRUE;
    button->last_change_time        = static_cast<std::int64_t>(state.lastChangeTime);
    return true;
}

bool openxr_quest_controller::query_axis2d(XrAction action, XrPath hand_path, controller_axis2d* axis) {
    *axis                         = controller_axis2d{};
    XrActionStateGetInfo get_info = make_xr_struct<XrActionStateGetInfo>(XR_TYPE_ACTION_STATE_GET_INFO);
    get_info.action               = action;
    get_info.subactionPath        = hand_path;

    XrActionStateVector2f state = make_xr_struct<XrActionStateVector2f>(XR_TYPE_ACTION_STATE_VECTOR2F);
    if (!check_xr(xrGetActionStateVector2f(session_, &get_info, &state), "xrGetActionStateVector2f")) {
        return false;
    }
    axis->active                  = state.isActive == XR_TRUE;
    axis->changed_since_last_sync = state.changedSinceLastSync == XR_TRUE;
    axis->last_change_time        = static_cast<std::int64_t>(state.lastChangeTime);
    if (axis->active) {
        axis->value = {state.currentState.x, state.currentState.y};
    }
    return true;
}

bool openxr_quest_controller::refresh_interaction_profiles() {
    bool success = true;
    for (std::size_t hand_index = 0; hand_index < hand_paths_.size(); ++hand_index) {
        XrInteractionProfileState state  = make_xr_struct<XrInteractionProfileState>(XR_TYPE_INTERACTION_PROFILE_STATE);
        const XrResult            result = xrGetCurrentInteractionProfile(session_, hand_paths_[hand_index], &state);
        if (XR_FAILED(result)) {
            plugin_logger_->warn("xrGetCurrentInteractionProfile({}) failed: {}", hand_index == 0 ? "left" : "right",
                                 xr_result_string(result));
            success = false;
            continue;
        }

        const std::string        raw_profile = path_string(state.interactionProfile);
        const controller_profile profile     = profile_from_path(raw_profile);
        if (profile != interaction_profiles_[hand_index]) {
            interaction_profiles_[hand_index] = profile;
            plugin_logger_->info("{} interaction profile -> {}", hand_index == 0 ? "left" : "right",
                                 raw_profile.empty() ? "none" : raw_profile);
        }
    }
    interaction_profiles_dirty_ = false;
    return success;
}

void openxr_quest_controller::log_input_changes(const controller_input& input) {
    if (log_input_) {
        const controller_input  empty_previous;
        const controller_input& previous = have_previous_input_ ? previous_input_ : empty_previous;
        log_hand_changes("left", true, input.left, previous.left, !have_previous_input_);
        log_hand_changes("right", false, input.right, previous.right, !have_previous_input_);
    }
    previous_input_      = input;
    have_previous_input_ = true;
}

void openxr_quest_controller::log_hand_changes(const char* hand_name, bool left, const hand_controller& current,
                                               const hand_controller& previous, bool first_sample) {
    if ((!first_sample && current.available != previous.available) || (first_sample && current.available)) {
        plugin_logger_->info("{} controller {}", hand_name, current.available ? "AVAILABLE" : "UNAVAILABLE");
    }
    if ((!first_sample && current.interaction_profile != previous.interaction_profile) ||
        (first_sample && current.interaction_profile != controller_profile::none)) {
        plugin_logger_->info("{} controller profile {}", hand_name, profile_label(current.interaction_profile));
    }

    const bool tracked            = current.tracked();
    const bool previously_tracked = previous.tracked();
    if ((!first_sample && tracked != previously_tracked) || (first_sample && tracked)) {
        plugin_logger_->info("{} controller tracking {}", hand_name, tracked ? "ACQUIRED" : "LOST");
    }

    log_button_change(hand_name, "trigger", current.trigger, previous.trigger, first_sample);
    log_button_change(hand_name, "squeeze", current.squeeze, previous.squeeze, first_sample);
    log_button_change(hand_name, left ? "primary (X)" : "primary (A)", current.primary, previous.primary, first_sample);
    log_button_change(hand_name, left ? "secondary (Y)" : "secondary (B)", current.secondary, previous.secondary, first_sample);
    log_button_change(hand_name, "thumbstick click", current.thumbstick_click, previous.thumbstick_click, first_sample);

    const bool thumbstick_moved = current.thumbstick.active && current.thumbstick.value.norm() >= kThumbstickMoveThreshold;
    const bool previous_moved   = previous.thumbstick.active && previous.thumbstick.value.norm() >= kThumbstickMoveThreshold;
    if ((!first_sample && thumbstick_moved != previous_moved) || (first_sample && thumbstick_moved)) {
        if (thumbstick_moved) {
            plugin_logger_->info("{} thumbstick MOVED x={:.2f} y={:.2f}", hand_name, current.thumbstick.value.x(),
                                 current.thumbstick.value.y());
        } else {
            plugin_logger_->info("{} thumbstick CENTERED", hand_name);
        }
    }
}

void openxr_quest_controller::log_button_change(const char* hand_name, const char* button_name,
                                                const controller_button& current, const controller_button& previous,
                                                bool first_sample) {
    if ((!first_sample && current.pressed != previous.pressed) || (first_sample && current.pressed)) {
        plugin_logger_->info("{} {} {} value={:.2f}", hand_name, button_name, current.pressed ? "PRESSED" : "RELEASED",
                             current.value);
    }
}

void openxr_quest_controller::destroy_openxr() {
    if (session_ != XR_NULL_HANDLE && session_running_) {
        const XrResult result = xrRequestExitSession(session_);
        if (XR_FAILED(result) && result != XR_ERROR_SESSION_NOT_RUNNING) {
            plugin_logger_->debug("xrRequestExitSession during shutdown returned {}", xr_result_string(result));
        }
    }

    destroy_stereo_renderer();
    destroy_swapchains();

    if (local_space_ != XR_NULL_HANDLE) {
        xrDestroySpace(local_space_);
        local_space_ = XR_NULL_HANDLE;
    }
    for (XrSpace& space : aim_spaces_) {
        if (space != XR_NULL_HANDLE) {
            xrDestroySpace(space);
            space = XR_NULL_HANDLE;
        }
    }
    for (XrSpace& space : grip_spaces_) {
        if (space != XR_NULL_HANDLE) {
            xrDestroySpace(space);
            space = XR_NULL_HANDLE;
        }
    }
    if (session_ != XR_NULL_HANDLE) {
        xrDestroySession(session_);
        session_ = XR_NULL_HANDLE;
    }
    if (action_set_ != XR_NULL_HANDLE) {
        xrDestroyActionSet(action_set_);
        action_set_ = XR_NULL_HANDLE;
    }
    if (instance_ != XR_NULL_HANDLE) {
        xrDestroyInstance(instance_);
        instance_ = XR_NULL_HANDLE;
    }

    system_id_                  = XR_NULL_SYSTEM_ID;
    session_running_            = false;
    session_state_              = XR_SESSION_STATE_UNKNOWN;
    interaction_profiles_dirty_ = true;
    interaction_profiles_       = {controller_profile::none, controller_profile::none};
    located_views_valid_        = false;
}

bool openxr_quest_controller::check_xr(XrResult result, const char* operation) const {
    if (XR_SUCCEEDED(result)) {
        return true;
    }
    plugin_logger_->error("{} failed: {}", operation, xr_result_string(result));
    return false;
}

std::string openxr_quest_controller::xr_result_string(XrResult result) const {
    if (instance_ != XR_NULL_HANDLE) {
        char result_buffer[XR_MAX_RESULT_STRING_SIZE]{};
        if (XR_SUCCEEDED(xrResultToString(instance_, result, result_buffer))) {
            return result_buffer;
        }
    }
    return std::to_string(result);
}

std::string openxr_quest_controller::path_string(XrPath path) const {
    if (path == XR_NULL_PATH || instance_ == XR_NULL_HANDLE) {
        return {};
    }

    std::uint32_t required_size = 0;
    if (XR_FAILED(xrPathToString(instance_, path, 0, &required_size, nullptr)) || required_size == 0) {
        return {};
    }
    std::vector<char> buffer(required_size);
    if (XR_FAILED(xrPathToString(instance_, path, required_size, &required_size, buffer.data()))) {
        return {};
    }
    return buffer.data();
}

openxr_quest_controller::controller_profile openxr_quest_controller::profile_from_path(const std::string& path) {
    if (path.empty()) {
        return controller_profile::none;
    }
    if (path == kSimpleControllerProfile) {
        return controller_profile::simple_controller;
    }
    if (path == kOculusTouchProfile) {
        return controller_profile::oculus_touch;
    }
    if (path == kHtcViveProfile) {
        return controller_profile::htc_vive;
    }
    if (path == kValveIndexProfile) {
        return controller_profile::valve_index;
    }
    if (path == kMicrosoftMotionProfile) {
        return controller_profile::microsoft_motion;
    }
    return controller_profile::unknown;
}

const char* openxr_quest_controller::profile_label(controller_profile profile) {
    switch (profile) {
    case controller_profile::none:
        return "none";
    case controller_profile::simple_controller:
        return "simple_controller";
    case controller_profile::oculus_touch:
        return "oculus_touch";
    case controller_profile::htc_vive:
        return "htc_vive";
    case controller_profile::valve_index:
        return "valve_index";
    case controller_profile::microsoft_motion:
        return "microsoft_motion";
    case controller_profile::unknown:
        return "unknown";
    }
    return "unknown";
}

const char* openxr_quest_controller::session_state_label(XrSessionState state) {
    switch (state) {
    case XR_SESSION_STATE_UNKNOWN:
        return "UNKNOWN";
    case XR_SESSION_STATE_IDLE:
        return "IDLE";
    case XR_SESSION_STATE_READY:
        return "READY";
    case XR_SESSION_STATE_SYNCHRONIZED:
        return "SYNCHRONIZED";
    case XR_SESSION_STATE_VISIBLE:
        return "VISIBLE";
    case XR_SESSION_STATE_FOCUSED:
        return "FOCUSED";
    case XR_SESSION_STATE_STOPPING:
        return "STOPPING";
    case XR_SESSION_STATE_LOSS_PENDING:
        return "LOSS_PENDING";
    case XR_SESSION_STATE_EXITING:
        return "EXITING";
    case XR_SESSION_STATE_MAX_ENUM:
        return "MAX_ENUM";
    }
    return "UNKNOWN";
}

void openxr_quest_controller::merge_button(controller_button* destination, const controller_button& source) {
    destination->active                  = destination->active || source.active;
    destination->pressed                 = destination->pressed || source.pressed;
    destination->changed_since_last_sync = destination->changed_since_last_sync || source.changed_since_last_sync;
    destination->value                   = std::max(destination->value, source.value);
    destination->last_change_time        = std::max(destination->last_change_time, source.last_change_time);
}

} // namespace ILLIXR

using namespace ILLIXR;

PLUGIN_MAIN(openxr_quest_controller)
