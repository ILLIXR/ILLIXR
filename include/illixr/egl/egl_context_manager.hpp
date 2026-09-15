#pragma once
#ifdef __ANDROID__
/// Problem: EGL contexts are thread-local. Only one thread can have a context current.
/// Solution: Create multiple contexts that SHARE resources (textures, buffers, etc.)
///
/// Architecture:
///   - Primary context: Created first, owned by OpenXR/render thread
///   - Secondary context(s): Created with share_context = primary, owned by other threads
///   - Resources created in one context are visible in all shared contexts

#    include <EGL/egl.h>
#    include <EGL/eglext.h>
#    include <mutex>
#    include <thread>

namespace ILLIXR {

/// Singleton manager for EGL contexts across threads
class egl_context_manager {
public:
    static egl_context_manager& instance();

    /// Register the primary EGL context (call from OpenXR plugin after creating context)
    /// @param display EGL display
    /// @param context Primary EGL context (the one OpenXR uses)
    /// @param config EGL config used to create the context
    void register_primary_context(EGLDisplay display, EGLContext context, EGLConfig config);

    /// Release the primary context from the current thread.
    /// Call this after register_primary_context() and before the threadloop starts,
    /// so that the threadloop's thread can acquire it.
    void release_primary_context_from_current_thread();

    /// Get or create an EGL context for the current thread.
    /// If called from the primary thread, returns the primary context.
    /// If called from another thread, creates a new shared context.
    /// @return EGL context for this thread, or EGL_NO_CONTEXT on failure
    EGLContext get_context_for_current_thread();

    /// Make the appropriate context current for the current thread.
    /// Creates a shared context if needed.
    /// @return true if a context is now current
    bool make_current_for_thread();

    /// Release the current context (call before thread exits if it's not the primary thread)
    void release_current_thread_context();

    /// Wait for the context manager to be initialized
    /// @param timeout_ms Maximum time to wait in milliseconds
    /// @return true if initialized, false if timeout
    bool wait_for_initialization(int timeout_ms = 5000);

    /// Get the EGL display
    EGLDisplay get_display() const {
        return display_;
    }

    /// Get the EGL config
    EGLConfig get_config() const {
        return config_;
    }

    /// Get the primary context (for reference, don't use from other threads)
    EGLContext get_primary_context() const {
        return primary_context_;
    }

    /// Check if primary context is registered
    bool is_initialized() const {
        return primary_context_ != EGL_NO_CONTEXT;
    }

    EGLContext create_shared_context();

private:
    egl_context_manager() = default;

    ~egl_context_manager() {
        // Note: Don't destroy contexts here - they should be cleaned up by their owning threads
    }

    // Non-copyable
    egl_context_manager(const egl_context_manager&)            = delete;
    egl_context_manager& operator=(const egl_context_manager&) = delete;

    std::mutex                                      mutex_;
    EGLDisplay                                      display_{EGL_NO_DISPLAY};
    EGLContext                                      primary_context_{EGL_NO_CONTEXT};
    EGLConfig                                       config_{nullptr};
    std::thread::id                                 primary_thread_id_;
    std::unordered_map<std::thread::id, EGLContext> thread_contexts_;

    std::condition_variable init_cv_;
    std::atomic<bool>       initialized_{false};
    bool                    primary_context_assigned_{false};
};

/// RAII helper to ensure GL context is current for the current thread
class gl_context_guard {
public:
    gl_context_guard()
        : success_(false) {
        success_ = egl_context_manager::instance().make_current_for_thread();
        if (!success_) {
            throw std::runtime_error("gl_context_guard: Failed to make context current");
        }
    }

    ~gl_context_guard() {
        // Don't release - context stays current for thread
    }

    bool is_valid() const {
        return success_;
    }

    explicit operator bool() const {
        return success_;
    }

private:
    bool success_;
};

} // namespace ILLIXR

#endif
