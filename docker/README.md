This directory contains Docker files which can be used to create Docker images for any of the supported OS versions.
Each container has the default user `illixr`, and a script called `cmk.sh` in the home directory. This script can be used to configure, build, and install ILLIXR in the container.
Note that this requires [Docker](https://docker.com) to be installed.

To build the containers run any of the following (depending on what OS's you need):

**Ubuntu 20.04**
```bash
docker build -t illixrubuntu:20 -f ubuntu/20/Dockerfile .
```

**Ubuntu 22.04**
```bash
docker build -t illixrubuntu:22 -f ubuntu/22/Dockerfile .
```

**Fedora 37**
```bash
docker build -t illixrfedora:37 -f fedora/37/Dockerfile .
```

**Fedora 38**
```bash
docker build -t illixrfedora:38 -f fedora/38/Dockerfile .
```

**CentOS stream9**
```bash
docker build -t illixrcentos:9 -f centos/9/Dockerfile .
```

To run a container
```bash
docker run -ti -p 8181:8181 <image>
```

where image is one of
```bash
illixrubuntu:20
illixrubuntu:22
illixrfedora:37
illixrfedora:38
illixrcentos:9
```