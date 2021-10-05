#!/usr/bin/env sh

. $HOME/miniconda3/etc/profile.d/conda.sh

gpu_clock=$(nvidia-smi -q -d CLOCK | grep 'Max Clocks' -A 4 | sed -nr 's/ +SM +: ([0-9]+) MHz/\1/p')
sudo nvidia-smi -lgc "${gpu_clock},${gpu_clock}"

conda run \
	  --no-capture-output \
	  --cwd "$(dirname ${0})" \
	  --name illixr-runner \
	  python runner/runner/main.py "${@}"
