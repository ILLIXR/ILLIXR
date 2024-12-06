#pragma once

#include <algorithm> // for std::for_each
#include <chrono>    // for std::chrono::nanoseconds
// #include <cstddef>
#include "dataset_loader.hpp"
#include "illixr/common/data_format.hpp"

#include <functional>      // for std::greater
#include <queue>           // for std::priority_queue
#include <spdlog/spdlog.h> // for debug messages

enum class DataType {
    IMAGE,
    IMU,
    POSE,
    GROUND_TRUTH,
};

struct DataEntry {
    std::chrono::nanoseconds timestamp;
    DataType                 type;
};

class DataEmitter {
    // the way this class works is as follows: we have a queue of DataEntry objects,
    // which are simply a combination of timestamps along with what data needs to be
    // emitted at that time.
    //
    // When the time comes, we emit the relevant data on the relevant channel.
public:
    DataEmitter(std::chrono::nanoseconds starting_time)
        : data_list{}
        , data{}
        , starting_time{starting_time} {
        // initializing the DatasetLoader loads in all the data from the dataset, so
        // we don't need to do anything special here.

        // we enter each timestamp and the corresponding data type to the priority queue
        std::for_each(data.m_imageData.cbegin(), data.m_imageData.cend(), [&](const auto& elem) {
            insertDataEntry(elem.first, DataType::IMAGE);
        });

        std::for_each(data.m_IMUData.cbegin(), data.m_IMUData.cend(), [&](const auto& elem) {
            insertDataEntry(elem.first, DataType::IMU);
        });

        std::for_each(data.m_poseData.cbegin(), data.m_poseData.cend(), [&](const auto& elem) {
            insertDataEntry(elem.first, DataType::POSE);
        });

        std::for_each(data.m_groundTruthData.cbegin(), data.m_groundTruthData.cend(), [&](const auto& elem) {
            insertDataEntry(elem.first, DataType::GROUND_TRUTH);
        });

        // the top element is the one with the earliest timestamp.
        // We record this for the purposes of
        // aligning the dataset time with the system time as much as possible
        // TODO: see if 2nd sentence makes sense.
        dataset_first_time = data_list.top.first;
    }

    void emit(std::chrono::nanoseconds current_time) {
        // we assume that current_time is the time since the Unix Epoch.

        std::chrono::nanoseconds dataset_time = (current_time - starting_time) + dataset_first_time;
        if (data_list.empty()) {
            // there is no more data left to emit, so we exit early
            return;
        }

        // we simply publish all the data that has timestamps <= `current_time`
        while (data_list.top().timestamp < dataset_time && !data_list.empty()) {
            DataEntry entry = priorityQueue.pop();

            if (entry.type == IMAGE) {
                emitImageData(dataset_time);
            } else if (entry.type == IMU) {
                emitIMUData(dataset_time);
            } else if (entry.type == POSE) {
                emitPoseData(dataset_time);
            } else {
                // ground truth
                emitGroundTruthData(timestamp);
            }
        }
    }

    // TODO: decide whether it is worth implementing this alternate version that drops data items that
    // are too old and only publishes "fresh" ones that are in a "window of viability".
    // void emit(std::chrono::nanoseconds current_time) {
    //     // we assume that current_time is the time since the Unix Epoch.

    // // time that has elapsed since startup, plus offset (first time in the dataset).
    // std::chrono::nanoseconds lower_bound_time = (current_time - starting_time) + dataset_first_time;

    // std::chrono::nanoseconds upper_bound_time = lower_bound_time + error_cushion;

    // // if (currentTime - nextEntry.timestamp > EMISSION_TOLERANCE_NS) {
    // //     // Too late to emit, drop this entry
    // //     priorityQueue.pop(); // Drop the entry
    // //     continue;
    // // }

    // // print info about how many entries were dropped due to the thread waking up late.
    // int count = 0;
    // while (data_list.top().timestamp < lower_bound_time) {
    //     data_list.pop();
    //     count++;
    // }

    // spdlog::get("illixr")->info("[dataset_plugin] Dropped {} data items due to thread scheduling.", count);

    // // EMISSION
    // // if (!priorityQueue.empty()) {
    // //     DataEntry nextEntry = priorityQueue.top();
    // //     // Emit data based on nextEntry.type
    // //     priorityQueue.pop();
    // // }

    // if (data_list.empty()) {
    //     // there is no more data left to emit.
    //     return;
    // }

    // // we simply publish all the data that has timestamps
    // for (auto it = m_data.lower_bound(lower_bound_time); it != m_data.upper_bound(upper_bound_time); ++it) {
    //     auto [timestamp, type] = *it;

    // if (type == IMAGE) {
    //     emitImageData(timestamp);
    // } else if (type == IMU) {
    //     emitIMUData(timestamp);
    // } else if (type == POSE) {
    //     emitPoseData(timestamp);
    // } else {
    //     // ground truth
    //     emitGroundTruthData(timestamp);
    // }
    // }
    // }

    bool finished() {
        // have we run out of data to emit?
        return data_list.empty();
    }

    std::chrono::nanoseconds sleep_for() {
        // how long should the data emitter thread sleep for?

        // what is the next time at which we must emit some data?
        std::chrono::nanoseconds dataset_next = data_list.top.first;

        // the time difference needs to be casted to `time_point` because `m_rtc` returns that type.
        // the explicit type of the `sleep_time` variable will trigger a typecast of the final calculated expression.
        std::chrono::nanoseconds sleep_time = time_point{dataset_next - dataset_first_time} - m_rtc->now();

        return sleep_time;
    }

private:
    void insertDataEntry(std::uint64_t timestamp, DataType type) {
        DataEntry entry{timestamp, type};
        data_list.push(entry); // Automatically sorted by timestamp
    }

    void emitImageData(std::chrono::nanoseconds timestamp) {
        auto range = data.getImageData.equal_range(timestamp);

        for (auto it = range.first; it != range.second; ++it) {
            ImageData datum = it->second;

            // we report the time relative to the first timestamp. That is, we report how long it has been since the
            // first data item was emitted.
            // For example, if the first item has a timestamp of 11000ns and the second item has a timestamp of 11334ns,
            // we will publish 314ns as the timestamp.
            time_point time(timestamp - dataset_first_time);

            m_image_publisher.put(
                m_image_publisher.allocate<image_type>(image_type{time, datum.loadImage(), datum.getChannel()}));
        }
    }

    void emitIMUData(std::chrono::nanoseconds timestamp) {
        auto range = data.getIMUData.equal_range(timestamp);

        for (auto it = range.first; it != range.second; ++it) {
            IMUData datum = it->second;

            // we report the time relative to the first timestamp. That is, we report how long it has been since the
            // first data item was emitted.
            // For example, if the first item has a timestamp of 11000ns and the second item has a timestamp of 11334ns,
            // we will publish 314ns as the timestamp.
            time_point time(timestamp - dataset_first_time);

            m_imu_publisher.put(
                m_imu_publisher.allocate<imu_type>(imu_type{time, datum.angular_v, datum.linear_a, datum.channel}));
        }
    }

    void emitPoseData(std::chrono::nanoseconds timestamp) {
        auto range = data.getPoseData.equal_range(timestamp);

        for (auto it = range.first; it != range.second; ++it) {
            PoseData datum = it->second;

            // we report the time relative to the first timestamp. That is, we report how long it has been since the
            // first data item was emitted.
            // For example, if the first item has a timestamp of 11000ns and the second item has a timestamp of 11334ns,
            // we will publish 314ns as the timestamp.
            time_point time(timestamp - dataset_first_time);

            m_pose_publisher.put(m_pose_publisher.allocate<pose_type>(pose_type{time, datum.position, datum.orientation}));
        }
    }

    void emitGroundTruthData(std::chrono::nanoseconds timestamp) {
        auto range = data.getImageData.equal_range(timestamp);

        for (auto it = range.first; it != range.second; ++it) {
            ImageData datum = it->second;

            // we report the time relative to the first timestamp. That is, we report how long it has been since the
            // first data item was emitted.
            // For example, if the first item has a timestamp of 11000ns and the second item has a timestamp of 11334ns,
            // we will publish 314ns as the timestamp.
            time_point time(timestamp - dataset_first_time);

            m_ground_truth_publisher.put(
                m_ground_truth_publisher.allocate<ground_truth_type>(ground_truth_type{time, datum.data}));
        }
    }

    std::priority_queue<DataEntry, std::greater<std::chrono::nanoseconds>> data_list;
    // using the custom comparator means that the earliest timestamp appears earlier in the queue.
    DatasetLoader data;

    std::chrono::nanoseconds dataset_first_time;
    std::chrono::nanoseconds time_since_start;
    // const std::chrono::nanoseconds error_cushion{500};

    // writers
    using namespace ILLIXR;
    switchboard::writer<image_type>        m_image_publisher;
    switchboard::writer<imu_type>          m_imu_publisher;
    switchboard::writer<pose_type>         m_pose_publisher;
    switchboard::writer<ground_truth_type> m_ground_truth_publisher;
};

// void processNextEmission() {
//     uint64_t currentTime = getCurrentTime();

// while (!priorityQueue.empty()) {
//     DataEntry nextEntry = priorityQueue.top();

// if (currentTime < nextEntry.timestamp) {
//     // Not yet time to emit
//     break;
// }

// if (currentTime - nextEntry.timestamp > EMISSION_TOLERANCE_NS) {
//     // Too late to emit, drop this entry
//     priorityQueue.pop(); // Drop the entry
//     continue;
// }

// // Emit the data
// emitData(nextEntry);
// priorityQueue.pop(); // Remove the emitted entry
// }
// }
