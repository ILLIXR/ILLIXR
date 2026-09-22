# Pose Recording and Replaying

Pose recording (capture) is the process of reading the current [poses][G12] (head, hand, hand interaction) and writing them to disk
for re-use during testing. Pose replaying is the process of reading these [poses][G12] from disk and writing them to
topics on the [switchboard][G10] for other [plugins][G11] to use. The initial implementation is aimed at capturing
OpenXR [poses][G12] from a headset.

## Pose Recording

The `record_pose.capture` [plugin][G11] reads `combined_pose` structs from the [switchboard][G10] data stream, serialize
them, and write them to files. It is intended to be run on headsets in conjunction with the `orx_interface` [plugin][G11],
or any other [plugin][G11] that publishes the `combined_pose` topic. The recording process produces 2 files:

- One capturing the full suite of [poses][G12] (head, hand, hand interactions), and has the suffix `.ipose`.
- One capturing only the head [poses][G12], and has the suffix `.hpose`.

The base name of the file can be given via the `ILLIXR_POSE_CAPTURE_FILE` environment variable (do not include the file
suffix), with a default value of `illixr_pose_capture`. The location of the file will differ depending on the Operating
System.

- Android: the files will be in `/sdcard/Android/data/com.example.native_activity`
- Linux and Windows: the files will be in your current running directory, unless you specify a full path or subdirectory

## Pose Replaying

The `record_pose.replay` [plugin][G11] reads one of the files created by the `record_pose.capture` [plugin][G11] and writing a
`combined_pose` struct to a [switchboard][G10] topic of the same name. The specific file to be read is given with the
`ILLIXR_POSE_INJECTOR_FILE`environment variable. The [plugin][G11] will automatically detect the type (either full or
head only poses) and act accordingly. There are two ways the [plugin][G11] can be run:

- On the headset: output poses are written to a networked topic for use on the server.
- On the server: output poses are written to a normal topic for use by other plugins on the system.

In both use cases a headset can be worn to view the results of the injected poses. However, the second (server-side)
replaying can be run in a headless mode as well.

[//]: # (- Glossary -)

[G10]:  ../glossary.md#switchboard

[G11]:  ../glossary.md#plugin

[G12]:  ../glossary.md#pose
