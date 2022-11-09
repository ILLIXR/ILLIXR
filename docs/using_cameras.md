
# Using Cameras with ILLIXR

ILLIXR supports a wide range of cameras, most of which are for the purpose of feeding images and IMU measurements to the system. <br>
*To learn more about how it works, checkout [Building ILLIXR][12].* <br>
*If you are interested in what topics these cameras feed into, checkout [ILLIXR plugins][10].*


**Important Note**: <br>
Before running any of these plugins below, it is important to comment out `offline_imu_cam` in `configs\native.yaml`. <br>

## Add Calibration Parameters
In order to add your camera's calibration extrinsics, you need to modify your choice of SLAM/VIO plugin. Follow these instructions to [modify a plugin][21].

-   [`Kimera-VIO`][5]:
    
    Navigate to `params`. <br>
    Add your calibration here. Look at the other folders for reference. <br>
    Modify [this line][7] in `examples/plugin.cpp` to point to your calibration folder. 

-   [`OpenVINS`][6]:
    
    Navigate to `ov_msckf/src`.<br>
    Uncomment [this line][3] out in `slam2.cpp` in order to use ZED's calibration parameter for OpenVins. <br>
    You can add your own calibration parameters in the same file.     

## ZED Mini

1. **Install ZED SDK** 

    Install the latest version of the ZED SDK on [stereolabs.com][1].<br>
    For more information, checkout the [ZED API documentation][2].

2. **Get ZED's calibration parameters**

    Both OpenVINS and Kimera have a decent calirbation parameters for ZED. But if you wish to add your own: 
    
        ./usr/local/zed/tools/ZED_Calibration

    Your original factory calibration file is stored here

        /usr/local/zed/settings/

    Or download it from [calib.stereolabs.com][8].

3. **Run ILLIXR with ZED:**  

    Uncomment `zed` in `configs\native` and run ILLIXR normally.

## Intel Realsense 

ILLIXR has been tested with Inteal RealSense D455, but it should work with any D or T series RealSense Camera. 

1. **Install librealsense (if you haven't already):**

    Instruction on how to install can be found [here][4].

2. **Get RealSense calibration parameters:**

    Navigate to `enumerate-devices`
    
        ./PATH/TO/LIBREALSENSE/build/tools/enumerate-devices 

    Run this command to obtain the calibration parameters

        ./rs-enumerate-devices -c

3. **Run ILLIXR with RealSense:**  

    Uncomment `realsense` in `configs\native` and run ILLIXR normally.



[//]: # (- References -)

[1]: https://www.stereolabs.com/docs/installation/linux/
[2]: https://www.stereolabs.com/docs/api/
[3]: https://github.com/ILLIXR/open_vins/blob/43b42dddaf9d3b8e6257e0bb8a91053b59a677e4/ov_msckf/src/slam2.cpp#L24
[4]: https://github.com/IntelRealSense/librealsense/blob/development/doc/distribution_linux.md
[5]: https://github.com/ILLIXR/Kimera-VIO
[6]: https://github.com/ILLIXR/open_vins
[7]: https://github.com/ILLIXR/Kimera-VIO/blob/4ba8c4a8deede4fa089b545fd34ce100a38bf4b2/examples/plugin.cpp#L26
[8]: https://www.stereolabs.com/developers/calib/

[//]: # (- Internal -)

[10]:   illixr_plugins.md
[11]:   writing_your_plugin.md
[12]:   building_illixr.md
[13]:   glossary.md#spindle
[14]:   glossary.md#switchboard
[15]:   glossary.md#phonebook
[16]:   virtualization.md
[17]:   glossary.md#xvfb
[18]:   glossary.md#monado
[19]:   glossary.md#openxr
[20]:   glossary.md#qemu-kvm
[21]:   modifying_a_plugin.md