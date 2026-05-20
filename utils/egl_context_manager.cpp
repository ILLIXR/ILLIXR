#include "illixr/egl/egl_context_manager.hpp"

#include <spdlog/spdlog.h>

using namespace ILLIXR;

egl_context_manager& egl_context_manager::instance() {
    static egl_context_manager mgr;
    return mgr;
}


void egl_context_manager::register_primary_context(EGLDisplay display, EGLContext context, EGLConfig config) {
    std::lock_guard<std::mutex> lock(mutex_);

    display_ = display;
    primary_context_ = context;
    config_ = config;

    spdlog::get("illixr")->info("egl_context_manager: Primary context registered: {}",
                                (void*)context);

    initialized_.store(true);
    init_cv_.notify_all();
}

void egl_context_manager::release_primary_context_from_current_thread() {
    if (eglGetCurrentContext() == primary_context_) {
        spdlog::get("illixr")->info("egl_context_manager: Releasing primary context from init thread");
        eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    }
}
EGLContext egl_context_manager::get_context_for_current_thread() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (primary_context_ == EGL_NO_CONTEXT) {
        spdlog::get("illixr")->error("egl_context_manager: No primary context registered");
        return EGL_NO_CONTEXT;
    }

    std::thread::id tid = std::this_thread::get_id();

    // Check if this thread already has a context assigned
    auto it = thread_contexts_.find(tid);
    if (it != thread_contexts_.end()) {
        return it->second;
    }

    // First thread to request gets the primary context (should be render thread)
    if (!primary_context_assigned_) {
        primary_context_assigned_ = true;
        primary_thread_id_ = tid;
        thread_contexts_[tid] = primary_context_;
        spdlog::get("illixr")->info("egl_context_manager: Assigned primary context to thread");
        return primary_context_;
    }

    // Other threads get a shared context
    EGLContext new_context = create_shared_context();
    if (new_context != EGL_NO_CONTEXT) {
        thread_contexts_[tid] = new_context;
        spdlog::get("illixr")->info("egl_context_manager: Created shared context {} for thread",
                                    (void*)new_context);
    }

    return new_context;
}

bool egl_context_manager::make_current_for_thread() {
    EGLContext ctx = get_context_for_current_thread();
    if (ctx == EGL_NO_CONTEXT) {
        return false;
    }

    // Check if already current
    EGLContext current = eglGetCurrentContext();
    if (current == ctx) {
        return true;
    }

    // If a different context is current, that's a problem
    if (current != EGL_NO_CONTEXT) {
        spdlog::get("illixr")->warn("egl_context_manager: Different context {} was current, expected {}",
                                    (void*)current, (void*)ctx);
    }

    // Make current with no surface (we'll render to FBOs/textures)
    if (!eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) {
        EGLint error = eglGetError();
        spdlog::get("illixr")->error("egl_context_manager: eglMakeCurrent failed: 0x{:X}", error);

        if (error == 0x3002) {  // EGL_BAD_ACCESS
            spdlog::get("illixr")->error("  -> Context is current on another thread!");
            spdlog::get("illixr")->error("  -> Make sure to call release_primary_context_from_current_thread()");
            spdlog::get("illixr")->error("  -> after registering and before threadloop starts");
        }
        return false;
    }

    return true;
}

bool egl_context_manager::wait_for_initialization(int timeout_ms) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (initialized_.load()) {
        return true;
    }
    return init_cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                             [this] { return initialized_.load(); });
}

void egl_context_manager::release_current_thread_context() {
    std::lock_guard<std::mutex> lock(mutex_);

    std::thread::id tid = std::this_thread::get_id();

    auto it = thread_contexts_.find(tid);
    if (it == thread_contexts_.end()) {
        return;
    }

    EGLContext ctx = it->second;

    // Unbind
    if (eglGetCurrentContext() == ctx) {
        eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    }

    // Don't destroy primary context
    if (ctx != primary_context_) {
        eglDestroyContext(display_, ctx);
        spdlog::get("illixr")->debug("egl_context_manager: Destroyed shared context");
    }

    thread_contexts_.erase(it);
}

EGLContext egl_context_manager::create_shared_context() {
    const EGLint context_attribs[] = {
            EGL_CONTEXT_CLIENT_VERSION, 3,
            EGL_NONE
    };

    // Create context that shares resources with primary context
    EGLContext shared = eglCreateContext(display_, config_, primary_context_, context_attribs);

    if (shared == EGL_NO_CONTEXT) {
        EGLint error = eglGetError();
        spdlog::get("illixr")->error("egl_context_manager: Failed to create shared context: 0x{:X}",
                                         error);
    }

    return shared;
}
