#!/usr/bin/env sh


cmd_run="./runner.sh"
config_clean="configs/clean.yaml"

if [ -f "${cmd_run}" ]; then
    ${cmd_run} ${config_clean}
else
    echo "Unable to find runner at location '${cmd_run}'"
fi
