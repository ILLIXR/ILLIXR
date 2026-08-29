#include "plugin.hpp"

#include <Eigen/Core>
#include <Eigen/Geometry>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <spawn.h>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <vector>

#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace {

constexpr std::uint32_t kInputVersion = 1;
constexpr char          kInputMagic[8] = {'I', 'L', 'L', 'I', 'X', 'R', 'I', '1'};
constexpr char          kFrameMagic[8] = {'B', 'O', 'B', 'A', 'Q', 'I', 'M', '1'};
constexpr char          kOverlayMagic[8] = {'B', 'O', 'B', 'A', 'O', 'V', 'L', '1'};
constexpr char          kModalMagic[8] = {'B', 'O', 'B', 'A', 'M', 'O', 'D', '1'};

constexpr std::uint32_t kFlagActive             = 1U << 0U;
constexpr std::uint32_t kFlagPositionValid      = 1U << 1U;
constexpr std::uint32_t kFlagOrientationValid   = 1U << 2U;
constexpr std::uint32_t kFlagPositionTracked    = 1U << 3U;
constexpr std::uint32_t kFlagOrientationTracked = 1U << 4U;
constexpr std::uint32_t kFlagPressed            = 1U << 1U;
constexpr char          kBobaRuntimeEnvironment[] = "boba-cu132";

constexpr std::uint32_t kFrameVersion          = 3;
constexpr std::uint32_t kOverlayVersion        = 2;
constexpr std::uint32_t kModalVersion          = 1;
constexpr std::uint32_t kEyeCount              = 2;
constexpr std::uint32_t kChannelCount          = 4;
constexpr std::uint32_t kOverlayCommandFloats  = 14;
constexpr std::uint32_t kModalVisibleFlag      = 1U << 0U;
constexpr std::uint32_t kModalLeftValidFlag    = 1U << 1U;
constexpr std::uint32_t kModalRightValidFlag   = 1U << 2U;

#pragma pack(push, 1)
struct InputHeader {
    char          magic[8];
    std::uint32_t version;
    std::uint32_t byte_count;
    std::uint64_t sequence;
    std::int64_t  xr_sample_time;
};

struct InputEye {
    std::uint32_t flags;
    std::uint32_t recommended_width;
    std::uint32_t recommended_height;
    float         position[3];
    float         orientation[4];
    float         fov[4];
};

struct InputPose {
    std::uint32_t flags;
    float         position[3];
    float         orientation[4];
};

struct InputButton {
    std::uint32_t flags;
    float         value;
};

struct InputAxis {
    std::uint32_t flags;
    float         x;
    float         y;
};

struct InputController {
    std::uint32_t available_flags;
    std::uint32_t interaction_profile;
    InputPose     grip;
    InputPose     aim;
    InputButton   trigger;
    InputButton   squeeze;
    InputButton   primary;
    InputButton   secondary;
    InputButton   thumbstick_click;
    InputAxis     thumbstick;
};

struct InputPacket {
    InputHeader     header;
    InputEye        eyes[2];
    InputController controllers[2];
};

struct SharedFrameHeader {
    char          magic[8];
    std::uint32_t version;
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t channels;
    std::uint32_t frame_bytes;
    std::uint32_t slot_count;
    std::uint64_t latest_frame_id;
    std::uint64_t latest_slot;
    std::uint32_t presentation_mode;
    std::uint32_t metadata_bytes;
    std::uint8_t  padding[8];
};

struct SharedFramePoseMetadataSlot {
    std::uint64_t frame_id;
    std::uint32_t valid_flags;
    std::uint32_t reserved0;
    float         left_position[3];
    float         left_orientation[4];
    float         left_fov[4];
    float         right_position[3];
    float         right_orientation[4];
    float         right_fov[4];
    std::uint8_t  padding[24];
};

struct SharedOverlayHeader {
    char          magic[8];
    std::uint32_t version;
    std::uint32_t command_stride_floats;
    std::uint32_t max_commands_per_eye;
    std::uint64_t latest_overlay_id;
    std::uint32_t left_count;
    std::uint32_t right_count;
    std::uint32_t slot_count;
    std::uint8_t  padding[24];
};

struct SharedOverlaySlotMetadata {
    std::uint64_t frame_id;
    std::uint32_t left_count;
    std::uint32_t right_count;
    std::uint32_t reserved0;
    std::uint32_t reserved1;
    std::uint8_t  padding[8];
};

struct SharedModalHeader {
    char          magic[8];
    std::uint32_t version;
    std::uint32_t max_width;
    std::uint32_t max_height;
    std::uint32_t slot_count;
    std::uint64_t latest_modal_id;
    std::uint32_t reserved0;
    std::uint32_t reserved1;
    std::uint8_t  padding[24];
};

struct SharedModalSlotMetadata {
    std::uint64_t frame_id;
    std::uint32_t valid_flags;
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t reserved0;
    float         left_quad[8];
    float         right_quad[8];
    float         width_m;
    float         height_m;
    std::uint8_t  padding[32];
};
#pragma pack(pop)

static_assert(sizeof(InputHeader) == 32);
static_assert(sizeof(InputEye) == 56);
static_assert(sizeof(InputPose) == 32);
static_assert(sizeof(InputButton) == 8);
static_assert(sizeof(InputAxis) == 12);
static_assert(sizeof(InputController) == 124);
static_assert(sizeof(InputPacket) == 392);
static_assert(sizeof(SharedFrameHeader) == 64);
static_assert(sizeof(SharedFramePoseMetadataSlot) == 128);
static_assert(sizeof(SharedOverlayHeader) == 64);
static_assert(sizeof(SharedOverlaySlotMetadata) == 32);
static_assert(sizeof(SharedModalHeader) == 64);
static_assert(sizeof(SharedModalSlotMetadata) == 128);

template<typename T>
bool read_struct(const std::uint8_t* data, std::size_t size, std::size_t offset, T* value) {
    if (data == nullptr || offset > size || sizeof(T) > size - offset) {
        return false;
    }
    std::memcpy(value, data + offset, sizeof(T));
    return true;
}

std::uint32_t pose_flags(const ILLIXR::data_format::quest_controller_pose& pose) {
    std::uint32_t flags = 0;
    flags |= pose.active ? kFlagActive : 0U;
    flags |= pose.position_valid ? kFlagPositionValid : 0U;
    flags |= pose.orientation_valid ? kFlagOrientationValid : 0U;
    flags |= pose.position_tracked ? kFlagPositionTracked : 0U;
    flags |= pose.orientation_tracked ? kFlagOrientationTracked : 0U;
    return flags;
}

void copy_pose(const ILLIXR::data_format::quest_controller_pose& source, InputPose* destination) {
    destination->flags = pose_flags(source);
    for (int index = 0; index < 3; ++index) {
        destination->position[index] = source.position[index];
    }
    destination->orientation[0] = source.orientation.x();
    destination->orientation[1] = source.orientation.y();
    destination->orientation[2] = source.orientation.z();
    destination->orientation[3] = source.orientation.w();
}

void copy_button(const ILLIXR::data_format::quest_controller_button& source, InputButton* destination) {
    destination->flags = (source.active ? kFlagActive : 0U) | (source.pressed ? kFlagPressed : 0U);
    destination->value = source.value;
}

void copy_controller(const ILLIXR::data_format::quest_hand_controller& source, InputController* destination) {
    destination->available_flags = source.available ? kFlagActive : 0U;
    destination->interaction_profile = static_cast<std::uint32_t>(source.interaction_profile);
    copy_pose(source.grip_pose, &destination->grip);
    copy_pose(source.aim_pose, &destination->aim);
    copy_button(source.trigger, &destination->trigger);
    copy_button(source.squeeze, &destination->squeeze);
    copy_button(source.primary, &destination->primary);
    copy_button(source.secondary, &destination->secondary);
    copy_button(source.thumbstick_click, &destination->thumbstick_click);
    destination->thumbstick.flags = source.thumbstick.active ? kFlagActive : 0U;
    destination->thumbstick.x     = source.thumbstick.value.x();
    destination->thumbstick.y     = source.thumbstick.value.y();
}

void copy_eye(const ILLIXR::data_format::openxr_eye_view& source, InputEye* destination) {
    destination->flags = source.pose_valid
        ? (kFlagActive | kFlagPositionValid | kFlagOrientationValid)
        : 0U;
    if (source.pose_tracked) {
        destination->flags |= kFlagPositionTracked | kFlagOrientationTracked;
    }
    destination->recommended_width  = source.recommended_width;
    destination->recommended_height = source.recommended_height;
    for (int index = 0; index < 3; ++index) {
        destination->position[index] = source.position[index];
    }
    destination->orientation[0] = source.orientation.x();
    destination->orientation[1] = source.orientation.y();
    destination->orientation[2] = source.orientation.z();
    destination->orientation[3] = source.orientation.w();
    destination->fov[0] = source.angle_left;
    destination->fov[1] = source.angle_right;
    destination->fov[2] = source.angle_up;
    destination->fov[3] = source.angle_down;
}

void copy_render_view(const float position[3], const float orientation[4], const float fov[4], bool valid,
                      ILLIXR::data_format::stereo_render_view* destination) {
    destination->valid       = valid;
    destination->position    = {position[0], position[1], position[2]};
    destination->orientation = {orientation[3], orientation[0], orientation[1], orientation[2]};
    destination->angle_left  = fov[0];
    destination->angle_right = fov[1];
    destination->angle_up    = fov[2];
    destination->angle_down  = fov[3];
}

bool same_magic(const char actual[8], const char expected[8]) {
    return std::memcmp(actual, expected, 8) == 0;
}

std::filesystem::path default_boba_install_root() {
    if (const char* xdg_data_home = std::getenv("XDG_DATA_HOME");
        xdg_data_home != nullptr && *xdg_data_home != '\0') {
        return std::filesystem::path{xdg_data_home} / "illixr" / "boba_immersive";
    }
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        return std::filesystem::path{home} / ".local" / "share" / "illixr" / "boba_immersive";
    }
    return {};
}

std::string resolve_boba_launcher(const std::shared_ptr<ILLIXR::switchboard>& switchboard) {
    const std::string launcher_override = switchboard->get_env("BOBA_DEMO_LAUNCHER");
    if (!launcher_override.empty()) {
        return launcher_override;
    }

    std::filesystem::path install_root{switchboard->get_env("BOBA_IMMERSIVE_ROOT")};
    if (install_root.empty()) {
        install_root = default_boba_install_root();
    }
    if (install_root.empty()) {
        return "Boba-Demo/boba_app.sh";
    }
    return (install_root / "Boba-Demo" / "boba_app.sh").string();
}

} // namespace

namespace ILLIXR {

boba_immersive::mapped_file::~mapped_file() {
    reset();
}

void boba_immersive::mapped_file::reset() {
    if (data != nullptr) {
        munmap(const_cast<std::uint8_t*>(data), size);
        data = nullptr;
        size = 0;
    }
    if (fd >= 0) {
        close(fd);
        fd = -1;
    }
}

boba_immersive::boba_immersive(const std::string& name, phonebook* pb)
    : plugin{name, pb}
    , switchboard_{pb->lookup_impl<switchboard>()}
    , clock_{pb->lookup_impl<relative_clock>()}
    , stoplight_{pb->lookup_impl<stoplight>()}
    , controller_reader_{switchboard_->get_reader<controller_input>("quest_controller")}
    , view_reader_{switchboard_->get_reader<view_frame>("openxr_view")}
    , stereo_writer_{switchboard_->get_writer<stereo_frame>("stereo_frame")}
    , boba_launcher_{resolve_boba_launcher(switchboard_)} {
    spdlogger(switchboard_->get_env_char("BOBA_IMMERSIVE_LOG_LEVEL", "info"));
}

boba_immersive::~boba_immersive() {
    stop_requested_.store(true);
    if (worker_.joinable()) {
        worker_.join();
    }
}

void boba_immersive::start() {
    plugin::start();
    stop_requested_.store(false);
    worker_ = std::thread([this]() {
        run();
    });
}

void boba_immersive::stop() {
    stop_requested_.store(true);
    if (worker_.joinable()) {
        worker_.join();
    }
    plugin::stop();
}

void boba_immersive::run() {
    stoplight_->wait_for_ready();
    if (!create_runtime_directory() || !create_input_socket() || !launch_boba()) {
        terminate_boba();
        cleanup_runtime_directory();
        plugin_logger_->error("Boba immersive startup failed; requesting ILLIXR shutdown");
        stoplight_->signal_should_stop();
        return;
    }

    plugin_logger_->info("Boba immersive producer started; input={} output={}", input_socket_path_, frame_path_);
    while (!stop_requested_.load() && !stoplight_->check_should_stop()) {
        int status = 0;
        const pid_t wait_result = waitpid(boba_pid_, &status, WNOHANG);
        if (wait_result == boba_pid_) {
            if (WIFEXITED(status)) {
                const int exit_code = WEXITSTATUS(status);
                if (exit_code == 0) {
                    plugin_logger_->info("Boba immersive process exited cleanly");
                } else {
                    plugin_logger_->error("Boba immersive process exited with code {}", exit_code);
                }
            } else if (WIFSIGNALED(status)) {
                plugin_logger_->error("Boba immersive process terminated by signal {}", WTERMSIG(status));
            }
            boba_pid_ = -1;
            plugin_logger_->info("Boba immersive stopped; requesting ILLIXR shutdown");
            stoplight_->signal_should_stop();
            break;
        }

        send_latest_input();
        map_output_files();
        publish_latest_frame();
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }

    terminate_boba();
    frame_mapping_.reset();
    overlay_mapping_.reset();
    modal_mapping_.reset();
    cleanup_runtime_directory();
}

bool boba_immersive::create_runtime_directory() {
    std::array<char, 64> template_path{};
    const char* prefix = "/tmp/illixr-boba-XXXXXX";
    std::copy(prefix, prefix + std::strlen(prefix) + 1, template_path.begin());
    char* result = mkdtemp(template_path.data());
    if (result == nullptr) {
        plugin_logger_->error("mkdtemp failed: {}", std::strerror(errno));
        return false;
    }
    runtime_directory_ = result;
    input_socket_path_  = runtime_directory_ + "/input.sock";
    frame_path_         = runtime_directory_ + "/stereo.bin";
    overlay_path_       = runtime_directory_ + "/overlay.bin";
    modal_path_         = runtime_directory_ + "/modal.bin";
    return true;
}

bool boba_immersive::create_input_socket() {
    input_socket_ = socket(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    if (input_socket_ < 0) {
        plugin_logger_->error("socket(AF_UNIX) failed: {}", std::strerror(errno));
        return false;
    }
    if (input_socket_path_.size() >= sizeof(sockaddr_un::sun_path)) {
        plugin_logger_->error("Boba input socket path is too long: {}", input_socket_path_);
        return false;
    }
    return true;
}

bool boba_immersive::launch_boba() {
    if (!std::filesystem::is_regular_file(boba_launcher_)) {
        plugin_logger_->error(
            "Boba launcher was not found: {}. Run setup_boba_immersive.sh, set BOBA_IMMERSIVE_ROOT, or set "
            "BOBA_DEMO_LAUNCHER to override the launcher directly.",
            boba_launcher_);
        return false;
    }

    std::vector<std::string> environment_storage;
    for (char** entry = environ; entry != nullptr && *entry != nullptr; ++entry) {
        const std::string_view value{*entry};
        if (value.rfind("BOBA_ILLIXR_INPUT_SOCKET=", 0) == 0 ||
            value.rfind("BOBA_ILLIXR_FRAME_PATH=", 0) == 0 ||
            value.rfind("BOBA_ILLIXR_OVERLAY_PATH=", 0) == 0 ||
            value.rfind("BOBA_ILLIXR_MODAL_PATH=", 0) == 0 ||
            value.rfind("BOBA_RUNTIME_ENV=", 0) == 0) {
            continue;
        }
        environment_storage.emplace_back(*entry);
    }
    environment_storage.emplace_back("BOBA_ILLIXR_INPUT_SOCKET=" + input_socket_path_);
    environment_storage.emplace_back("BOBA_ILLIXR_FRAME_PATH=" + frame_path_);
    environment_storage.emplace_back("BOBA_ILLIXR_OVERLAY_PATH=" + overlay_path_);
    environment_storage.emplace_back("BOBA_ILLIXR_MODAL_PATH=" + modal_path_);
    environment_storage.emplace_back(std::string{"BOBA_RUNTIME_ENV="} + kBobaRuntimeEnvironment);

    std::vector<char*> environment;
    environment.reserve(environment_storage.size() + 1);
    for (std::string& value : environment_storage) {
        environment.push_back(value.data());
    }
    environment.push_back(nullptr);

    std::array<char*, 4> arguments{
        const_cast<char*>("/bin/bash"),
        boba_launcher_.data(),
        const_cast<char*>("--illixr"),
        nullptr,
    };
    posix_spawnattr_t attributes;
    if (posix_spawnattr_init(&attributes) != 0) {
        plugin_logger_->error("posix_spawnattr_init failed");
        return false;
    }
    short flags = POSIX_SPAWN_SETPGROUP;
    posix_spawnattr_setflags(&attributes, flags);
    posix_spawnattr_setpgroup(&attributes, 0);
    const int result = posix_spawn(&boba_pid_, "/bin/bash", nullptr, &attributes, arguments.data(), environment.data());
    posix_spawnattr_destroy(&attributes);
    if (result != 0) {
        boba_pid_ = -1;
        plugin_logger_->error("Unable to launch Boba: {}", std::strerror(result));
        return false;
    }
    plugin_logger_->info("Launched the existing Boba immersive demo (pid={}, launcher={}, conda_env={})",
                         boba_pid_, boba_launcher_, kBobaRuntimeEnvironment);
    return true;
}

void boba_immersive::terminate_boba() {
    if (boba_pid_ <= 0) {
        return;
    }
    kill(-boba_pid_, SIGTERM);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    while (std::chrono::steady_clock::now() < deadline) {
        int status = 0;
        const pid_t result = waitpid(boba_pid_, &status, WNOHANG);
        if (result == boba_pid_ || (result < 0 && errno == ECHILD)) {
            boba_pid_ = -1;
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{50});
    }
    plugin_logger_->warn("Boba did not stop after SIGTERM; sending SIGKILL");
    kill(-boba_pid_, SIGKILL);
    waitpid(boba_pid_, nullptr, 0);
    boba_pid_ = -1;
}

void boba_immersive::cleanup_runtime_directory() {
    if (input_socket_ >= 0) {
        close(input_socket_);
        input_socket_ = -1;
    }
    for (const std::string* path : {&input_socket_path_, &frame_path_, &overlay_path_, &modal_path_}) {
        if (!path->empty()) {
            unlink(path->c_str());
        }
    }
    if (!runtime_directory_.empty()) {
        rmdir(runtime_directory_.c_str());
    }
}

bool boba_immersive::send_latest_input() {
    const auto controller = controller_reader_.get_ro_nullable();
    const auto views      = view_reader_.get_ro_nullable();
    if (controller == nullptr || views == nullptr || controller->sequence != views->sequence) {
        return false;
    }
    if (have_input_sequence_ && views->sequence <= last_input_sequence_) {
        return false;
    }

    InputPacket packet{};
    std::memcpy(packet.header.magic, kInputMagic, sizeof(kInputMagic));
    packet.header.version        = kInputVersion;
    packet.header.byte_count     = sizeof(packet);
    packet.header.sequence       = views->sequence;
    packet.header.xr_sample_time = views->xr_sample_time;
    copy_eye(views->left, &packet.eyes[0]);
    copy_eye(views->right, &packet.eyes[1]);
    copy_controller(controller->left, &packet.controllers[0]);
    copy_controller(controller->right, &packet.controllers[1]);

    sockaddr_un destination{};
    destination.sun_family = AF_UNIX;
    std::memcpy(destination.sun_path, input_socket_path_.c_str(), input_socket_path_.size() + 1);
    const ssize_t sent = sendto(input_socket_, &packet, sizeof(packet), MSG_DONTWAIT,
                                reinterpret_cast<const sockaddr*>(&destination), sizeof(destination));
    if (sent == static_cast<ssize_t>(sizeof(packet))) {
        last_input_sequence_ = views->sequence;
        have_input_sequence_ = true;
        return true;
    }
    if (sent < 0 && errno != ENOENT && errno != ECONNREFUSED && errno != EAGAIN && errno != EWOULDBLOCK) {
        plugin_logger_->warn("Unable to send Boba input sample: {}", std::strerror(errno));
    }
    return false;
}

bool boba_immersive::map_if_ready(const std::string& path, mapped_file* mapping) {
    if (mapping->data != nullptr) {
        return true;
    }
    const int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        return false;
    }
    struct stat status {};
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
    return true;
}

bool boba_immersive::map_output_files() {
    const bool frame_ready = map_if_ready(frame_path_, &frame_mapping_);
    map_if_ready(overlay_path_, &overlay_mapping_);
    map_if_ready(modal_path_, &modal_mapping_);
    return frame_ready;
}

bool boba_immersive::publish_latest_frame() {
    if (frame_mapping_.data == nullptr) {
        return false;
    }

    SharedFrameHeader header{};
    if (!read_struct(frame_mapping_.data, frame_mapping_.size, 0, &header) ||
        !same_magic(header.magic, kFrameMagic) || header.version != kFrameVersion ||
        header.channels != kChannelCount || header.slot_count == 0 ||
        header.latest_slot >= header.slot_count || header.latest_frame_id == 0 ||
        header.latest_frame_id <= last_frame_id_) {
        return false;
    }

    const std::size_t slot = static_cast<std::size_t>(header.latest_slot);
    const std::size_t metadata_offset = sizeof(SharedFrameHeader) + slot * sizeof(SharedFramePoseMetadataSlot);
    SharedFramePoseMetadataSlot pose_metadata{};
    if (!read_struct(frame_mapping_.data, frame_mapping_.size, metadata_offset, &pose_metadata) ||
        pose_metadata.frame_id != header.latest_frame_id) {
        return false;
    }

    const std::uint64_t eye_bytes = static_cast<std::uint64_t>(header.width) * header.height * header.channels;
    if (header.frame_bytes != eye_bytes * kEyeCount ||
        header.metadata_bytes < header.slot_count * sizeof(SharedFramePoseMetadataSlot)) {
        return false;
    }
    const std::uint64_t slot_offset = sizeof(SharedFrameHeader) + header.metadata_bytes
        + static_cast<std::uint64_t>(slot) * header.frame_bytes;
    if (slot_offset > frame_mapping_.size || header.frame_bytes > frame_mapping_.size - slot_offset) {
        return false;
    }

    auto output = stereo_writer_.allocate();
    output->sequence        = header.latest_frame_id;
    output->sample_time     = clock_->now();
    output->source_frame_id = header.latest_frame_id;
    output->format          = data_format::stereo_pixel_format::rgba8_unorm;
    output->origin          = data_format::stereo_image_origin::upper_left;
    if (header.presentation_mode <= static_cast<std::uint32_t>(data_format::stereo_presentation_mode::head_locked_panel)) {
        output->presentation_mode = static_cast<data_format::stereo_presentation_mode>(header.presentation_mode);
    }
    output->pixel_buffer_path       = frame_path_;
    output->pixel_generation_offset = metadata_offset;
    output->left = {slot_offset, eye_bytes, header.width, header.height, header.width * header.channels};
    output->right = {slot_offset + eye_bytes, eye_bytes, header.width, header.height,
                     header.width * header.channels};
    copy_render_view(pose_metadata.left_position, pose_metadata.left_orientation, pose_metadata.left_fov,
                     (pose_metadata.valid_flags & 1U) != 0, &output->left_render_view);
    copy_render_view(pose_metadata.right_position, pose_metadata.right_orientation, pose_metadata.right_fov,
                     (pose_metadata.valid_flags & 2U) != 0, &output->right_render_view);

    SharedOverlayHeader overlay_header{};
    const std::size_t overlay_metadata_offset = sizeof(SharedOverlayHeader) + slot * sizeof(SharedOverlaySlotMetadata);
    SharedOverlaySlotMetadata overlay_metadata{};
    if (read_struct(overlay_mapping_.data, overlay_mapping_.size, 0, &overlay_header) &&
        same_magic(overlay_header.magic, kOverlayMagic) && overlay_header.version == kOverlayVersion &&
        overlay_header.command_stride_floats == kOverlayCommandFloats &&
        overlay_header.slot_count == header.slot_count &&
        read_struct(overlay_mapping_.data, overlay_mapping_.size, overlay_metadata_offset, &overlay_metadata) &&
        overlay_metadata.frame_id == header.latest_frame_id) {
        const std::uint64_t command_stride_bytes =
            static_cast<std::uint64_t>(overlay_header.command_stride_floats) * sizeof(float);
        const std::uint64_t eye_stride_bytes =
            static_cast<std::uint64_t>(overlay_header.max_commands_per_eye) * command_stride_bytes;
        const std::uint64_t payload_offset = sizeof(SharedOverlayHeader)
            + static_cast<std::uint64_t>(overlay_header.slot_count) * sizeof(SharedOverlaySlotMetadata)
            + static_cast<std::uint64_t>(slot) * kEyeCount * eye_stride_bytes;
        if (payload_offset <= overlay_mapping_.size && kEyeCount * eye_stride_bytes <= overlay_mapping_.size - payload_offset) {
            output->overlay_buffer_path       = overlay_path_;
            output->overlay_generation_offset = overlay_metadata_offset;
            output->left_overlay_commands = {
                payload_offset,
                std::min(overlay_metadata.left_count, overlay_header.max_commands_per_eye),
                overlay_header.command_stride_floats,
            };
            output->right_overlay_commands = {
                payload_offset + eye_stride_bytes,
                std::min(overlay_metadata.right_count, overlay_header.max_commands_per_eye),
                overlay_header.command_stride_floats,
            };
        }
    }

    SharedModalHeader modal_header{};
    const std::size_t modal_metadata_offset = sizeof(SharedModalHeader) + slot * sizeof(SharedModalSlotMetadata);
    SharedModalSlotMetadata modal_metadata{};
    if (read_struct(modal_mapping_.data, modal_mapping_.size, 0, &modal_header) &&
        same_magic(modal_header.magic, kModalMagic) && modal_header.version == kModalVersion &&
        modal_header.slot_count == header.slot_count &&
        read_struct(modal_mapping_.data, modal_mapping_.size, modal_metadata_offset, &modal_metadata) &&
        modal_metadata.frame_id == header.latest_frame_id && modal_metadata.width <= modal_header.max_width &&
        modal_metadata.height <= modal_header.max_height) {
        const std::uint64_t modal_slot_bytes =
            static_cast<std::uint64_t>(modal_header.max_width) * modal_header.max_height * kChannelCount;
        const std::uint64_t modal_payload_offset = sizeof(SharedModalHeader)
            + static_cast<std::uint64_t>(modal_header.slot_count) * sizeof(SharedModalSlotMetadata)
            + static_cast<std::uint64_t>(slot) * modal_slot_bytes;
        if (modal_payload_offset <= modal_mapping_.size && modal_slot_bytes <= modal_mapping_.size - modal_payload_offset) {
            output->modal_buffer_path       = modal_path_;
            output->modal_generation_offset = modal_metadata_offset;
            output->modal.visible     = (modal_metadata.valid_flags & kModalVisibleFlag) != 0;
            output->modal.left_valid  = (modal_metadata.valid_flags & kModalLeftValidFlag) != 0;
            output->modal.right_valid = (modal_metadata.valid_flags & kModalRightValidFlag) != 0;
            output->modal.byte_offset = modal_payload_offset;
            output->modal.width       = modal_metadata.width;
            output->modal.height      = modal_metadata.height;
            output->modal.source_row_stride_bytes = modal_header.max_width * kChannelCount;
            for (std::size_t index = 0; index < 4; ++index) {
                output->modal.left_quad_pixels[index] = {
                    modal_metadata.left_quad[index * 2], modal_metadata.left_quad[index * 2 + 1]};
                output->modal.right_quad_pixels[index] = {
                    modal_metadata.right_quad[index * 2], modal_metadata.right_quad[index * 2 + 1]};
            }
            output->modal.width_m  = modal_metadata.width_m;
            output->modal.height_m = modal_metadata.height_m;
        }
    }

    SharedFrameHeader confirm_header{};
    SharedFramePoseMetadataSlot confirm_metadata{};
    if (!read_struct(frame_mapping_.data, frame_mapping_.size, 0, &confirm_header) ||
        !read_struct(frame_mapping_.data, frame_mapping_.size, metadata_offset, &confirm_metadata) ||
        confirm_header.latest_frame_id != header.latest_frame_id || confirm_header.latest_slot != header.latest_slot ||
        confirm_metadata.frame_id != header.latest_frame_id) {
        return false;
    }

    last_frame_id_ = header.latest_frame_id;
    const std::uint32_t published_left_overlays  = output->left_overlay_commands.command_count;
    const std::uint32_t published_right_overlays = output->right_overlay_commands.command_count;
    const bool          published_modal          = output->modal.visible;
    stereo_writer_.put(std::move(output));
    if (last_frame_id_ == 1 || last_frame_id_ % 300 == 0) {
        plugin_logger_->info("Published Boba stereo_frame id={} size={}x{} overlays={}/{} modal={}",
                             last_frame_id_, header.width, header.height,
                             published_left_overlays, published_right_overlays,
                             published_modal);
    }
    return true;
}

} // namespace ILLIXR

using namespace ILLIXR;

PLUGIN_MAIN(boba_immersive)
