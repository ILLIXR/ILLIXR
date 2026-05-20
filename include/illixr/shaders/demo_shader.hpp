#pragma once
#ifdef __ANDROID__
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#endif

static const char* const demo_vertex_shader =
#ifdef __ANDROID__
    "precision mediump float;"
    "attribute vec3 in_position;\n"
    "attribute vec2 in_uv;\n"
#else
    "#version " GLSL_VERSION "\n"
    "layout(location = 0) in vec3 in_position;\n"
    "layout(location = 1) in vec2 in_uv;\n"
#endif
    "uniform mat4 u_modelview;\n"
    "uniform mat4 u_projection;\n"
#ifdef __ANDROID__
    "varying vec2 uv;\n"
#else
    "out mediump vec2 uv;\n"
#endif
    "void main() {\n"
    "    gl_Position = u_projection * u_modelview * vec4(in_position,1.0);\n"
    "    uv = in_uv;\n"
    "}\n";


static const char* const demo_fragment_shader =
#ifdef __ANDROID__
    "precision mediump float;\n"
    "uniform sampler2D main_tex;\n"
    "varying vec2 uv;\n"
    "varying vec4 outcolor;\n"
    "\n "
    "void main()\n"
    "{\n"
    "       gl_FragColor = texture2D(main_tex, uv);\n"
#else
    "#version " GLSL_VERSION "\n"
    "precision mediump float;\n"
    "uniform highp sampler2D main_tex;\n"
    "in mediump vec2 uv;\n"
    "out lowp vec4 outcolor;\n"
    "void main() {\n"
    "       outcolor = texture(main_tex, uv);\n"
#endif
    "}\n";
