#pragma once

#ifdef __ANDROID__
#include <android_native_app_glue.h>
#include <jni.h>
#include <spdlog/spdlog.h>

namespace ILLIXR {

// Get JNI environment for the current thread.
// Attaches the thread to the JVM if not already attached.
inline JNIEnv* get_jni_env(android_app* app) {
    if (!app || !app->activity || !app->activity->vm) {
        spdlog::get("illixr")->error("[get_jni_env] Invalid android_app");
        return nullptr;
    }

    JavaVM* vm  = app->activity->vm;
    JNIEnv* env = nullptr;

    // Check if current thread is already attached
    jint result = vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);

    if (result == JNI_OK) {
        return env; // Already attached
    }

    if (result == JNI_EDETACHED) {
        // Need to attach this thread
        JavaVMAttachArgs args;
        args.version = JNI_VERSION_1_6;
        args.name    = "ILLIXR_Native";
        args.group   = nullptr;

        result = vm->AttachCurrentThread(&env, &args);
        if (result != JNI_OK) {
            spdlog::get("illixr")->error("[get_jni_env] AttachCurrentThread failed: {}", result);
            return nullptr;
        }

        spdlog::get("illixr")->debug("[get_jni_env] Attached thread to JVM");
        return env;
    }

    spdlog::get("illixr")->error("[get_jni_env] GetEnv failed: {}", result);
    return nullptr;
}

// RAII helper to attach/detach JNI on a thread
class jni_scope {
public:
    explicit jni_scope(android_app* app)
        : vm_(nullptr)
        , env_(nullptr)
        , did_attach_(false) {
        if (!app || !app->activity || !app->activity->vm) {
            return;
        }

        vm_         = app->activity->vm;
        jint result = vm_->GetEnv(reinterpret_cast<void**>(&env_), JNI_VERSION_1_6);

        if (result == JNI_EDETACHED) {
            JavaVMAttachArgs args;
            args.version = JNI_VERSION_1_6;
            args.name    = "ILLIXR_Temp";
            args.group   = nullptr;

            if (vm_->AttachCurrentThread(&env_, &args) == JNI_OK) {
                did_attach_ = true;
            }
        }
    }

    ~jni_scope() {
        if (did_attach_ && vm_) {
            vm_->DetachCurrentThread();
        }
    }

    // Non-copyable
    jni_scope(const jni_scope&)            = delete;
    jni_scope& operator=(const jni_scope&) = delete;

    [[nodiscard]] JNIEnv* env() const { return env_; }
    [[nodiscard]] bool is_valid() const { return env_ != nullptr; }
    explicit operator bool() const { return is_valid(); }

private:
    JavaVM* vm_;
    JNIEnv* env_;
    bool    did_attach_;
};

} // namespace ILLIXR
#endif // __ANDROID__
