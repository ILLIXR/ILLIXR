# Running ILLIXR With VIO Offloaded

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
to set up each machine to be able to send/receive UDP multicasts, and configure your local network. A detailed 
guide on how to do this can be found [here][1]. Currently, this set of plugins only supports offloading within 
your local area network. This is due to a constraint of eCAL which doesn't support communication outside the LAN.


[//]: # (- References -)

[1]:    https://continental.github.io/ecal/getting_started/cloud.html