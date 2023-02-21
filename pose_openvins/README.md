# Getting Started with OpenVINS

## Building OpenVINS

1. Install OpenVINS as a global library using the `scripts/install_openvins.sh`
2. From there, the [Makefile](Makefile) will build and link to the `libov_msckf_lib.so`
3. Configure the plugin to be used and the config
    - Enable `- path: pose_openvins/` in your plugin group
    - In actions, set `openvins_path: pose_openvins/` path to where the config files are located
    - In actions, set `openvins_sensor: euroc_mav` the name of the config file (e.g. euroc_mav, rs_t265, rs_d455, ...)
4. Run ILLIXR as normal now!



## Converting ROS Bags

- This will use [Kalibr](https://github.com/ethz-asl/kalibr)
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



## Credit

Thank you the RPNG Group for the wonderful project. Credits are also due to those who authored, contributed, and maintained this plugin: [Rishi Deshai](https://github.com/therishidesai), 
[Giordano Salvador](https://github.com/e3m3), [Jeffrey Zhang](https://github.com/JeffreyZh4ng), [Mohammed Huzaifa](https://github.com/mhuzai), [Jae Lee](https://github.com/Hyjale), [Samuel Grayson](https://github.com/charmoniumQ), [Qinjun Jiang](https://github.com/qinjunj), and [Henry Che](https://github.com/hungdche). 


