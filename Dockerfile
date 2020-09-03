FROM ubuntu:18.04

COPY . $HOME/ILLIXR/

ENV DEBIAN_FRONTEND noninteractive

WORKDIR ILLIXR

RUN apt update && apt install -y sudo apt-transport-https

RUN ./install_deps.sh -y

ENTRYPOINT runner.sh configs/ci.yaml
