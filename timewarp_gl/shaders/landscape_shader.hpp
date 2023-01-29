#include <GL/gl.h>

static const float quad_vertices[] = {
    -1.0f, -1.0f, 
    -1.0f,  1.0f, 
    1.0f,  1.0f, 
    1.0f, -1.0f
};

static const float quad_texcoords[] = {
    0.0f, 0.0f, 
    0.0f, 1.0f, 
    1.0f, 1.0f, 
    1.0f, 0.0f
};

static const int quad_indices[] = {
    0, 2, 1,
    3, 2, 0
};


static const char* const landscapeVertexShader = "#version " GLSL_VERSION "\n"
                                                 "in vec3 vertexPosition;\n"
                                                 "in vec2 vertexUV;\n"
                                                 "out vec2 vUV;\n"
                                                 "out gl_PerVertex { vec4 gl_Position; };\n"
                                                 "void main()\n"
                                                 "{\n"
                                                 "   gl_Position = vec4( vertexPosition, 1.0 );\n"
                                                 "   vUV = vertexUV;\n"
                                                 "}\n";

static const char* const landscapeFragmentShader = "#version " GLSL_VERSION "\n"
                                                   "uniform highp sampler2DArray leftTexture;\n"
                                                   "uniform highp sampler2DArray rightTexture;\n"
                                                   "in vec2 vUV;\n"
                                                   "out lowp vec4 outcolor;\n"
                                                   "void main()\n"
                                                   "{\n"
                                                   "	if (vUV.x < 0.5) {\n"
                                                   "        vec2 uv = vec2(2.0 * vUV.x, vUV.y);\n"
                                                   "		outcolor = texture(leftTexture, vec3(uv, 0));\n"
                                                   "    }\n"
                                                   "    else {\n"
                                                   "        vec2 uv = vec2(2.0 * (vUV.x - 0.5, vUV.y);\n"
                                                   "        outcolor = texture(rightTexture, vec3(uv, 0));\n"
                                                   "    }\n"
                                                   "}\n";
