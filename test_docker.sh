#!/bin/sh

set -e -x

cd "$(dirname "${0}")"

mkdir -p blank

if [ -z "${shell}" ]
then
	cmd=/app/test.sh
else
	cmd=/bin/bash
fi

docker run --cap-add=SYS_PTRACE --security-opt seccomp=unconfined --init --rm -v "$(realpath "${PWD}"):/app:rw" -it charmonium/illixr:6.0 ${cmd}
