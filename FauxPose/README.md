# FauxPose IlliXR plugin

## Summary
The `FauxPose` IlliXR plugin generates "fast_pose" data from a simple
mathematical algorithm (circular movement).  The intent is for use when
debugging other plugins and the developer wants a known pose trajectory
without having to configure actual tracking.

The movement is hard-coded to be on the Y=1.5m plane, with the X and Z
values set to rotate in a circle.  The period and amplitude of the movement
are currently hard-coded within the plugin constructor.

## Usage
The "FauxPose" plugin must be included in the YAML configuration file
prior to any rendering plugin (such as "debugview" or "gldemo").  Also
no other pose-generating plugin should be included.  (In the standard
configuration this requires removing the "rt_slam_plugins" -- usually
by commenting out the line:
	#- !include "rt_slam_plugins.yaml"

