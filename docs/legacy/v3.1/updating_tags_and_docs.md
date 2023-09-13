#  Updating Tags and Documentation

## Updating Tags

For releases, perform these steps from `master` once the desired features have been merged in.

1. Get latest tags:

    <!--- language: lang-none -->

        git pull --tags -f

1. Tag your branch. Please use semantic versioning to name the tag; i.e., `v<major>.<minor>.<patch>`:

    <!--- language: lang-none -->

        git tag -f <tag-name> ## `-f` is required if updating an existing tag

1. Push your tag upstream:

    <!--- language: lang-none -->

        git push origin --tags

## Updating Documentation

Perform these steps from the root directory of the project.

1. Create the directory where the generated files will be placed:

    <!--- language: lang-none -->

        mkdir -p site/api

1. Run `doxygen` to generate API documentation:

    <!--- language: lang-none -->

        doxygen doxygen.conf

1. Run `mkdocs` to deploy new documentation:

    <!--- language: lang-none -->

        mkdocs gh-deploy
