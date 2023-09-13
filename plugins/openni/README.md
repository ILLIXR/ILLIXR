# OpenNI

## Installation

OpenNI should be install when running `install_deps.sh`. This will download OpenNI to `/opt/ILLIXR/`

### Note: 

If OpenNI is installed anywhere else, `OPENNI_ROOT` needs to be passed in as an environment variable before running ILLIXR. For example:

```
export OPENNI_ROOT=/path/to/openni
./runner.sh configs/native-gl.yaml
```

### Important!

Make sure to clean `openni` everytime you make any new changes related to OpenNI

```
cd ILLIXR/openni && make clean
```

## Set Camera Mode

If you wish to switch up the video modes of OpenNI, update these two macros in `openni/plugin.cpp` accordingly:
```
#define RGB_MODE 0
#define DEPTH_MODE 0
```

You can see the list of available modes once you run ILLIXR the first time. 

## Debugging

```
Device open failed: 	DeviceOpen using default: no devices found
```
**Solution**: No OpenNI compatible device is plugged in. So plug one in

```
Device open failed: 	Could not open "1d27/0601@3/2": USB transfer timeout!
```
**Solution**: This is usually fixed by unplug and plug back in.

```
Device open failed: 	Could not open "1d27/0601@3/2": Failed to open the USB device!
```
**Solution**: Fix by running with sudo 


