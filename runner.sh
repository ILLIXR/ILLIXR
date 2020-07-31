#!/usr/bin/env sh
cd "$(dirname ${0})/runner"
poetry run runner/main.py ../config.yaml
