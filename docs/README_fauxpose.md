# FauxPose ILLIXR plugin

## Summary
The `FauxPose` ILLIXR plugin generates "fast_pose" data from a simple
mathematical algorithm (circular movement).  The intent is for use when
debugging other plugins and the developer wants a known pose trajectory
without having to configure actual tracking.

The movement is hard-coded to be on the Y=center.y plane, with the X and Z
values set to rotate in a circle.  The period and amplitude of the movement
are have default values (0.5, and 2.0 respectively) that can be changed
through environment variables.

    Topic details:
    -   *Publishes* `pose_position` on `fast_pose` topic.

## Usage
The "FauxPose" plugin must be included in the YAML configuration file
prior to any rendering plugin (such as "debugview" or "gldemo").  Also
no other pose-generating plugin should be included.  (In the standard
configuration this requires removing the "rt_slam_plugins" -- usually
by commenting out the line:
	#- !include "rt_slam_plugins.yaml"

And then adding FauxPose as a plugin:
        - path: fauxpose/

An example "faux.yaml" configuration file is included as an example.


By default, the "orbit" of the tracked position will be about the point
(0.0, 1.5, 0.0), with a default amplitude of "2.0", and period of "0.5"
seconds, with the orientation facing the negative-X direction.  The first
three of these values can be overridden through environment variables:
	* FAUXPOSE_PERIOD=<n>
	* FAUXPOSE_AMPLITUDE=<n>
	* FAUXPOSE_CENTER=<x,y,z>

