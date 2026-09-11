#pragma once

// Quest 3 Storage Permissions:
// - For app-specific storage (recommended): No permissions needed
// - Path: /storage/emulated/0/Android/data/<package>/files/
//
// To retrieve files via ADB:
//   adb pull /storage/emulated/0/Android/data/com.yourapp/files/frame_dumps/

#include <android/log.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <GLES2/gl2ext.h>
#include <GLES3/gl3.h>
#include <GLES3/gl32.h>
#include <spdlog/spdlog.h>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace ILLIXR {

class frame_dumper {
public:
    // Initialize the frame dumper
    explicit frame_dumper(const std::string& base_path, int max_dumps = 200)
        : base_path_(base_path)
        , max_dumps_(max_dumps)
        , dump_count_(0) {
        // Create dump directory
        dump_dir_ = base_path_ + "/frame_dumps";
        mkdir(dump_dir_.c_str(), 0755);

        spdlog::get("illixr")->info("frame_dumper: Initialized, output dir: {}", dump_dir_);
    }

    // Dump raw NV12 data to a file
    void dump_nv12(const uint8_t* data, int width, int height, int eye, uint64_t frame_num) {
        if (dump_count_ >= max_dumps_)
            return;

        std::string filename = dump_dir_ + "/frame_" + std::to_string(frame_num) + "_eye" + std::to_string(eye) + "_" +
            std::to_string(width) + "x" + std::to_string(height) + ".nv12";

        size_t size = width * height * 3 / 2; // NV12 size

        std::ofstream file(filename, std::ios::binary);
        if (file.is_open()) {
            file.write(reinterpret_cast<const char*>(data), size);
            file.close();
            spdlog::get("illixr")->info("frame_dumper: Wrote NV12 {}", filename);
            dump_count_++;
        } else {
            spdlog::get("illixr")->error("frame_dumper: Failed to open {}", filename);
        }
    }

    // Dump raw NV12 data and also convert to PPM for easy viewing
    void dump_nv12_as_ppm(const uint8_t* data, int width, int height, int eye, uint64_t frame_num) {
        if (dump_count_ >= max_dumps_)
            return;

        // First dump raw NV12
        dump_nv12(data, width, height, eye, frame_num);

        // Convert to RGB and save as PPM (simple format that's easy to view)
        std::vector<uint8_t> rgb(width * height * 3);
        nv12_to_rgb(data, rgb.data(), width, height);

        std::string filename = dump_dir_ + "/frame_" + std::to_string(frame_num) + "_eye" + std::to_string(eye) + ".ppm";

        std::ofstream file(filename, std::ios::binary);
        if (file.is_open()) {
            // PPM header
            file << "P6\n" << width << " " << height << "\n255\n";
            file.write(reinterpret_cast<const char*>(rgb.data()), rgb.size());
            file.close();
            spdlog::get("illixr")->info("frame_dumper: Wrote PPM {}", filename);
        }
    }

    // Dump a GL texture to disk (reads back from GPU)
    void dump_gl_texture(GLuint texture_id, int width, int height, int eye, uint64_t frame_num, bool is_external_oes = false) {
        if (dump_count_ >= max_dumps_)
            return;

        if (is_external_oes) {
            // External OES textures can't be read back directly
            // We need to render to an FBO and read from that
            dump_external_oes_texture(texture_id, width, height, eye, frame_num);
        } else {
            dump_regular_texture(texture_id, width, height, eye, frame_num);
        }
    }

    // Dump the current framebuffer contents
    void dump_framebuffer(int width, int height, int eye, uint64_t frame_num) {
        if (dump_count_ >= max_dumps_)
            return;

        std::vector<uint8_t> pixels(width * height * 4); // RGBA
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

        GLenum err = glGetError();
        if (err != GL_NO_ERROR) {
            spdlog::get("illixr")->error("frame_dumper: glReadPixels failed: 0x{:X}", err);
            return;
        }

        // Convert RGBA to RGB for PPM
        std::vector<uint8_t> rgb(width * height * 3);
        for (int i = 0; i < width * height; i++) {
            rgb[i * 3 + 0] = pixels[i * 4 + 0];
            rgb[i * 3 + 1] = pixels[i * 4 + 1];
            rgb[i * 3 + 2] = pixels[i * 4 + 2];
        }

        std::string filename = dump_dir_ + "/fb_" + std::to_string(frame_num) + "_eye" + std::to_string(eye) + ".ppm";

        std::ofstream file(filename, std::ios::binary);
        if (file.is_open()) {
            file << "P6\n" << width << " " << height << "\n255\n";
            file.write(reinterpret_cast<const char*>(rgb.data()), rgb.size());
            file.close();
            spdlog::get("illixr")->info("frame_dumper: Wrote framebuffer {}", filename);
            dump_count_++;
        }
    }

    // Log texture info without dumping
    void log_texture_info(GLuint texture_id, const char* name) {
        GLint current_tex = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &current_tex);

        glBindTexture(GL_TEXTURE_2D, texture_id);

        GLint width = 0, height = 0, internal_format = 0;
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &internal_format);

        spdlog::get("illixr")->info("Texture '{}' (ID {}): {}x{}, format=0x{:X}", name, texture_id, width, height,
                                    internal_format);

        glBindTexture(GL_TEXTURE_2D, current_tex);
    }

    // Get number of dumps made
    int get_dump_count() const {
        return dump_count_;
    }

    // Reset dump counter
    void reset_count() {
        dump_count_ = 0;
    }

    // Get the dump directory path
    const std::string& get_dump_dir() const {
        return dump_dir_;
    }

private:
    void nv12_to_rgb(const uint8_t* nv12, uint8_t* rgb, int width, int height) {
        const uint8_t* y_plane  = nv12;
        const uint8_t* uv_plane = nv12 + width * height;

        for (int j = 0; j < height; j++) {
            for (int i = 0; i < width; i++) {
                int y_idx  = j * width + i;
                int uv_idx = (j / 2) * width + (i & ~1); // UV is half resolution, interleaved

                int Y = y_plane[y_idx];
                int U = uv_plane[uv_idx];
                int V = uv_plane[uv_idx + 1];

                // BT.601 conversion (commonly used)
                int C = Y - 16;
                int D = U - 128;
                int E = V - 128;

                int R = (298 * C + 409 * E + 128) >> 8;
                int G = (298 * C - 100 * D - 208 * E + 128) >> 8;
                int B = (298 * C + 516 * D + 128) >> 8;

                // Clamp
                R = R < 0 ? 0 : (R > 255 ? 255 : R);
                G = G < 0 ? 0 : (G > 255 ? 255 : G);
                B = B < 0 ? 0 : (B > 255 ? 255 : B);

                int rgb_idx      = (j * width + i) * 3;
                rgb[rgb_idx + 0] = static_cast<uint8_t>(R);
                rgb[rgb_idx + 1] = static_cast<uint8_t>(G);
                rgb[rgb_idx + 2] = static_cast<uint8_t>(B);
            }
        }
    }

    void dump_regular_texture(GLuint texture_id, int width, int height, int eye, uint64_t frame_num) {
        // Create FBO to read from texture
        GLuint fbo;
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture_id, 0);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
            dump_framebuffer(width, height, eye, frame_num);
        } else {
            spdlog::get("illixr")->error("frame_dumper: Framebuffer incomplete for texture read");
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &fbo);
    }

    void dump_external_oes_texture(GLuint texture_id, int width, int height, int eye, uint64_t frame_num) {
        // For external OES textures, we need to render to an intermediate texture
        // This requires setting up a simple shader - for now just log that we can't dump directly
        spdlog::get("illixr")->warn("frame_dumper: External OES texture {} - use dump_framebuffer() after rendering instead",
                                    texture_id);

        // Alternative: dump framebuffer after the texture has been rendered
        // The caller should use dump_framebuffer() after render_eye() instead
    }

    std::string base_path_;
    std::string dump_dir_;
    int         max_dumps_;
    int         dump_count_;
};

// Get the app's external files directory (no permissions needed)
inline std::string get_app_files_dir(android_app* app) {
    if (!app || !app->activity) {
        return "";
    }

    // Get JNI env
    JNIEnv* env = nullptr;
    app->activity->vm->AttachCurrentThread(&env, nullptr);
    if (!env) {
        return "";
    }

    // Get activity class
    jclass activity_class = env->GetObjectClass(app->activity->clazz);

    // Get getExternalFilesDir method
    jmethodID get_external_files_dir =
        env->GetMethodID(activity_class, "getExternalFilesDir", "(Ljava/lang/String;)Ljava/io/File;");

    // Call getExternalFilesDir(null)
    jobject file_obj = env->CallObjectMethod(app->activity->clazz, get_external_files_dir, nullptr);

    if (!file_obj) {
        env->DeleteLocalRef(activity_class);
        return "";
    }

    // Get File.getAbsolutePath()
    jclass    file_class = env->GetObjectClass(file_obj);
    jmethodID get_path   = env->GetMethodID(file_class, "getAbsolutePath", "()Ljava/lang/String;");
    jstring   path_str   = (jstring) env->CallObjectMethod(file_obj, get_path);

    const char* path_chars = env->GetStringUTFChars(path_str, nullptr);
    std::string result(path_chars);

    env->ReleaseStringUTFChars(path_str, path_chars);
    env->DeleteLocalRef(path_str);
    env->DeleteLocalRef(file_class);
    env->DeleteLocalRef(file_obj);
    env->DeleteLocalRef(activity_class);

    return result;
}

} // namespace ILLIXR