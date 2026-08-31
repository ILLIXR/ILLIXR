#pragma once

#include "illixr/data_format/semantics.hpp"
#include "illixr/switchboard.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <optional>
#include <vector>

namespace ILLIXR::decode {

// ---------------------------------------------------------------------------
// decoded_frame_cache
//
// Ring buffer storing decoded RGB frames alongside their frame numbers.
// Sized to match the switchboard topic history depth (256).
//
// Written by the decode callback (switchboard thread).
// Read by latest() (Python thread).
// Protected by a mutex.
//
// Each slot owns a std::vector<uint8_t> that holds a copy of the pinned
// host RGB buffer. The copy is necessary because NvdecDecoder reuses its
// pinned buffer on each decode() call.
// ---------------------------------------------------------------------------
class decoded_frame_cache {
public:
    static constexpr size_t CAPACITY = 256;

    struct Entry {
        std::vector<uint8_t> rgb; // packed RGB uint8, H*W*3 bytes
        int32_t              frame_number = -1;
        int32_t              width        = 0;
        int32_t              height       = 0;
        bool                 valid        = false;
    };

    // Store a decoded RGB frame. Copies from the pinned host pointer.
    void store(int32_t frame_number, int32_t w, int32_t h, const uint8_t* rgb_data) {
        std::lock_guard<std::mutex> lock(mutex_);
        Entry&                      slot = entries_[next_slot_];
        slot.frame_number                = frame_number;
        slot.width                       = w;
        slot.height                      = h;
        const auto nbytes                = static_cast<size_t>(w * h * 3);
        slot.rgb.resize(nbytes);
        std::memcpy(slot.rgb.data(), rgb_data, nbytes);
        slot.valid = true;
        next_slot_ = (next_slot_ + 1) % CAPACITY;
    }

    // Return a copy of the most recently stored entry, taken under the lock.
    // Returns nullopt if no frame has been stored yet.
    std::optional<Entry> latest() const {
        std::lock_guard<std::mutex> lock(mutex_);
        int32_t      best_fn = -1;
        const Entry* best    = nullptr;
        for (const auto& e : entries_) {
            if (e.valid && e.frame_number > best_fn) {
                best_fn = e.frame_number;
                best    = &e;
            }
        }
        if (best == nullptr)
            return std::nullopt;
        return *best; // copy made while lock is held
    }

private:
    mutable std::mutex          mutex_;
    std::array<Entry, CAPACITY> entries_;
    size_t                      next_slot_ = 0;
};

// ---------------------------------------------------------------------------
// semantic_metadata_cache
//
// Ring buffer storing each semantic_frame (depth, poses, intrinsics, etc.),
// keyed by frame_number. Holds a switchboard::ptr to the frame itself rather
// than copying individual fields out, so the frame's other data stays alive
// and zero-copy-accessible for as long as the slot is valid.
//
// decode lags arrival by a variable amount (see nvdec_decoder::decode), so
// the frame most recently decoded is not the frame most recently arrived.
// This cache lets get() look up the arrival-time data (depth/pose/etc.)
// for whichever frame_number the decoder actually finished decoding,
// rather than assuming the two arrive together.
//
// Written by on_semantic_data() (switchboard thread), at arrival, before
// the decode call. Read by find() (Python thread).
// Protected by a mutex.
// ---------------------------------------------------------------------------
class semantic_metadata_cache {
public:
    static constexpr size_t CAPACITY = 256;

    struct Entry {
        switchboard::ptr<const data_format::semantic_frame> data;
        int32_t                                             frame_number = -1;
        bool                                                valid        = false;
    };

    // Store a frame's metadata, keyed by frame_number.
    void store(int32_t frame_number, const switchboard::ptr<const data_format::semantic_frame>& frame) {
        std::lock_guard<std::mutex> lock(mutex_);
        Entry&                      slot = entries_[next_slot_];
        slot.data                        = frame;
        slot.frame_number                = frame_number;
        slot.valid                       = true;
        next_slot_                       = (next_slot_ + 1) % CAPACITY;
    }

    // Return a copy of the entry matching frame_number, taken under the lock.
    // Returns nullopt if not found (evicted or not yet arrived).
    std::optional<Entry> find(int32_t frame_number) const {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& e : entries_) {
            if (e.valid && e.frame_number == frame_number)
                return e; // copy made while lock is held
        }
        return std::nullopt;
    }

private:
    mutable std::mutex          mutex_;
    std::array<Entry, CAPACITY> entries_;
    size_t                      next_slot_ = 0;
};

} // namespace ILLIXR::decode
