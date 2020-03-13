#!/bin/sh

set -e -x

cd "$(dirname "${0}")"

docker build . \
	   --tag illixr \
   	   --build-arg USER_ID="$(id --user)" --build-arg USER_NAME="$(id --user --name)" --build-arg GROUP_ID="$(id --group)" --build-arg GROUP_NAME="$(id --group --name)" \
&& true

if [ -z "${shell}" ]
then
	cmd=/app/test.sh
else
	cmd=/bin/bash
fi

docker run --init --rm -v "$(realpath "${PWD}"):/app:rw" -it illixr ${cmd}
