# ILLIXR Debugging Tips

## Debugging Locally
The config described in [Building ILLIXR][1] supports running the runtime with arbitrary commands like `gdb`. When debugging locally we recommend using either `gdb` or `valgrind` in this way.

## Debugging PRs
### Running the CI/CD Docker Container Locally
Follow these steps when a CI/CD build fails on a PR:

- Click details on the failing build
- In the build view go to the Push Docker Image tab and copy the `docker push ghcr.io/illixr/illixr-tests:<branch-name>` command
- Then in your terminal run `docker pull ghcr.io/illixr/illixr-tests:<branch-name>`
- Finally run `docker run -it --entrypoint /bin/bash ghcr.io/illixr/illixr-tests:<branch-name>`

This now gives you a bash shell in a docker container with all of the code that ran on CI/CD.

[1]: building_illixr.md
