#!/usr/bin/env sh

. $HOME/miniconda3/etc/profile.d/conda.sh

cd "$(dirname ${0})/"

conda deactivate
conda activate illixr-runner

python runner/runner/main.py "${@}"
