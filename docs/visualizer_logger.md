## Visualizer & Logger

There is a utility header file `common/logger.hpp`, which is a logger for recording process starting and ending time, with respect to the component initialization time. Currently, only `audio_pipeline` and `hologram`use this logger. Please create a `log` directory in your running binary directory to let those log files correctly appear.

`log/visualizer.py`is a script to create visual timeline according to the log files in the same directory. It is not convenient to use or precisely correct right now.