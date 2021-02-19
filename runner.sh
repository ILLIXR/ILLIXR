#!/usr/bin/env sh

. $HOME/miniconda3/etc/profile.d/conda.sh

cd "$(dirname ${0})/"

conda deactivate
conda activate illixr-runner

export ILLIXR_INTEGRATION=yes

python runner/runner/main.py "${@}"
