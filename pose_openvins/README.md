# Getting Started with [OpenVINS](https://github.com/rpng/open_vins/)

## Building OpenVINS

0. The main repository is located at https://github.com/rpng/open_vins/
1. Install OpenVINS as a global library using the `scripts/install_openvins.sh`
2. From there, the [Makefile](Makefile) will build and link to the `libov_msckf_lib.so`
3. Configure the plugin to be used and the config
    - Enable `- path: pose_openvins/` in your plugin group
    - In actions, set `openvins_path: pose_openvins/` path to where the config files are located
    - In actions, set `openvins_sensor: euroc_mav` the name of the config file (e.g. euroc_mav, rs_t265, rs_d455, ...)
4. Run ILLIXR as normal now!



## Converting ROS Bags

- This will use [Kalibr](https://github.com/ethz-asl/kalibr) so ensure you have built it
- Example data is the D455 from [ar_table_dataset](https://github.com/rpng/ar_table_dataset)
```
rosrun kalibr kalibr_bagextractor \
    --bag table_01.bag \
    --image-topics /d455/color/image_raw \
    --imu-topics /d455/imu \
    --output-folder table_01/.
```
- From here you can generate a groundtruth with [vicon2gt](https://github.com/rpng/vicon2gt/) and name it `state0.csv`
- The final folder structure should be:
```
cam0/
cam1/ (optional)
imu0.csv
state0.csv
```



## Adding a New Sensor

To add a new sensor the user should first perform calibration of it to find the intrinsics.
The high level can be found on the [OpenVINS documentation](https://docs.openvins.com/gs-calibration.html), but they are as follows:

- Record a 10-24hr dataset of just the IMU and process it
  - Use https://github.com/ori-drs/allan_variance_ros
  - Example script is [located here](https://github.com/rpng/ar_table_dataset/blob/master/calibrate_imu.sh)
- Record and calibrate the camera intrinsics using [Kalibr](https://github.com/ethz-asl/kalibr)
  - Record a datset with a calibration aprilgrid board (don't use a checkerboard)
  - Process it (you can downsample the framerate to 10hz with `--bag-freq 10.0`)
  - Example script is [located here](https://github.com/rpng/ar_table_dataset/blob/master/calibrate_camera_static.sh)
- Record and calibrate the camera to IMU extrinsics using [Kalibr](https://github.com/ethz-asl/kalibr)
  - Needs to be very dynamic and excite all axes of the IMU and be high framerate
  - Example script is [located here](https://github.com/rpng/ar_table_dataset/blob/master/calibrate_camera_dynamic.sh)
- Now you can create a config directory with the following:
  - `your_sensor/estimator_config.yaml` - Copy this from the `eruoc_mav`, you shouldn't need to make any large changes besides camera number
  - `your_sensor/kalibr_imu_chain.yaml` - Needs to be created using the Kalibr output (the tabs need to be fixed and `%YAML:1.0` added to the top)
  - `your_sensor/kalibr_imucam_chain.yaml` - Needs to be created using the Kalibr output (the tabs need to be fixed and `%YAML:1.0` added to the top)
- Update `openvins_sensor: euroc_mav` to point to your new sensor folder with `openvins_sensor: your_sensor`



## Credit

Thank you the RPNG Group for the wonderful project.
Credits are also due to those who authored, contributed, and maintained this plugin:
[Rishi Deshai](https://github.com/therishidesai), 
[Giordano Salvador](https://github.com/e3m3),
[Jeffrey Zhang](https://github.com/JeffreyZh4ng),
[Mohammed Huzaifa](https://github.com/mhuzai),
[Jae Lee](https://github.com/Hyjale),
[Samuel Grayson](https://github.com/charmoniumQ),
[Qinjun Jiang](https://github.com/qinjunj),
and [Henry Che](https://github.com/hungdche). 


The linked OpenVINS code was written by the [Robot Perception and Navigation Group (RPNG)](https://sites.udel.edu/robot/) at the University of Delaware.
If you have any issues with the code please open an issue on [their github page](https://github.com/rpng/open_vins/) with relevant implementation details and references.
For researchers that have leveraged or compared to this plugin component, please cite the following:
```
@Conference{Geneva2020ICRA,
  Title      = {{OpenVINS}: A Research Platform for Visual-Inertial Estimation},
  Author     = {Patrick Geneva and Kevin Eckenhoff and Woosik Lee and Yulin Yang and Guoquan Huang},
  Booktitle  = {Proc. of the IEEE International Conference on Robotics and Automation},
  Year       = {2020},
  Address    = {Paris, France},
  Url        = {\url{https://github.com/rpng/open_vins}}
}
```


