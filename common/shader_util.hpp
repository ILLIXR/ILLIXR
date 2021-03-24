#include <iostream>
#include <fstream>
#include <sstream>
#include "GL/gl.h"
#include <string>
#include <cstring>
#include <vector>
#include "error_util.hpp"

using namespace ILLIXR;

static constexpr std::size_t GL_MAX_LOG_LENGTH = 4096U;


static void GLAPIENTRY
	MessageCallback([[maybe_unused]] GLenum source,
					[[maybe_unused]] GLenum type,
					[[maybe_unused]] GLuint id,
					[[maybe_unused]] GLenum severity,
					[[maybe_unused]] GLsizei length,
					[[maybe_unused]] const GLchar* message,
					[[maybe_unused]] const void* userParam )
{
#ifndef NDEBUG
    if (severity == GL_DEBUG_SEVERITY_NOTIFICATION) {
        /// Don't show message if severity level is notification. Non-fatal.
        return;
    }
    std::cerr << "GL CALLBACK: " << (type == GL_DEBUG_TYPE_ERROR ? "** GL ERROR **" : "")
              << " type = 0x" << std::hex << type << std::dec
              << ", severity = 0x" << std::hex << severity << std::dec
              << ", message = " << message
              << std::endl;
	// https://www.khronos.org/opengl/wiki/Debug_Output#Message_Components
    if (severity == GL_DEBUG_SEVERITY_HIGH) {
        /// Fatal error if severity level is high.
        ILLIXR::abort();
    } /// else => severity level low and medium are non-fatal.
#endif
}

static GLuint init_and_link (const char* vertex_shader, const char* fragment_shader){

    // GL handles for intermediary objects.
    GLint result, vertex_shader_handle, fragment_shader_handle, shader_program;

    vertex_shader_handle = glCreateShader(GL_VERTEX_SHADER);
    GLint vshader_len = strlen(vertex_shader);
    glShaderSource(vertex_shader_handle, 1, &vertex_shader, &vshader_len);
    glCompileShader(vertex_shader_handle);
    glGetShaderiv(vertex_shader_handle, GL_COMPILE_STATUS, &result);
    if (result == GL_FALSE) {
        GLsizei length = 0;
        std::vector<GLchar> gl_buf_log;
        gl_buf_log.resize(GL_MAX_LOG_LENGTH);

        glGetShaderInfoLog(vertex_shader_handle, GL_MAX_LOG_LENGTH*sizeof(GLchar), &length, gl_buf_log.data());
        const std::string msg{gl_buf_log.begin(), gl_buf_log.end()};
        assert(length == static_cast<GLsizei>(msg.size()) && "Length of log should match GLchar vector contents");
        ILLIXR::abort("[shader_util] Failed to get vertex_shader_handle: " + msg);
    }

    GLint fragResult = GL_FALSE;
    fragment_shader_handle = glCreateShader(GL_FRAGMENT_SHADER);
    GLint fshader_len = strlen(fragment_shader);
    glShaderSource(fragment_shader_handle, 1, &fragment_shader, &fshader_len);
    glCompileShader(fragment_shader_handle);
    glGetShaderiv(fragment_shader_handle, GL_COMPILE_STATUS, &fragResult);
    if (fragResult == GL_FALSE) {
        GLsizei length = 0;
        std::vector<GLchar> gl_buf_log;
        gl_buf_log.resize(GL_MAX_LOG_LENGTH);

        glGetShaderInfoLog(fragment_shader_handle, GL_MAX_LOG_LENGTH*sizeof(GLchar), &length, gl_buf_log.data());
        const std::string msg{gl_buf_log.begin(), gl_buf_log.end()};
        assert(length == static_cast<GLsizei>(msg.size()) && "Length of log should match GLchar vector contents");
        ILLIXR::abort("[shader_util] Failed to get fragment_shader_handle: " + msg);
    }

    // Create program and link shaders
    shader_program = glCreateProgram();
    glAttachShader(shader_program, vertex_shader_handle);
    glAttachShader(shader_program, fragment_shader_handle);
    const GLenum gl_err_attach = glGetError();
    if (gl_err_attach != GL_NO_ERROR) {
        ILLIXR::abort("[shader_util] AttachShader or createProgram failed");
    }

    ///////////////////
    // Link and verify

    glLinkProgram(shader_program);

    const GLenum gl_err_link = glGetError();
    if (gl_err_link != GL_NO_ERROR) {
        ILLIXR::abort("[shader_util] Linking failed");
    }

    glGetProgramiv(shader_program, GL_LINK_STATUS, &result);
    if (result == GL_FALSE) {
        GLsizei length = 0;
	    std::vector<GLchar> gl_buf_log;
        gl_buf_log.resize(GL_MAX_LOG_LENGTH);

	    glGetProgramInfoLog(shader_program, GL_MAX_LOG_LENGTH*sizeof(GLchar), &length, gl_buf_log.data());
        const std::string msg{gl_buf_log.begin(), gl_buf_log.end()};
        assert(length == static_cast<GLsizei>(msg.size()) && "Length of log should match GLchar vector contents");
        ILLIXR::abort("[shader_util] Failed to get shader program: " + msg);
    }

    // After successful link, detach shaders from shader program
    glDetachShader(shader_program, vertex_shader_handle);
    glDetachShader(shader_program, fragment_shader_handle);

    return shader_program;
}
