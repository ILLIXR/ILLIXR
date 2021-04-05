# ILLIXR Debugging Tips


## Debugging Locally
The config described in [Building ILLIXR][10] supports running the runtime with
    arbitrary commands like `gdb`.
When debugging locally, we recommend using either `gdb` or `valgrind` in this way.

## Debugging Pull Requests or with a Clean Environment
### 1. Get a Docker Image
#### From your Local Project
From the root directory in your project, run:

<!--- language: lang-shell -->
    docker build [--build-arg=JOBS="<integer>"] [--no-cache] --tag <repository>:<tag> .

Note the optional `Dockerfile` argument, `JOBS`, which specifies the number of threads/tasks to use for building.
Also note the optional argument, `--no-cache`, which forces Docker to rerun commands in `Dockerfile`
    (see [this article][1] for more information).
For this project's main module, you can use something like `illixr-illixr` for the `<repository>` value,
    and your current branch name or release version as the `<tag>` value.

Note that building the docker image can take some time (up to 40min on a 4-core desktop machine) and uses somewhere between 2-4GB of RAM.

#### From a GitHub Pull Request's CI/CD Flow
Follow these steps when a CI/CD build fails on a PR:

-   Click `details` on the failing build.

-   In the build view go to the Push Docker Image tab and copy the `docker push ghcr.io/illixr/illixr-tests:<branch-name>` command.

-   Then in your terminal, run:

    <!--- language: lang-shell -->

        docker pull ghcr.io/illixr/illixr-tests:<branch-name>

### 2. Test your Image in the Docker container
Verify that your image was created successfully:

<!--- language: lang-shell -->
    docker image ls

Take note of your image's `REPOSITORY` and `TAG` values.
Now run:

<!--- language: lang-shell -->
    docker run -it --entrypoint /bin/bash <repository>:<tag>

You are now in a bash shell in a docker container.

From here you can test whichever project flow you wish, such as the usual `./runner.sh configs/native.yaml`,
    or the CI/CD testing flow (`./runner.sh configs/ci.yaml`).


[//]: # (- References -)

[1]:    https://www.baeldung.com/linux/docker-build-cache

[//]: # (- Internal -)

[10]:   building_illixr.md
