FROM ubuntu:18.04

ADD ./* $HOME/ILLIXR

ENV DEBIAN_FRONTEND noninteractive

WORKDIR ILLIXR

RUN install_deps.sh -y

ENTRYPOINT runner.sh configs/ci.yaml