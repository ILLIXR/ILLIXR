#!/usr/bin/env sh

. $HOME/miniconda3/etc/profile.d/conda.sh

cd "$(dirname ${0})/"

conda deactivate
conda activate illixr-runner

argc=$#
if [ "$argc" -eq "2" ]; then
	python runner/runner/main.py "${1}" "${2}"
else
	python runner/runner/main.py "${1}"
fi

