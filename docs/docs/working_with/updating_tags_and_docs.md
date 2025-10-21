#  Updating Tags and Documentation

## Updating Tags

For releases, perform these steps from `master` once the desired features have been merged in.

1. Get latest tags:

    ```  { .bash .copy }
    git pull --tags -f
    ```

2. Tag your branch. Please use semantic versioning to name the tag; i.e., `v<major>.<minor>.<patch>`:

    ``` { .bash .copy }
    git tag -f <tag-name> ## `-f` is required if updating an existing tag
    ```

3. Push your tag upstream:

    ``` { .bash .copy }
    git push origin --tags
    ```
   
## Updating Documentation

To generate the documentation (will pick up any changes) be sure to add `-DBUILD_DOCS=ON` to your cmake flags. Then from the build directory run:

    ```  { .bash .copy }
    cmake --build . -t docs  # this will put the generated documentation in <build_path>/docs/docs
    cmake --install .        # this will install the generated docs in your install directory under 'share/doc/ILLIXR/docs'
    ```
