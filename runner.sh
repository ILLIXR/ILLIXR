#!/usr/bin/env sh

. $HOME/miniconda3/etc/profile.d/conda.sh

orig_cwd="${PWD}"

cd "$(dirname ${0})/runner"

config_file="${1}"

# if config_file is relative, make it relative to the orig_cwd
if ! echo "${config_file}" | grep "^/"; then
	config_file="${orig_cwd}/${config_file}"
fi


conda deactivate
conda activate illixr-runner
python runner/main.py "${config_file}"
