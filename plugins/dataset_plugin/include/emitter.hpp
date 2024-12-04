#pragma once

#include <algorithm> // for std::for_each
#include <chrono>    // for std::chrono::nanoseconds
// #include <cstddef>
#include <functional> // for std::greater
#include <queue>     // for std::priority_queue

#include "illixr/common/data_format.hpp"

#include "dataset_loader.hpp"

enum class DataType {
    IMAGE,
    IMU,
    POSE,
    GROUND_TRUTH,
};

struct DataEntry {
    std::chrono::nanoseconds timestamp;
    DataType type;
};

class DataEmitter {
    // the way this class works is as follows: we have a queue of DataEntry objects,
    // which are simply a combination of timestamps along with what data needs to be
    // emitted at that time.
    //
    // When the time comes, we emit the relevant data on the relevant channel.
    public:
    DataEmitter() : data_list{}, data{} {
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
        std::chrono::nanoseconds upper_bound_time = time_since_start + dataset_first_time;

        std::chrono::nanoseconds lower_bound_time = upper_bound_time - error_cushion;

        for (m_data_iterator = m_data.lower_bound(lower_bound_time); m_data_iterator != m_data.upper_bound(upper_bound_time);
             ++m_data_iterator) {
            GroundTruthData datum = m_data_iterator->second;

            time_point expected_real_time_given_dataset_time(m_data_iterator->first - dataset_first_time);

            m_ground_truth_publisher.put(m_ground_truth_publisher.allocate<ground_truth_type>(
                ground_truth_type{expected_real_time_given_dataset_time, datum.data}));
        }

        while (data_list.top.first - current_time >= error_cushion) {
            // this means that 

        }
        emitImageData()
    }

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

    void emitImageData() {
        
    }

    void emitIMUData() {

    }

    void emitPoseData() {

    }

    void emitGroundTruthData() {

    }

    std::priority_queue<DataEntry, std::greater<std::chrono::nanoseconds>> data_list;
    // using the custom comparator means that the earliest timestamp appears earlier in the queue.
    DatasetLoader data;

    
    std::chrono::nanoseconds       dataset_first_time;
    const std::chrono::nanoseconds error_cushion{500};


    // writers
    using namespace ILLIXR;
    switchboard::writer<image_type>        m_image_publisher;
    switchboard::writer<imu_type>          m_imu_publisher;
    switchboard::writer<pose_type>         m_pose_publisher;
    switchboard::writer<ground_truth_type> m_ground_truth_publisher;
};




// EMISSION
if (!priorityQueue.empty()) {
    DataEntry nextEntry = priorityQueue.top();
    // Emit data based on nextEntry.type
    priorityQueue.pop();
}

// EMISSION WITH TOLERANCE CHECK
const uint64_t EMISSION_TOLERANCE_NS = 1000000; // Example: 1 ms
void processNextEmission() {
    uint64_t currentTime = getCurrentTime();

    while (!priorityQueue.empty()) {
        DataEntry nextEntry = priorityQueue.top();
        
        if (currentTime < nextEntry.timestamp) {
            // Not yet time to emit
            break;
        }
        
        if (currentTime - nextEntry.timestamp > EMISSION_TOLERANCE_NS) {
            // Too late to emit, drop this entry
            priorityQueue.pop();  // Drop the entry
            continue;
        }

        // Emit the data
        emitData(nextEntry);
        priorityQueue.pop();  // Remove the emitted entry
    }
}
