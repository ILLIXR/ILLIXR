**Part of [ILLIXR](https://github.com/ILLIXR/ILLIXR), the Illinois Extended Reality Benchmark Suite.**

# Getting Started with OpenVINS

## Building OpenVINS

Standalone OpenVINS compilation:

```
git clone https://github.com/ILLIXR/open_vins.git
cd open_vins/ILLIXR/
make [opt|dbg]
```

## Running OpenVINS Standalone

Now to run OpenVINS Standalone do the following:
```
cd build/[RelWithDebInfo|Debug]
./run_illixr_stal <path_to_config> <path_to_cam0> <path_to_cam1> <path_to_imu0> <path_to_cam0_images> <path_to_cam1_images>
```

When running OpenVINS we assume the data is formatted in the [EUROC Dataset standard](https://projects.asl.ethz.ch/datasets/doku.php?id=kmavvisualinertialdatasets).

## Credit

Thank you the RPNG Group for the wonderful project. Credits are also due to those who authored, contributed, and maintained this plugin: [Rishi Deshai](https://github.com/therishidesai), 
[Giordano Salvador](https://github.com/e3m3), [Jeffrey Zhang](https://github.com/JeffreyZh4ng), [Mohammed Huzaifa](https://github.com/mhuzai), [Jae Lee](https://github.com/Hyjale), [Samuel Grayson](https://github.com/charmoniumQ), [Qinjun Jiang](https://github.com/qinjunj), and [Henry Che](https://github.com/hungdche). 


