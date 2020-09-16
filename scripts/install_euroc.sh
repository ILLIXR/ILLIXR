#!/bin/bash

curl -o data.zip \
"http://robotics.ethz.ch/~asl-datasets/ijrr_euroc_mav_dataset/vicon_room1/V1_02_medium/V1_02_medium.zip" && \
unzip data.zip && \
rm -rf __MACOSX data1 && \
mv mav0 data1
