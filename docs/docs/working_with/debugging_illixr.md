# ILLIXR Debugging Tips

## Debugging Locally

The config described in [Getting Started][10] supports running the runtime with
arbitrary commands like `gdb`.
When debugging locally, we recommend using either `gdb` or `valgrind` in this way.
You can use the `ENABLE_PRE_SLEEP` environment variable or `enable_pre_sleep` command line argument to set a sleep time
before ILLIXR fully starts. This gives you time to attach a debugger.

[//]: # (- Internal -)

[10]:   ../getting_started.md
