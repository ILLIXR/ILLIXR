#pragma once

#undef Success // For 'Success' conflict
#include <eigen3/Eigen/Dense>
#include <algorithm>
#include <string>
#include <map>
#include <GL/gl.h>
#include <utility>
#include <type_traits>
//#undef Complex // For 'Complex' conflict

#include "relative_clock.hpp"
#include "switchboard.hpp"

// Tell gldemo and timewarp_gl to use two texture handle for left and right eye
#define USE_ALT_EYE_FORMAT

namespace ILLIXR {

using ullong = unsigned long long;
struct point_with_units;

namespace units {
    enum eyes : int { LEFT_EYE = 0, RIGHT_EYE = 1 };

    enum measurement_unit : int { MILLIMETER = 0, CENTIMETER = 1, METER = 2, INCH = 3, FOOT = 4, PERCENT = 5, PIXEL = 6, UNSET = 7 };

    const std::map<measurement_unit, const std::string> unit_str{{MILLIMETER, "mm"},
                                                                 {CENTIMETER, "cm"},
                                                                 {METER, "m"},
                                                                 {INCH, "in"},
                                                                 {FOOT, "ft"},
                                                                 {PERCENT, "%"},
                                                                 {PIXEL, "px"},
                                                                 {UNSET, "unitless"}
    };
    constexpr int last_convertable_unit = FOOT;
    //                                          mm          cm          m            ft                 in
    constexpr float conversion_factor[5][5] = {{1.,         0.1,        .001,        1./(25.4 * 12.),   1./25.4},   // mm
                                               {10.,        1.,         .01,         1./(2.54 * 12.),   1./2.54},   // cm
                                               {1000.,      100.,       1.,          100./(2.54 * 12.), 100./2.54}, // m
                                               {12. * 25.4, 12. * 2.54, 12. * .0254, 1.,                12.},       // ft
                                               {25.4,       2.54,       .0254,       1./12.,            1.}         // in
    };

    inline float convert(const int from, const int to, float val) {
        return conversion_factor[from][to] * val;
    }

    inline eyes non_primary(eyes eye) {
        if (eye == LEFT_EYE)
            return RIGHT_EYE;
        return LEFT_EYE;
    }

}

namespace coordinates {
    enum frame {
        IMAGE,
        LEFT_HANDED_Y_UP,
        LEFT_HANDED_Z_UP,
        RIGHT_HANDED_Y_UP,// XR_REFERENCE_SPACE_TYPE_VIEW
        RIGHT_HANDED_Z_UP,
        RIGHT_HANDED_Z_UP_X_FWD
    };

    struct reference_frame : switchboard::event {
        time_point time;
        const frame reference;

        reference_frame(time_point time_, frame ref)
        : time{time_}
        , reference{ref} {}
    };
    enum reference_space {
        VIEWER,
        WORLD,
        ROOM = WORLD
    };

    /*
     * Rotation matrix for a point tp rotate its axes to convert it to a new coordinate system
     *
     */
    inline Eigen::Matrix3f rotation(const float alpha, const float beta, const float gamma) {
        Eigen::Matrix3f rot;
        double ra = alpha * M_PI / 180.;
        double rb = beta * M_PI / 180.;
        double rg = gamma * M_PI / 180;
        rot << (cos(rg)*cos(rb)), (cos(rg)*sin(rb)*sin(ra) - sin(rg)*cos(ra)), (cos(rg)*sin(rb)*cos(ra) + sin(rg)*sin(ra)),
                (sin(rg)*cos(rb)), (sin(rg)*sin(rb)*sin(ra) + cos(rg)*cos(ra)), (sin(rg)*sin(rb)*cos(ra) - cos(rg)*sin(ra)),
                -sin(rb), (cos(rb)*sin(ra)), (cos(rb)*cos(ra));
        return rot;
    }
    const Eigen::Matrix3f invert_x = (Eigen::Matrix3f() << -1., 0., 0., 0., 1., 0., 0., 0., 1.).finished();
    const Eigen::Matrix3f invert_y = (Eigen::Matrix3f() << 1., 0., 0., 0., -1., 0., 0., 0., 1.).finished();
    const Eigen::Matrix3f invert_z = (Eigen::Matrix3f() << 1., 0., 0., 0., 1., 0., 0., 0., -1.).finished();
    const Eigen::Matrix3f identity = Eigen::Matrix3f::Identity();

    //                                 from:      IM                                LHYU                               RHYU                               RHZU                               LHZU                                RHZUXF                                             to:
    const Eigen::Matrix3f conversion[6][6] = {{identity,                           invert_y,                          rotation(180., 0., 0.),            rotation(-90., 0., 0.),            rotation(-90., 0., 90.) * invert_z, rotation(-90., 0., -90.)},                       // IM
                                              {invert_y,                           identity,                          invert_z,                          rotation(-90., 0., 0.) * invert_y, rotation(90., 0., 90.),             rotation(-90., 0., -90.) * invert_y},            // LHYU
                                              {rotation(180., 0., 0.),             invert_z,                          identity,                          rotation(90., 0., 0.),             rotation(90., 0., 90.) * invert_z,  rotation(-90., 0., 90.) * invert_x * invert_y},  // RHYU
                                              {rotation(90., 0., 0.),              invert_y * rotation(90.,0.,0.),    rotation(-90., 0., 0.),            identity,                          rotation(0., 0., 90.) * invert_y,   rotation(0., 0., -90.)},                         // RHZU
                                              {rotation(90., -90., 0.) * invert_y, rotation(-90.,-90.,0.),            rotation(0., 90., 90.) * invert_y, invert_x * rotation(0, 0.,90),     identity,                           invert_y},                                       // LHZU
                                              {rotation(90., -90., 0.),            rotation(-90.,-90.,0.) * invert_y, rotation(0., 90., 90.),            rotation(0, 0.,90),                invert_y,                           identity}};                                      // RHZUXF

}

struct imu_type : switchboard::event {
    time_point      time;
    Eigen::Vector3d angular_v;
    Eigen::Vector3d linear_a;

    imu_type(time_point time_, Eigen::Vector3d angular_v_, Eigen::Vector3d linear_a_)
        : time{time_}
        , angular_v{std::move(angular_v_)}
        , linear_a{std::move(linear_a_)} { }
};

struct connection_signal : public switchboard::event {
    bool start;

    connection_signal(bool start_)
        : start{start_} { }
};

// Values needed to initialize the IMU integrator
typedef struct {
    double                      gyro_noise;
    double                      acc_noise;
    double                      gyro_walk;
    double                      acc_walk;
    Eigen::Matrix<double, 3, 1> n_gravity;
    double                      imu_integration_sigma;
    double                      nominal_rate;
} imu_params;

// IMU biases, initialization params, and slow pose needed by the IMU integrator
struct imu_integrator_input : public switchboard::event {
    time_point last_cam_integration_time;
    duration   t_offset;
    imu_params params;

    Eigen::Vector3d             biasAcc;
    Eigen::Vector3d             biasGyro;
    Eigen::Matrix<double, 3, 1> position;
    Eigen::Matrix<double, 3, 1> velocity;
    Eigen::Quaterniond          quat;

    imu_integrator_input(time_point last_cam_integration_time_, duration t_offset_, imu_params params_,
                         Eigen::Vector3d biasAcc_, Eigen::Vector3d biasGyro_, Eigen::Matrix<double, 3, 1> position_,
                         Eigen::Matrix<double, 3, 1> velocity_, Eigen::Quaterniond quat_)
        : last_cam_integration_time{last_cam_integration_time_}
        , t_offset{t_offset_}
        , params{std::move(params_)}
        , biasAcc{std::move(biasAcc_)}
        , biasGyro{std::move(biasGyro_)}
        , position{std::move(position_)}
        , velocity{std::move(velocity_)}
        , quat{std::move(quat_)} { }
};

// Output of the IMU integrator to be used by pose prediction
struct imu_raw_type : public switchboard::event {
    // Biases from the last two IMU integration iterations used by RK4 for pose predict
    Eigen::Matrix<double, 3, 1> w_hat;
    Eigen::Matrix<double, 3, 1> a_hat;
    Eigen::Matrix<double, 3, 1> w_hat2;
    Eigen::Matrix<double, 3, 1> a_hat2;

    // Faster pose propagated forwards by the IMU integrator
    Eigen::Matrix<double, 3, 1> pos;
    Eigen::Matrix<double, 3, 1> vel;
    Eigen::Quaterniond          quat;
    time_point                  imu_time;

    imu_raw_type(Eigen::Matrix<double, 3, 1> w_hat_, Eigen::Matrix<double, 3, 1> a_hat_, Eigen::Matrix<double, 3, 1> w_hat2_,
                 Eigen::Matrix<double, 3, 1> a_hat2_, Eigen::Matrix<double, 3, 1> pos_, Eigen::Matrix<double, 3, 1> vel_,
                 Eigen::Quaterniond quat_, time_point imu_time_)
        : w_hat{std::move(w_hat_)}
        , a_hat{std::move(a_hat_)}
        , w_hat2{std::move(w_hat2_)}
        , a_hat2{std::move(a_hat2_)}
        , pos{std::move(pos_)}
        , vel{std::move(vel_)}
        , quat{std::move(quat_)}
        , imu_time{imu_time_} { }
};

struct pose_data {
    Eigen::Vector3f    position;
    Eigen::Quaternionf orientation;
    float confidence;
    units::measurement_unit unit;
    coordinates::frame co_frame;
    coordinates::reference_space ref_space;
    bool valid;

    pose_data()
    : position{0., 0., 0.}
    , orientation{1., 0., 0., 0.}
    , confidence{0}
    , unit{units::UNSET}
    , co_frame{coordinates::RIGHT_HANDED_Y_UP}
    , ref_space{coordinates::VIEWER}
    , valid{false} {}

    pose_data(Eigen::Vector3f position_, Eigen::Quaternionf orientation_, units::measurement_unit unit_ = units::UNSET,
              coordinates::frame frm = coordinates::RIGHT_HANDED_Y_UP,
              coordinates::reference_space ref = coordinates::VIEWER,
              const float confidence_ = 0.,
              bool valid_ = true)
        : position{std::move(position_)}
        , orientation{std::move(orientation_)}
        , confidence(confidence_)
        , unit{unit_}
        , co_frame{frm}
        , ref_space{ref}
        , valid{valid_} {}
};

struct pose_type : public switchboard::event, public pose_data {
    time_point sensor_time; // Recorded time of sensor data ingestion

    pose_type()
        : pose_data()
        , sensor_time{time_point{}} {}

    pose_type(time_point sensor_time_, Eigen::Vector3f& position_, Eigen::Quaternionf& orientation_,
              units::measurement_unit unit_ = units::UNSET, coordinates::frame frm = coordinates::RIGHT_HANDED_Y_UP,
              coordinates::reference_space ref = coordinates::VIEWER,const float confidence_ = 0.)
        : pose_data{position_, orientation_, unit_, frm, ref, confidence_}
        , sensor_time{sensor_time_} {}

    pose_type(time_point sensor_time_, const Eigen::Vector3f position_, const Eigen::Quaternionf orientation_,
              units::measurement_unit unit_ = units::UNSET, coordinates::frame frm = coordinates::RIGHT_HANDED_Y_UP,
              coordinates::reference_space ref = coordinates::VIEWER, const float confidence_ = 0.)
        : pose_data{std::move(position_), std::move(orientation_), unit_, frm, ref, confidence_}
        , sensor_time{sensor_time_} {}
};

typedef struct {
    pose_type  pose;
    time_point predict_computed_time; // Time at which the prediction was computed
    time_point predict_target_time;   // Time that prediction targeted.
} fast_pose_type;

// Used to identify which graphics API is being used (for swapchain construction)
enum class graphics_api { OPENGL, VULKAN, TBD };

// Used to distinguish between different image handles
enum class swapchain_usage { LEFT_SWAPCHAIN, RIGHT_SWAPCHAIN, LEFT_RENDER, RIGHT_RENDER, NA };

typedef struct vk_image_handle {
    int      file_descriptor;
    int64_t  format;
    size_t   allocation_size;
    uint32_t width;
    uint32_t height;

    vk_image_handle(int fd_, int64_t format_, size_t alloc_size, uint32_t width_, uint32_t height_)
        : file_descriptor{fd_}
        , format{format_}
        , allocation_size{alloc_size}
        , width{width_}
        , height{height_} { }
} vk_image_handle;

// This is used to share swapchain images between ILLIXR and Monado.
// When Monado uses its GL pipeline, it's enough to just share a context during creation.
// Otherwise, file descriptors are needed to share the images.
struct image_handle : public switchboard::event {
    graphics_api type;

    union {
        GLuint          gl_handle;
        vk_image_handle vk_handle;
    };

    uint32_t        num_images;
    swapchain_usage usage;

    image_handle()
        : type{graphics_api::TBD}
        , gl_handle{0}
        , num_images{0}
        , usage{swapchain_usage::NA} { }

    image_handle(GLuint gl_handle_, uint32_t num_images_, swapchain_usage usage_)
        : type{graphics_api::OPENGL}
        , gl_handle{gl_handle_}
        , num_images{num_images_}
        , usage{usage_} { }

    image_handle(int vk_fd_, int64_t format, size_t alloc_size, uint32_t width_, uint32_t height_, uint32_t num_images_,
                 swapchain_usage usage_)
        : type{graphics_api::VULKAN}
        , vk_handle{vk_fd_, format, alloc_size, width_, height_}
        , num_images{num_images_}
        , usage{usage_} { }
};

// Using arrays as a swapchain
// Array of left eyes, array of right eyes
// This more closely matches the format used by Monado
struct rendered_frame : public switchboard::event {
    std::array<GLuint, 2> swapchain_indices{}; // Does not change between swaps in swapchain
    std::array<GLuint, 2> swap_indices{};      // Which element of the swapchain
    fast_pose_type        render_pose;         // The pose used when rendering this frame.
    time_point            sample_time{};
    time_point            render_time{};

    rendered_frame() = default;

    rendered_frame(std::array<GLuint, 2>&& swapchain_indices_, std::array<GLuint, 2>&& swap_indices_,
                   fast_pose_type render_pose_, time_point sample_time_, time_point render_time_)
        : swapchain_indices{swapchain_indices_}
        , swap_indices{swap_indices_}
        , render_pose(std::move(render_pose_))
        , sample_time(sample_time_)
        , render_time(render_time_) { }
};

struct hologram_input : public switchboard::event {
    uint seq{};

    hologram_input() = default;

    explicit hologram_input(uint seq_)
        : seq{seq_} { }
};

struct signal_to_quad : public switchboard::event {
    ullong seq;

    signal_to_quad(ullong seq_)
        : seq{seq_} { }
};

// High-level HMD specification, timewarp plugin
// may/will calculate additional HMD info based on these specifications
struct hmd_physical_info {
    float ipd;
    int   displayPixelsWide;
    int   displayPixelsHigh;
    float chromaticAberration[4];
    float K[11];
    int   visiblePixelsWide;
    int   visiblePixelsHigh;
    float visibleMetersWide;
    float visibleMetersHigh;
    float lensSeparationInMeters;
    float metersPerTanAngleAtCenter;
};

struct texture_pose : public switchboard::event {
    duration           offload_duration{};
    unsigned char*     image{};
    time_point         pose_time{};
    Eigen::Vector3f    position;
    Eigen::Quaternionf latest_quaternion;
    Eigen::Quaternionf render_quaternion;

    texture_pose() = default;

    texture_pose(duration offload_duration_, unsigned char* image_, time_point pose_time_, Eigen::Vector3f position_,
                 Eigen::Quaternionf latest_quaternion_, Eigen::Quaternionf render_quaternion_)
        : offload_duration{offload_duration_}
        , image{image_}
        , pose_time{pose_time_}
        , position{std::move(position_)}
        , latest_quaternion{std::move(latest_quaternion_)}
        , render_quaternion{std::move(render_quaternion_)} { }
};

inline bool compare(const std::string& input, const std::string& val) {
    std::string v1, v2;
    std::transform(input.begin(), input.end(), v1.begin(), ::tolower);
    std::transform(val.begin(), val.end(), v2.begin(), ::tolower);
    return v1 == v2;
}

typedef std::map<units::eyes, pose_type> multi_pose_map;

/**
 * Struct which defines a representation of a rectangle
 */
struct rect {
    double x_center; //!< x-coordinate of the rectangle's center
    double y_center; //!< y-coordinate of the rectangle's center
    double width;    //!< width of the rectangle (parallel to x-axis when rotation angle is 0)
    double height;   //!< height of the rectangle (parallel to y-axis when rotation angle is 0)

    double                  rotation; //!< rotation angle of the rectangle in radians
    units::measurement_unit unit;
    bool                    valid;    //!< if the rectangle is valid

    /**
     * Generic constructor which sets all values to 0
     */
    rect()
        : x_center{0.}
        , y_center{0.}
        , width{0.}
        , height{0.}
        , rotation{0.}
        , unit{units::UNSET}
        , valid{false} { }

    /**
     * Copy constructor
     * @param other The rect to copy
     */
    explicit rect(rect* other) {
        if (other != nullptr) {
            x_center = other->x_center;
            y_center = other->y_center;
            width    = other->width;
            height   = other->height;
            rotation = other->rotation;
            unit     = other->unit;
            valid    = other->valid;
        }
    }

    /**
     * General constructor
     * @param xc the x-coordinate
     * @param yc the y-coordinate
     * @param w the width
     * @param h the height
     * @param r rotation angle
     */
    rect(const double xc, const double yc, const double w, const double h, const double r,
         units::measurement_unit unit_ = units::UNSET)
        : x_center{xc}
        , y_center{yc}
        , width{w}
        , height{h}
        , rotation{r}
        , unit{unit_}
        , valid{true} { }

    /**
     * Set the rect's values after construction
     * @param xc the x-coordinate
     * @param yc the y-coordinate
     * @param w the width
     * @param h the height
     * @param r rotation angle
     */
    void set(const double xc, const double yc, const double w, const double h, const double r,
             units::measurement_unit unit_ = units::UNSET) {
        x_center = xc;
        y_center = yc;
        width    = w;
        height   = h;

        rotation = r;
        unit     = unit_;
        valid    = true;
    }

    void flip_y() {
        if (unit == units::PERCENT)
            y_center = 1.0 - y_center;
        else
            throw std::runtime_error("Cannot rectify rect with non percent units");
    }
};


//**********************************************************************************
//  Points
//**********************************************************************************

struct point : Eigen::Vector3f {
    point() : Eigen::Vector3f{0., 0. ,0.} {}
    point(const float x, const float y, const float z = 0.) : Eigen::Vector3f(x, y, z) {}
    void set(const float x_, const float y_, const float z_ = 0.) {
        x() = x_;
        y() = y_;
        z() = z_;
    }

    point& operator=(const Eigen::Vector3f& other) {
        x() = other.x();
        y() = other.y();
        z() = other.z();
        return *this;
    }

    template<typename T, typename U, int Option>
    point& operator=(const Eigen::Product<T, U, Option>& pr) {
        x() = pr.x();
        y() = pr.y();
        z() = pr.z();
        return *this;
    }

    point& operator+=(Eigen::Vector3f& other) {
        x() += other.x();
        y() += other.y();
        z() += other.z();
        return *this;
    }

    point& operator-=(Eigen::Vector3f& other) {
        x() -= other.x();
        y() -= other.y();
        z() -= other.z();
        return *this;
    }
};

struct point_with_validity : point {
    bool valid = false;
    float confidence = 0.;

    point_with_validity() : point(), valid{false} {}
    point_with_validity(const float x, const float y, const float z, bool valid_ = true, const float confidence_ = 0.)
        : point{x, y, z}
        , valid{valid_}
        , confidence{confidence_} {}
    point_with_validity(const point& pnt, bool valid_ = true, const float confidence_ = 0.)
        : point{pnt}
        , valid{valid_}
        , confidence{confidence_} {}
};

struct point_with_units : point_with_validity {
    units::measurement_unit unit;

    explicit point_with_units(units::measurement_unit unit_ = units::UNSET)
        : point_with_validity()
        , unit{unit_} {}

    point_with_units(const float x, const float y, const float z, units::measurement_unit unit_ = units::UNSET, bool valid_ = true,
                     const float confidence_ = 0.)
        : point_with_validity{x, y, z, valid_, confidence_}
        , unit{unit_} {}

    point_with_units(const point& pnt, units::measurement_unit unit_ = units::UNSET, bool valid_ = true, const float confidence_ = 0.)
        : point_with_validity{pnt, valid_, confidence_}
        , unit{unit_} {}

    explicit point_with_units(const point_with_validity& pnt, units::measurement_unit unit_ = units::UNSET)
        : point_with_validity{pnt}
        , unit{unit_} {}

    point_with_units operator+(const point_with_units& pnt) const {
        point_with_units p_out;
        p_out.x() = x() + pnt.x();
        p_out.y() = y() + pnt.y();
        p_out.z() = z() + pnt.z();
        p_out.unit = unit;
        p_out.valid = valid && pnt.valid;
        return p_out;
    }

    point_with_units operator-(const point_with_units& pnt) const {
        point_with_units p_out;
        p_out.x() = x() - pnt.x();
        p_out.y() = y() - pnt.y();
        p_out.z() = z() - pnt.z();
        p_out.unit = unit;
        p_out.valid = valid && pnt.valid;
        return p_out;
    }

    point_with_units operator+(const Eigen::Vector3f& pnt) const {
        point_with_units p_out;
        p_out.x() = x() + pnt.x();
        p_out.y() = y() + pnt.y();
        p_out.z() = z() + pnt.z();
        p_out.unit = unit;
        p_out.valid = valid;
        return p_out;
    }

    point_with_units operator-(const Eigen::Vector3f& pnt) const {
        point_with_units p_out;
        p_out.x() = x() - pnt.x();
        p_out.y() = y() - pnt.y();
        p_out.z() = z() - pnt.z();
        p_out.unit = unit;
        p_out.valid = valid;
        return p_out;
    }


    point_with_units operator*(const float val) const {
        point_with_units p_out;
        p_out.x() *= val;
        p_out.y() *= val;
        p_out.z() *= val;
        p_out.unit = unit;
        p_out.valid = valid;
        return p_out;
    }

    point_with_units operator/(const float val) const {
        point_with_units p_out;
        p_out.x() /= val;
        p_out.y() /= val;
        p_out.z() /= val;
        p_out.unit = unit;
        p_out.valid = valid;
        return p_out;
    }

    void set(const float x_, const float y_, const float z_, units::measurement_unit unit_, bool valid_ = true) {
        x() = x_;
        y() = y_;
        z() = z_;
        unit = unit_;
        valid = valid_;
    }

    void set(const Eigen::Vector3f& vec) {
        x() = vec.x();
        y() = vec.y();
        z() = vec.z();
    }
};

inline point abs(const point& pnt) {
    return {std::abs(pnt.x()), std::abs(pnt.y()), std::abs(pnt.z())};
}

inline point_with_validity abs(const point_with_validity& pnt) {
    return {abs(point(pnt.x(), pnt.y(), pnt.z())), pnt.valid, pnt.confidence};
}

/**
 * Determine the absolute value of a point (done on each coordinate)
 * @param value The point to take the absolute value of
 * @return A point containing the result
 */
inline point_with_units abs(const point_with_units& pnt) {
    return {abs(point(pnt.x(), pnt.y(), pnt.z())), pnt.unit, pnt.valid, pnt.confidence};
}

/*
 * Normalize the coordinates, using the input size as reference
 */
template<typename T>
void normalize(T& obj, const float width, const float height, const float depth) {
    if (obj.unit == units::PERCENT) {
        std::cout << "Already normalized" << std::endl;
        return;
    }
    obj.x() /= width;
    obj.y() /= height;
    obj.z() /= depth;
    obj.unit = units::PERCENT;

}

template<typename T>
void normalize(T& obj, const float width, const float height) {
    normalize<T>(obj, width, height, 1.);
}

template<typename T>
void denormalize(T& obj, const float width, const float height, const float depth, units::measurement_unit unit_ = units::PIXEL) {
    if (obj.unit != units::PERCENT){
        std::cout << "Already denormalized" << std::endl;
        return;
    }
    if (unit_ == units::PERCENT)
        throw std::runtime_error("Cannot denormalize to PERCENT");

    obj.x() *= width;
    obj.y() *= height;
    obj.z() *= depth;
    obj.unit = unit_;
}

template<typename T>
void denormalize(T& obj, const float width, const float height, units::measurement_unit unit_ = units::PIXEL) {
    denormalize<T>(obj, width, height, 1., unit_);
}

template<>
void normalize<rect>(rect& obj, const float width, const float height, const float depth) {
    (void) depth;
    if (obj.unit == units::PERCENT) {
        std::cout << "Rect is already normalized" << std::endl;
        return;
    }
    obj.x_center /= width;
    obj.y_center /= height;
    obj.width /= width;
    obj.height /= height;
    obj.unit = units::PERCENT;
}

template<>
void denormalize<rect>(rect& obj, const float width, const float height, const float depth, units::measurement_unit unit) {
    (void)depth;
    if (obj.unit != units::PERCENT) {
        std::cout << "Rect is already denormalized" << std::endl;
        return;
    }
    if (unit == units::PERCENT)
        throw std::runtime_error("Cannot denormalize to PERCENT");
    obj.x_center *= width;
    obj.y_center *= height;
    obj.width *= width;
    obj.height *= height;
    obj.unit = unit;
}


struct points_with_units {
    std::vector<point_with_units> points;
    units::measurement_unit unit;
    bool valid;
    bool fixed = false;

    explicit points_with_units(units::measurement_unit unit_ = units::UNSET)
        : points{std::vector<point_with_units>()}
        , unit{unit_}, valid{false} {}
    explicit points_with_units(const int size, units::measurement_unit unit_ = units::UNSET)
        : points{std::vector<point_with_units>(size, point_with_units(unit_))}
        , unit{unit_}
        , valid{false}
        , fixed{true} {}
    explicit points_with_units(std::vector<point_with_units> points_)
        : points{std::move(points_)} {
        if (!points.empty())
            unit = points[0].unit;
        valid = true;
        for (const auto& pnt : points)
            valid &= pnt.valid;
    }
    points_with_units(const points_with_units& points_)
        : points_with_units(points_.points) {}

    explicit points_with_units(std::vector<point_with_validity>& points_, units::measurement_unit unit_ = units::UNSET)
        : unit{unit_} {
        points.resize(points_.size());
        valid = true;
        for (size_t i = 0; i < points_.size(); i++) {
            points[i] = point_with_units(points_[i], unit_);
            valid &= points_[i].valid;
        }
    }
    explicit points_with_units(std::vector<point>& points_, units::measurement_unit unit_ = units::UNSET, bool valid_ = true)
        : unit{unit_}
        , valid{valid_} {
        points.resize(points_.size());
        for (size_t i = 0; i < points_.size(); i++)
            points[i] = point_with_units(points_[i], unit_, valid_);
    }

    point_with_units& operator[](const size_t idx) {
        if (fixed)
            return points.at(idx);
        return points[idx];
    }
    point_with_units& at(const size_t idx) {
        return points.at(idx);
    }

    [[nodiscard]] const point_with_units& at(const size_t idx) const {
        return points.at(idx);
    }

    [[nodiscard]] size_t size() const {
        return points.size();
    }

    void mult(const Eigen::Matrix3f& ref_frm) {
        for (point& pnt : points)
            pnt = ref_frm * pnt;
    }

    void transform(const pose_data& pose) {
        for (point& pnt : points)
            pnt = (Eigen::Vector3f)((pose.orientation * pnt) + pose.position);
    }
};

template<>
void normalize<points_with_units>(points_with_units& obj, const float width, const float height, const float depth) {
    if (obj.unit == units::PERCENT) {
        std::cout << "Points are already normalized";
        return;
    }
    for (auto& pnt : obj.points)
        ::ILLIXR::normalize(pnt, width, height, depth);
    obj.unit = units::PERCENT;
}

template<>
void denormalize<points_with_units>(points_with_units& obj, const float width, const float height, const float depth,
                                    units::measurement_unit unit_) {
    for (auto& pnt : obj.points)
        ::ILLIXR::denormalize(pnt, width, height, depth, unit_);
    obj.unit = unit_;
}

} // namespace ILLIXR
