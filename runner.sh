#!/usr/bin/env sh

orig_cwd="${PWD}"

cd "$(dirname ${0})/runner"

config_file="${1}"

if ! echo "${config_file}" | grep "^/"; then
	config_file="${orig_cwd}/${config_file}"
fi

poetry run runner/main.py "${config_file}"
