#!/bin/bash

export ILLIXR_DATA=.cache/paths/http%%c%%s%%srobotics.ethz.ch%%s~asl-datasets%%sijrr_euroc_mav_dataset%%svicon_room1%%sV1_02_medium%%sV1_02_medium.zip/mav0
export ILLIXR_DEMO_DATA=demo_data 
export ILLIXR_OFFLOAD_ENABLE=False 
export ILLIXR_ALIGNMENT_ENABLE=False 
export ILLIXR_ENABLE_VERBOSE_ERRORS=False 
export ILLIXR_RUN_DURATION=10
export ILLIXR_ENABLE_PRE_SLEEP=False 
export KIMERA_ROOT=.cache/paths/https%c%s%sgithub.com%sILLIXR%sKimera-VIO.git/ 
export AUDIO_ROOT=.cache/paths/https%c%s%sgithub.com%sILLIXR%saudio_pipeline.git/ 
export REALSENSE_CAM=auto 
export __GL_MaxFramesAllowed=1 
export __GL_SYNC_TO_VBLANK=1 

runtime/main.opt.exe offline_imu/plugin.opt.so offline_cam/plugin.opt.so .cache/paths/https%c%s%sgithub.com%sILLIXR%sKimera-VIO.git/plugin.opt.so gtsam_integrator/plugin.opt.so pose_prediction/plugin.opt.so ground_truth_slam/plugin.opt.so gldemo/plugin.opt.so debugview/plugin.opt.so offload_data/plugin.opt.so timewarp_gl/plugin.opt.so .cache/paths/https%c%s%sgithub.com%sILLIXR%saudio_pipeline.git/plugin.opt.so
