# Running ILLIXR With VIO Offloaded

In order to run the offloading plugins you need the following extra dependecies

```
sudo add-apt-repository ppa:ecal/ecal-latest
sudo apt-get update
sudo apt-get install ecal
sudo apt-get install libprotobuf-dev protobuf-compiler
```

The most simple example is running the offloaded setup on one machine with the server in one terminal
and the device running in a different terminal. **Each terminal must be running from separate ILLIXR 
repositories** (clone ILLIXR twice to different locations; running both the server and device from the same 
ILLIR repository will cause lock issues). 

Start ILLIXR using
``` bash
main.opt.exe --yaml=profiles/offload-server.yaml --data=<> --demo_data=<>
```
for one terminal and 
``` bash
main.opt.exe --yaml=profiles/offload-device.yaml --data=<> --demo_data=<>

```
on the other terminal. This will run OpenVINS on the server and will feed it with the EuRoC dataset from the device.

To run more complicated experiment setups where the device and server are not on the same machine, you will need
to establish TCP communication between machines, you'll need to configure both the server and client settings.
The IP address and port number can be set in the configuration file:

`include/illixr/network/net_config.hpp`

This file contains variables where you can enter the respective IP addresses and port numbers for the server and client.
Edit these fields to suit your network setup.

## Compression

H.264 codec is supported for compressing the camera images to save bandwidth. To enable compression, define `USE_COMPRESSION` in `device_tx/plugin.cpp` and `server_rx/plugin.cpp`. In `device_tx/video_encoder.cpp` and `server_rx/video_decoder.cpp`, define appropriate image dimensions and desired target bitrate (defaults to 5Mbps). The codec library is implemented based on GStreamer and DeepStream. Please follow the instructions [here][1] to install GStreamer and DeepStream SDK. You don't have to reinstall CUDA and NVIDIA Driver if you have a relatively new version. TensorRT and librdkafka are not required either.


[//]: # (- References -)
[1]: https://docs.nvidia.com/metropolis/deepstream/dev-guide/text/DS_Quickstart.html#dgpu-setup-for-ubuntu
