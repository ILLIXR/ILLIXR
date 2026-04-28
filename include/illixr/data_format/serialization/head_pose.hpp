#pragma once

#include "misc.hpp"
#include "illixr/data_format/poses/head_pose.hpp"
#include "illixr/data_format/serialization/pose_base.hpp"

// ---------------------------------------------------------------------------
// Minimal file logger — thread-safe, header-only, no spdlog dependency
// ---------------------------------------------------------------------------
/* namespace ILLIXR::detail {

inline FILE* ser_log_file() {
    static std::once_flag flag;
    static FILE*          fp = nullptr;
    std::call_once(flag, [] {
        fp = fopen(SER_LOG_FILENAME, "w");
        if (fp) {
            fprintf(fp, "=== ILLIXR serialization log | side=" SER_LOG_SIDE " ===\n");
            fflush(fp);
        }
    });
    return fp;
}

inline std::mutex& ser_log_mutex() {
    static std::mutex mtx;
    return mtx;
}

template<typename... Args>
inline void ser_log(const char* fmt, Args&&... args) {
    FILE* fp = ser_log_file();
    if (!fp)
        return;
    std::lock_guard<std::mutex> lk(ser_log_mutex());
    fprintf(fp, fmt, std::forward<Args>(args)...);
    fprintf(fp, "\n");
    fflush(fp);
}

} // namespace ILLIXR::detail

// Convenience macro so call sites are compact
#define SER_LOG(...) ILLIXR::detail::ser_log(__VA_ARGS__)
// Log only when saving
#define SER_LOG_SAVE(ar, ...)                        \
    do {                                             \
        if constexpr (!Archive::is_loading::value) { \
            SER_LOG(__VA_ARGS__);                    \
        }                                            \
    } while (0)
// Log only when loading
#define SER_LOG_LOAD(ar, ...)                       \
    do {                                            \
        if constexpr (Archive::is_loading::value) { \
            SER_LOG(__VA_ARGS__);                   \
        }                                           \
    } while (0)
// Log on both sides, prefixed with SAVE/LOAD
#define SER_LOG_BOTH(ar, fmt, ...)                                  \
    do {                                                            \
        if constexpr (Archive::is_loading::value) {                 \
            SER_LOG("[LOAD|" SER_LOG_SIDE "] " fmt, ##__VA_ARGS__); \
        } else {                                                    \
            SER_LOG("[SAVE|" SER_LOG_SIDE "] " fmt, ##__VA_ARGS__); \
        }                                                           \
    } while (0)
*/
// ---------------------------------------------------------------------------

namespace boost::serialization {

template<class Archive>
[[maybe_unused]] void serialize(Archive& ar, ILLIXR::data_format::pose::fast_head_pose_type& pose, const unsigned int version) {
    (void) version;
    ar& boost::serialization::base_object<ILLIXR::switchboard::event>(pose);
    ar & pose.pose;
    ar & pose.predict_computed_time;
    ar & pose.predict_target_time;
    //SER_LOG_BOTH(ar, "  fast_head_pose_type: computed_time=%lld target_time=%lld",
    //             static_cast<long long>(pose.predict_computed_time.time_since_epoch().count()),
    //             static_cast<long long>(pose.predict_target_time));
}


} // namespace boost::serialization

BOOST_CLASS_EXPORT_KEY(ILLIXR::data_format::pose::head_pose_type)
BOOST_CLASS_EXPORT_KEY(ILLIXR::data_format::pose::fast_head_pose_type)
