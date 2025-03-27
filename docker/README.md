Note: These docker builds are incomplete, as we are redesigning the way they are offered.

This directory contains Docker files which can be used to create Docker images for any of the supported OS versions.
Each container has the default user `illixr`, and a script called `cmk.sh` in the home directory. This script can be used to configure, build, and install ILLIXR in the container.
Note that this requires [Docker](https://docker.com) to be installed.

To build the containers run any of the following (depending on what OS's you need):

**Ubuntu**
```bash
docker build -t illixrubuntu:v4.0 -f ubuntu/Dockerfile .
```

**Fedora**
```bash
docker build -t illixrfedora:v4.0 -f fedora//Dockerfile .
```
To run a container
```bash
docker run -ti -p 8181:8181 <image>
```

where image is one of
```bash
illixrubuntu:v4.0
illixrfedora:v4.0
```
