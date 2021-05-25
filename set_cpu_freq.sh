#!/usr/bin/env sh

. $HOME/miniconda3/etc/profile.d/conda.sh

conda run \
	  --no-capture-output \
	  --cwd "$(dirname ${0})" \
	  --name illixr-runner \
	  python runner/runner/set_cpu_freq.py "${@}"
