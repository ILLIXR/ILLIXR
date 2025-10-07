## Summary

`offload_vio` implements the functionality of moving VIO (Visual Inertial Odometry) to some server.
There are four plugins that handle transmission (`_tx`) and reception (`_rx`) of data on the client (`device`) and
server (`server`) respectively.

## Usage

In order to run offloading vio, you need the following extra dependecies

```
sudo add-apt-repository ppa:ecal/ecal-latest
sudo apt-get update
sudo apt-get install ecal
sudo apt-get install libprotobuf-dev protobuf-compiler
```

Please refer to the README in `tcp_network_backend` for setting the server and client IP address and port number.

The most simple example is running the offloaded setup on one machine with the server running in one terminal
and the device running in a different terminal. **Each terminal must be running from separate ILLIXR
repositories** (clone ILLIXR twice to different locations; running both the server and device from the same
ILLIR repository will cause lock issues).

Start the server using

``` bash
main.opt.exe --yaml=profiles/offload_vio_server.yaml --data=<> --demo_data=<>
```

in one terminal and

``` bash
main.opt.exe --yaml=profiles/offload_vio_device.yaml --data=<> --demo_data=<>

```

in the other terminal for the client. This will run OpenVINS on the server and will feed it with the EuRoC dataset (
replace <> in --data with `data`) from the device.

To run the client and server on different machines, please again refer to the README in `tcp_network_backend` for
setting appropriate server IP address and port number.

## Compression

H.264 codec is supported for compressing the camera images to save bandwidth. To enable compression, define
`USE_COMPRESSION` and `VIO` in `device_tx/plugin.cpp` and `server_rx/plugin.cpp` (defaulted to not enabled). In
`include/illixr/video_encoder.hpp` and `include/illixr/video_decoder.hpp`, define appropriate image dimensions and desired target
bitrate (defaults to 5Mbps). The codec library is implemented based on GStreamer and DeepStream. Please follow the
instructions [here][E10] to install GStreamer and DeepStream SDK. You don't have to reinstall CUDA and NVIDIA Driver if
you have a relatively new version. TensorRT and librdkafka are not required either.

[//]: # (- References -)

[E10]: https://docs.nvidia.com/metropolis/deepstream/dev-guide/text/DS_Installation.html#dgpu-setup-for-ubuntu
