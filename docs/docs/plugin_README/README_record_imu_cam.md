# Record IMU Cam

The purpose of this plugin is to record a dataset, similar to the [EuRoC MAV dataset][E10] that includes the [_IMU_][G10] data and
Cam images.

## How to record a dataset

Add `record_imu_cam` to either your input yaml file or to your `--plugins` argument when invoking the ILLIXR executable.

After recording, the dataset will be stored in the ILLIXR project directory, with the following structure:

    ILLIXR/data_record
        \_ cam0/
            \_ data/ 
                \_ timestamp.png
                \_ ...
            \_ data.csv
        \_ cam1/
            \_ data/
                \_ timestamp.png
                \_ ...
            \_ data.csv
        \_ imu0
            \_ data.csv

### Format

1. `cam0/data.csv` and `cam1/data.csv` are both formatted as

        timestamp [ns], timestamp.png
2. `imu0/data.csv` is formatted as

        timestamp [ns],w_x [rad s^-1],w_y [rad s^-1],w_z [rad s^-1],a_x [m s^-2],a_y [m s^-2],a_z [m s^-2]

## How to rerun recorded dataset

1. **(IMPORTANT)** Do not specify `record_imu_cam` in either your input yaml file or to your `--plugins` argument when
   invoking the ILLIXR executable.

2. When running the ILLIXR executable do one of the following:

    - In the input yaml file add a line to the `env_vars` section: `  data: <PATH_TO_ILLIXR>/data_record`
    - Add `--data=<PATH_TO_ILLIXR>/data_record` to the command line arguments
    - Set the environment variable `ILLIXR_DATA` to `<PATH_TO_ILLIXR>/data_record`

3. Make sure other plugins that feed images and IMU are not being used, such as [`offline_cam`][P10], 
   [`offline_imu`][P11], [`zed`][P12], and [`realsense`][P13].


[//]: # (- glossary -)

[G10]:  ../glossary.md#inertial-measurement-unit

[//]: # (- plugins -)

[P10]:  ../illixr_plugins.md#offline_cam

[P11]:  ../illixr_plugins.md#offline_imu

[P12]:  ../illixr_plugins.md#zed

[P13]:  ../illixr_plugins.md#realsense


[//]: # (- External -)

[E10]:    https://projects.asl.ethz.ch/datasets/doku.php?id=kmavvisualinertialdatasets
