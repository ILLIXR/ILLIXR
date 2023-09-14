# Record IMU Cam

The purpose of this plugin is to record a dataset, similar to the [EuRoC MAV dataset][1] that includes the IMU data and Cam images.
    

## How to record a dataset

In `configs/rt_slam_plugins.yaml`, uncomment this line:

    - path: record_imu_cam/

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

1. **(IMPORTANT)** In `configs/rt_slam_plugins.yaml`, comment this line: 

       # - path: record_imu_cam/  

2. In `configs/native.yaml` (or whatever mode you're running ILLIXR with), add the path of the recorded dataset like so:

        data: data_record
          # subpath: mav0
          # relative_to:
          #  archive_path:
          #    download_url: 'http://robotics.ethz.ch/~asl-datasets/ijrr_euroc_mav_dataset/vicon_room1/V1_02_medium/V1_02_medium.zip'

    Make sure to comment the default EuRoC dataset

3. In `runner/runner/main.py`, head toward the function corresponding to the mode with which you want to run ILLIXR, and change the `data_path` line like so (if run natively, it is [this line][2]):

        data_path = pathify(config["data"], root_dir, root_dir / "data_record", True, True)

4. Make sure other plugins that feed images and IMU are commented, such as `offline_cam`, `offline_imu`, `zed`, and `realsense`.

[//]: # (- External -)

[1]:    https://projects.asl.ethz.ch/datasets/doku.php?id=kmavvisualinertialdatasets
[2]:    https://github.com/ILLIXR/ILLIXR/blob/21832a1dbf132fa61718ba86bd87ca6130301517/runner/runner/main.py#L95
