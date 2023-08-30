# OpenNI

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

### Note

Make sure to clean openni everytime you make new changes

```
cd ILLIXR/openni && make clean
```
