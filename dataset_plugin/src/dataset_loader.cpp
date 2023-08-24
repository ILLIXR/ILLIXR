#include "include/dataset_loader.hpp"

#include "common/csv_iterator.hpp"

#include <cstddef> // for the std::size_t data type
#include <fstream>
#include <iostream> // for std::clog

// TODO: update the index-related  comments to match the current design.

bool DatasetLoader::isImageFile(const std::filesystem::path& filename) {
    std::string extension = filename.extension().string();
    return (extension == ".jpg" || extension == ".png");
}

std::chrono::nanoseconds DatasetLoader::convertToTimestamp(const TimestampUnit& tsUnit, unsigned long long timestamp_value) {
    // we will handle all timestamps as nanoseconds under the hood

    if (tsUnit == TimestampUnit::second) {
        // the conversion from seconds to nanoseconds is automatically done by
        // the std::chrono::nanoseconds constructor

        return std::chrono::seconds{timestamp_value};
    } else if (tsUnit == TimestampUnit::millisecond) {
        // the conversion from milliseconds to nanoseconds is automatically done
        // by the std::chrono::nanoseconds constructor

        return std::chrono::milliseconds{timestamp_value};
    } else if (tsUnit == TimestampUnit::microsecond) {
        // the conversion from microseconds to nanoseconds is automatically done
        // by the std::chrono::nanoseconds constructor

        return std::chrono::microseconds{timestamp_value};
    } else {
        // the only remaining option is nanoseconds

        return std::chrono::nanoseconds{timestamp_value};
    }
}

void DatasetLoader::loadIMUData() {
#ifndef NDEBUG
    std::clog << "Loading IMU Data...\n";
#endif

    for (std::size_t i = 0; i < m_config.imu_config.path_list.size(); ++i) {
        // we need to know the index of the path to know which channel to publish
        // the IMU data on. For example, if the IMU data is from the 2nd path in
        // the list, then we need to know that so that we can mark it for
        // publication on the imu2 channel.

        std::ifstream imuFile{m_config.imu_config.path_list[i]};

        if (m_config.imu_config.format[i]) {
            // then linear acceleration is first

            for (CSVIterator row{imuFile, 1}; row != CSVIterator{}; ++row) {
                // we skip the first row because it contains the column names

                std::chrono::nanoseconds timestamp =
                    convertToTimestamp(m_config.imu_config.timestamp_unit, std::stoull(row[0]));

                Eigen::Vector3d lin_accel{std::stod(row[1]), std::stod(row[2]), std::stod(row[3])};
                Eigen::Vector3d ang_vel{std::stod(row[4]), std::stod(row[5]), std::stod(row[6])};

                m_IMUData.insert({timestamp, IMUData{ang_vel, lin_accel, i}});
            }
        } else {
            // angular velocity is the first element

            for (CSVIterator row{imuFile, 1}; row != CSVIterator{}; ++row) {
                // we skip the first row because it contains the column names

                std::chrono::nanoseconds timestamp =
                    convertToTimestamp(m_config.imu_config.timestamp_unit, std::stoull(row[0]));

                Eigen::Vector3d ang_vel{std::stod(row[1]), std::stod(row[2]), std::stod(row[3])};
                Eigen::Vector3d lin_accel{std::stod(row[4]), std::stod(row[5]), std::stod(row[6])};

                m_IMUData.insert({timestamp, IMUData{ang_vel, lin_accel, i}});
            }
        }
    }

#ifndef NDEBUG
    std::clog << "Finished loading IMU Data.\n";
#endif
}

void DatasetLoader::loadImageData() {
    // TODO: Change the loading format to reading a csv file for each type of image.
#ifndef NDEBUG
    std::clog << "Loading RGB Images...\n";
#endif

    for (std::size_t i = 0; i < m_config.image_config.rgb_path_list.size(); ++i) {
        // we need to know the index of the path to know which channel to publish
        // the image on. For example, if the image is from the 2nd path in the list,
        // then we need to know that so that we can mark it for publication on the
        // rgb2 channel.

        std::filesystem::path path = m_config.image_config.rgb_path_list[i];

        for (const auto& file : std::filesystem::directory_iterator(path)) {
            if (file.is_regular_file()) {
                // checking that it's a normal file, just to be safe

                if (isImageFile(file.path().filename())) {
                    // we don't want to do anything with non-image files.

                    std::string filename = file.path().stem();

                    std::chrono::nanoseconds timestamp =
                        convertToTimestamp(m_config.image_config.timestamp_unit, std::stoull(filename));

                    m_imageData.insert({timestamp, ImageData{file.path(), i, ImageType::RGB}});
                }
            }
        }
    }

#ifndef NDEBUG
    std::clog << "Finished loading RGB Images.\n";

    std::clog << "Loading Depth Images...\n";
#endif

    for (std::size_t i = 0; i < m_config.image_config.depth_path_list.size(); ++i) {
        // we need to know the index of the path to know which channel to publish
        // the image on. For example, if the image is from the 2nd path in the list,
        // then we need to know that so that we can mark it for publication on the
        // depth2 channel.

        std::filesystem::path path = m_config.image_config.depth_path_list[i];

        for (const auto& file : std::filesystem::directory_iterator(path)) {
            if (file.is_regular_file()) {
                // checking that it's a normal file, just to be safe

                if (isImageFile(file.path().filename())) {
                    // we don't want to do anything with non-image files.

                    std::string filename = file.path().stem();

                    std::chrono::nanoseconds timestamp =
                        convertToTimestamp(m_config.image_config.timestamp_unit, std::stoull(filename));

                    m_imageData.insert({timestamp, ImageData{file.path(), i, ImageType::Depth}});
                }
            }
        }
    }

#ifndef NDEBUG
    std::clog << "Finished loading Depth Images.\n";

    std::clog << "Loading Grayscale Images...\n";
#endif

    for (std::size_t i = 0; i < m_config.image_config.grayscale_path_list.size(); ++i) {
        // we need to know the index of the path to know which channel to publish
        // the image on. For example, if the image is from the 2nd path in the list,
        // then we need to know that so that we can mark it for publication on the
        // grayscale2 channel.

        std::filesystem::path path = m_config.image_config.grayscale_path_list[i];

        for (const auto& file : std::filesystem::directory_iterator(path)) {
            if (file.is_regular_file()) {
                // checking that it's a normal file, just to be safe

                if (isImageFile(file.path().filename())) {
                    // we don't want to do anything with non-image files.

                    std::string filename = file.path().stem();

                    std::chrono::nanoseconds timestamp =
                        convertToTimestamp(m_config.image_config.timestamp_unit, std::stoull(filename));

                    m_imageData.insert({timestamp, ImageData{file.path(), i, ImageType::Grayscale}});
                }
            }
        }
    }

#ifndef NDEBUG
    std::clog << "Finished loading Grayscale Images.\n";
#endif
}

void DatasetLoader::loadPoseData() {
#ifndef NDEBUG
    std::clog << "Loading Pose Data...\n";
#endif

    for (std::size_t i = 0; i < m_config.pose_config.path_list.size(); ++i) {
        // we need to know the index of the path to know which channel to publish
        // the pose data on. For example, if the pose data is from the 2nd path in
        // the list, then we need to know that so that we can mark it for
        // publication on the pose2 channel.

        std::ifstream poseFile{m_config.pose_config.path_list[i]};

        for (CSVIterator row{poseFile, 1}; row != CSVIterator{}; ++row) {
            // we skip the first row because it contains the column names

            std::chrono::nanoseconds timestamp = convertToTimestamp(m_config.imu_config.timestamp_unit, std::stoull(row[0]));

            // TODO: how to read the pose data?
        }
    }

#ifndef NDEBUG
    std::clog << "Finished loading Pose Data.\n";
#endif
}

void DatasetLoader::loadGroundTruthData() {
#ifndef NDEBUG
    std::clog << "Loading Ground Truth Data...\n";
#endif

    // TODO: Slowly reason about the ground truth loading function and verify that it's correct.
    std::ifstream groundTruthFile{m_config.ground_truth_config.path};

    for (CSVIterator row{groundTruthFile, 1}; row != CSVIterator{}; ++row) {
        // we skip the first row because it contains the column names

        std::size_t rowIndex = 0;

        std::chrono::nanoseconds timestamp =
            convertToTimestamp(m_config.ground_truth_config.timestamp_unit, std::stoull(row[0]));

        GroundTruthData newEntry;
        Eigen::VectorXd data;

        for (std::size_t j = 0; j < m_config.ground_truth_config.name_list.size(); ++j) {
            // we want to reset the `data` variable before reading in new data at every iteration
            data.setZero();

            for (std::size_t k = 0; k < m_config.ground_truth_config.length_list[j]; ++k) {
                data[k] = std::stod(row[rowIndex++]);
            }

            newEntry.data[m_config.ground_truth_config.name_list[j]] = data;
        }

        m_groundTruthData.insert({timestamp, newEntry});
    }

#ifndef NDEBUG
    std::clog << "Finished loading Ground Truth Data.\n";
#endif
}
